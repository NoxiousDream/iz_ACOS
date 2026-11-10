#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#define MAX_PATIENTS  100
#define MAX_QUEUE     50
#define SPEC_COUNT    3
#define NUM_DUTY      2
#define SHM_NAME      "/hospital_shm_named"
#define SEM_MUTEX     "/hosp_mutex_n"
#define SEM_PRIMARY   "/hosp_primary_n"
#define SEM_SPEC_0    "/hosp_spec0_n"
#define SEM_SPEC_1    "/hosp_spec1_n"
#define SEM_SPEC_2    "/hosp_spec2_n"



static const char *SPEC_NAMES[SPEC_COUNT] = {"Стоматолог","Хирург","Терапевт"};

/* Состояния пациента */
enum { ST_WAIT_PRIMARY=0, ST_IN_PRIMARY, ST_WAIT_SPEC, ST_IN_TREAT, ST_DONE, ST_INTERRUPTED };

/* Разделяемая память */
typedef struct {
    int primary_queue[MAX_QUEUE];
    int primary_count;
    int spec_queues[SPEC_COUNT][MAX_QUEUE];
    int spec_counts[SPEC_COUNT];
    int patient_state[MAX_PATIENTS];
    int patient_spec[MAX_PATIENTS];
    int patient_urgent[MAX_PATIENTS];
    int patient_exam_ms[MAX_PATIENTS];
    int patient_treat_ms[MAX_PATIENTS];
    int total_arrived;
    int total_discharged;
    int all_generated;
    int shutdown;
} SharedState;

static volatile sig_atomic_t g_interrupt = 0;
static void sig_handler(int s){ (void)s; g_interrupt = 1; }

static int log_fd = -1;
static void log_msg(const char *fmt, ...){
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(STDOUT_FILENO, buf, len);
    if(log_fd >= 0) write(log_fd, buf, len);
}

static void cleanup(void){
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_PRIMARY);
    sem_unlink(SEM_SPEC_0);
    sem_unlink(SEM_SPEC_1);
    sem_unlink(SEM_SPEC_2);
    if(log_fd >= 0) close(log_fd);
}

/* Процесс дежурного врача */
static void duty_doctor(int id, SharedState *sh, sem_t *mtx, sem_t *prim_sem){
    for(;;){
        sem_wait(prim_sem);
        sem_wait(mtx);
        if(sh->shutdown){ sem_post(mtx); break; }
        if(sh->primary_count <= 0){ sem_post(mtx); continue; }
        int pid = sh->primary_queue[0];
        for(int i=0;i<sh->primary_count-1;i++) sh->primary_queue[i]=sh->primary_queue[i+1];
        sh->primary_count--;
        sh->patient_state[pid] = ST_IN_PRIMARY;
        sem_post(mtx);

        log_msg("[ДВ-%d] Начал осмотр пациента %d\n", id, pid);
        usleep(sh->patient_exam_ms[pid] * 1000);

        sem_wait(mtx);
        sh->patient_state[pid] = ST_WAIT_SPEC;
        /* Направление к специалисту уже определено при генерации */
        int spec = sh->patient_spec[pid];
        if(sh->spec_counts[spec] < MAX_QUEUE){
            sh->spec_queues[spec][sh->spec_counts[spec]++] = pid;
        }
        sem_post(mtx);
        log_msg("[ДВ-%d] Закончил осмотр пациента %d -> направлен к %s\n", id, pid, SPEC_NAMES[spec]);
    }
}

/* Процесс специалиста */
static void specialist(int spec, SharedState *sh, sem_t *mtx, sem_t *spec_sem){
    for(;;){
        sem_wait(spec_sem);
        sem_wait(mtx);
        if(sh->shutdown){ sem_post(mtx); break; }
        if(sh->spec_counts[spec] <= 0){ sem_post(mtx); continue; }
        int pid = sh->spec_queues[spec][0];
        for(int i=0;i<sh->spec_counts[spec]-1;i++)
            sh->spec_queues[spec][i]=sh->spec_queues[spec][i+1];
        sh->spec_counts[spec]--;
        sh->patient_state[pid] = ST_IN_TREAT;
        sem_post(mtx);

        log_msg("[%s] Начал лечение пациента %d\n", SPEC_NAMES[spec], pid);
        usleep(sh->patient_treat_ms[pid] * 1000);

        sem_wait(mtx);
        sh->patient_state[pid] = ST_DONE;
        sh->total_discharged++;
        sem_post(mtx);
        log_msg("[%s] Закончил лечение пациента %d — выписан\n", SPEC_NAMES[spec], pid);
    }
}

/* Процесс пациента */
static void patient_proc(int pid, SharedState *sh, sem_t *mtx, sem_t *prim_sem, sem_t *spec_sems[]){
    /* Ждём, пока дежурный заберёт из очереди */
    for(;;){
        sem_wait(mtx);
        int st = sh->patient_state[pid];
        int shut = sh->shutdown;
        sem_post(mtx);
        if(st != ST_WAIT_PRIMARY || shut) break;
        usleep(5000);
    }
    sem_wait(mtx);
    if(sh->shutdown){ sh->patient_state[pid]=ST_INTERRUPTED; sem_post(mtx); return; }
    sem_post(mtx);

    /* Ждём окончания первичного осмотра (состояние сменит врач) */
    for(;;){
        sem_wait(mtx);
        int st = sh->patient_state[pid];
        int shut = sh->shutdown;
        sem_post(mtx);
        if(st != ST_IN_PRIMARY || shut) break;
        usleep(5000);
    }
    sem_wait(mtx);
    if(sh->shutdown){ sh->patient_state[pid]=ST_INTERRUPTED; sem_post(mtx); return; }
    sem_post(mtx);

    /* Сигналим специалисту */
    int spec = sh->patient_spec[pid];
    sem_post(spec_sems[spec]);

    /* Ждём, пока специалист заберёт */
    for(;;){
        sem_wait(mtx);
        int st = sh->patient_state[pid];
        int shut = sh->shutdown;
        sem_post(mtx);
        if(st != ST_WAIT_SPEC || shut) break;
        usleep(5000);
    }
    sem_wait(mtx);
    if(sh->shutdown){ sh->patient_state[pid]=ST_INTERRUPTED; sem_post(mtx); return; }
    sem_post(mtx);

    /* Ждём окончания лечения */
    for(;;){
        sem_wait(mtx);
        int st = sh->patient_state[pid];
        int shut = sh->shutdown;
        sem_post(mtx);
        if(st != ST_IN_TREAT || shut) break;
        usleep(5000);
    }
}

int main(int argc, char *argv[]){
    int total = (argc > 1) ? atoi(argv[1]) : 15;
    unsigned seed = (argc > 2) ? atoi(argv[2]) : 42;
    if(total > MAX_PATIENTS) total = MAX_PATIENTS;
    srand(seed);
    signal(SIGINT, sig_handler);

    log_fd = open("hospital_named.log", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    log_msg("=== БОЛЬНИЦА: именованные семафоры + shared memory ===\n");
    log_msg("Пациентов: %d, seed: %u\n\n", total, seed);

    /* Разделяемая память */
    int shm_fd = shm_open(SHM_NAME, O_CREAT|O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedState));
    SharedState *sh = mmap(NULL, sizeof(SharedState), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    memset(sh, 0, sizeof(SharedState));

    /* Именованные семафоры */
    sem_unlink(SEM_MUTEX); sem_unlink(SEM_PRIMARY);
    sem_unlink(SEM_SPEC_0); sem_unlink(SEM_SPEC_1); sem_unlink(SEM_SPEC_2);
    sem_t *mtx = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);
    sem_t *prim_sem = sem_open(SEM_PRIMARY, O_CREAT, 0666, 0);
    sem_t *spec_sems[SPEC_COUNT];
    const char *snames[] = {SEM_SPEC_0, SEM_SPEC_1, SEM_SPEC_2};
    for(int i=0;i<SPEC_COUNT;i++) spec_sems[i]=sem_open(snames[i], O_CREAT, 0666, 0);

    /* Запуск врачей */
    pid_t doc_pids[NUM_DUTY + SPEC_COUNT];
    for(int i=0;i<NUM_DUTY;i++){
        doc_pids[i] = fork();
        if(doc_pids[i]==0){ duty_doctor(i, sh, mtx, prim_sem); exit(0); }
    }
    for(int i=0;i<SPEC_COUNT;i++){
        doc_pids[NUM_DUTY+i] = fork();
        if(doc_pids[NUM_DUTY+i]==0){ specialist(i, sh, mtx, spec_sems[i]); exit(0); }
    }

    /* Генерация пациентов */
    pid_t pat_pids[MAX_PATIENTS];
    for(int i=0;i<total && !g_interrupt;i++){
        sem_wait(mtx);
        sh->patient_spec[i] = rand() % SPEC_COUNT;
        sh->patient_urgent[i] = (rand()%100)<15;
        sh->patient_exam_ms[i] = 50 + rand()%200;
        sh->patient_treat_ms[i] = 100 + rand()%300;
        sh->patient_state[i] = ST_WAIT_PRIMARY;
        if(sh->primary_count < MAX_QUEUE)
            sh->primary_queue[sh->primary_count++] = i;
        sh->total_arrived++;
        sem_post(mtx);

        log_msg("Пациент %d прибыл (%s)\n", i, SPEC_NAMES[sh->patient_spec[i]]);
        sem_post(prim_sem);

        pat_pids[i] = fork();
        if(pat_pids[i]==0){ patient_proc(i, sh, mtx, prim_sem, spec_sems); exit(0); }

        usleep((50 + rand()%150)*1000);
    }

    sem_wait(mtx); sh->all_generated = 1; sem_post(mtx);

    /* Ждём завершения */
    while(!g_interrupt){
        sem_wait(mtx);
        int done = (sh->total_discharged >= total);
        sem_post(mtx);
        if(done) break;
        usleep(50000);
    }

    /* Завершение */
    sem_wait(mtx); sh->shutdown = 1; sem_post(mtx);
    for(int i=0;i<NUM_DUTY;i++) sem_post(prim_sem);
    for(int i=0;i<SPEC_COUNT;i++) sem_post(spec_sems[i]);

    for(int i=0;i<NUM_DUTY+SPEC_COUNT;i++) waitpid(doc_pids[i],NULL,0);
    for(int i=0;i<total;i++) waitpid(pat_pids[i],NULL,0);

    sem_wait(mtx);
    log_msg("\n=== ИТОГО ===\n");
    log_msg("Прибыло: %d, Выписано: %d, Прервано: %d\n",
            sh->total_arrived, sh->total_discharged,
            sh->total_arrived - sh->total_discharged);
    sem_post(mtx);

    munmap(sh, sizeof(SharedState));
    sem_close(mtx); sem_close(prim_sem);
    for(int i=0;i<SPEC_COUNT;i++) sem_close(spec_sems[i]);
    cleanup();
    return 0;
}