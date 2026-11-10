/*
 * hospital_ind_shm.c — Независимые процессы.
 * Именованные семафоры + разделяемая память.
 *
 * Компиляция: gcc -std=c99 -o hospital_ind_shm hospital_ind_shm.c -lrt -lpthread
 * Запуск:
 *   ./hospital_ind_shm coordinator 20 42
 *   ./hospital_ind_shm doctor duty 0
 *   ./hospital_ind_shm doctor spec 1
 *   ./hospital_ind_shm patient 5
 *
 * ИЛИ: ./hospital_ind_shm auto 20 42  (запустит всё сам через fork+exec)
 */
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

#define MAX_PATIENTS 100
#define MAX_QUEUE    50
#define SPEC_COUNT   3
#define SHM_NAME  "/hosp_ind_shm"
#define SEM_MTX   "/hosp_ind_mtx"
#define SEM_PRIM  "/hosp_ind_prm"
#define SEM_S0    "/hosp_ind_s0"
#define SEM_S1    "/hosp_ind_s1"
#define SEM_S2    "/hosp_ind_s2"

static const char *SPEC_NAMES[SPEC_COUNT]={"Стоматолог","Хирург","Терапевт"};
enum {ST_WAIT_PRIMARY=0,ST_IN_PRIMARY,ST_WAIT_SPEC,ST_IN_TREAT,ST_DONE,ST_INT};

typedef struct {
    int primary_queue[MAX_QUEUE]; int primary_count;
    int spec_queues[SPEC_COUNT][MAX_QUEUE]; int spec_counts[SPEC_COUNT];
    int patient_state[MAX_PATIENTS]; int patient_spec[MAX_PATIENTS];
    int patient_exam_ms[MAX_PATIENTS]; int patient_treat_ms[MAX_PATIENTS];
    int total_arrived,total_discharged,total_target;
    int all_generated,shutdown;
} SharedState;

static volatile sig_atomic_t g_int=0;
static void sh(int s){(void)s;g_int=1;}
static int log_fd=-1;
static void log_msg(const char*f,...){
    char b[512];va_list a;va_start(a,f);
    int l=vsnprintf(b,sizeof(b),f,a);va_end(a);
    write(STDOUT_FILENO,b,l);if(log_fd>=0)write(log_fd,b,l);
}

static void cleanup_all(void){
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_MTX);sem_unlink(SEM_PRIM);
    sem_unlink(SEM_S0);sem_unlink(SEM_S1);sem_unlink(SEM_S2);
}

/* Роль: координатор */
static void run_coordinator(int total,unsigned seed){
    signal(SIGINT,sh);
    log_msg("[КООРДИНАТОР] Старт: %d пациентов, seed=%u\n",total,seed);
    srand(seed);

    SharedState *sh;
    int fd=shm_open(SHM_NAME,O_CREAT|O_RDWR,0666);
    ftruncate(fd,sizeof(SharedState));
    sh=mmap(NULL,sizeof(SharedState),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);
    memset(sh,0,sizeof(SharedState));
    sh->total_target=total;

    sem_t *mtx=sem_open(SEM_MTX,O_CREAT,0666,1);
    sem_t *prim=sem_open(SEM_PRIM,O_CREAT,0666,0);
    const char*sn[]={SEM_S0,SEM_S1,SEM_S2};
    sem_t *ss[SPEC_COUNT];
    for(int i=0;i<SPEC_COUNT;i++)ss[i]=sem_open(sn[i],O_CREAT,0666,0);

    /* Генерация пациентов */
    for(int i=0;i<total&&!g_int;i++){
        sem_wait(mtx);
        sh->patient_spec[i]=rand()%SPEC_COUNT;
        sh->patient_exam_ms[i]=50+rand()%200;
        sh->patient_treat_ms[i]=100+rand()%300;
        sh->patient_state[i]=ST_WAIT_PRIMARY;
        if(sh->primary_count<MAX_QUEUE)sh->primary_queue[sh->primary_count++]=i;
        sh->total_arrived++;
        sem_post(mtx);
        log_msg("[КООРД] Пациент %d прибыл (%s)\n",i,SPEC_NAMES[sh->patient_spec[i]]);
        sem_post(prim);
        usleep((30+rand()%100)*1000);
    }
    sem_wait(mtx);sh->all_generated=1;sem_post(mtx);

    while(!g_int){
        sem_wait(mtx);int d=sh->total_discharged>=total;sem_post(mtx);
        if(d)break;usleep(50000);
    }
    sem_wait(mtx);sh->shutdown=1;sem_post(mtx);
    for(int i=0;i<2;i++)sem_post(prim);
    for(int i=0;i<SPEC_COUNT;i++)sem_post(ss[i]);

    log_msg("[КООРД] Итого: прибыло=%d выписано=%d\n",sh->total_arrived,sh->total_discharged);
    munmap(sh,sizeof(SharedState));
    sem_close(mtx);sem_close(prim);
    for(int i=0;i<SPEC_COUNT;i++)sem_close(ss[i]);
    cleanup_all();
}

/* Роль: врач */
static void run_doctor(const char*type,int id){
    signal(SIGINT,sh);
    int fd=shm_open(SHM_NAME,O_RDWR,0666);
    SharedState*sh=mmap(NULL,sizeof(SharedState),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);
    sem_t*mtx=sem_open(SEM_MTX,0);
    sem_t*prim=sem_open(SEM_PRIM,0);
    const char*sn[]={SEM_S0,SEM_S1,SEM_S2};
    sem_t*ss[SPEC_COUNT];for(int i=0;i<SPEC_COUNT;i++)ss[i]=sem_open(sn[i],0);

    if(strcmp(type,"duty")==0){
        for(;;){
            sem_wait(prim);sem_wait(mtx);
            if(sh->shutdown){sem_post(mtx);break;}
            if(sh->primary_count<=0){sem_post(mtx);continue;}
            int p=sh->primary_queue[0];
            for(int i=0;i<sh->primary_count-1;i++)sh->primary_queue[i]=sh->primary_queue[i+1];
            sh->primary_count--;sh->patient_state[p]=ST_IN_PRIMARY;
            sem_post(mtx);
            log_msg("[ДВ-%d] Осмотр пациента %d\n",id,p);
            usleep(sh->patient_exam_ms[p]*1000);
            sem_wait(mtx);
            int sp=sh->patient_spec[p];sh->patient_state[p]=ST_WAIT_SPEC;
            if(sh->spec_counts[sp]<MAX_QUEUE)sh->spec_queues[sp][sh->spec_counts[sp]++]=p;
            sem_post(mtx);
            log_msg("[ДВ-%d] Пациент %d -> %s\n",id,p,SPEC_NAMES[sp]);
        }
    } else {
        for(;;){
            sem_wait(ss[id]);sem_wait(mtx);
            if(sh->shutdown){sem_post(mtx);break;}
            if(sh->spec_counts[id]<=0){sem_post(mtx);continue;}
            int p=sh->spec_queues[id][0];
            for(int i=0;i<sh->spec_counts[id]-1;i++)sh->spec_queues[id][i]=sh->spec_queues[id][i+1];
            sh->spec_counts[id]--;sh->patient_state[p]=ST_IN_TREAT;
            sem_post(mtx);
            log_msg("[%s] Лечение пациента %d\n",SPEC_NAMES[id],p);
            usleep(sh->patient_treat_ms[p]*1000);
            sem_wait(mtx);
            sh->patient_state[p]=ST_DONE;sh->total_discharged++;
            sem_post(mtx);
            log_msg("[%s] Пациент %d выписан\n",SPEC_NAMES[id],p);
        }
    }
    munmap(sh,sizeof(SharedState));
    sem_close(mtx);sem_close(prim);
    for(int i=0;i<SPEC_COUNT;i++)sem_close(ss[i]);
}

/* Роль: пациент */
static void run_patient(int pid){
    signal(SIGINT,sh);
    int fd=shm_open(SHM_NAME,O_RDWR,0666);
    SharedState*sh=mmap(NULL,sizeof(SharedState),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);
    sem_t*mtx=sem_open(SEM_MTX,0);

    while(!g_int){
        sem_wait(mtx);int s=sh->patient_state[pid];int sd=sh->shutdown;sem_post(mtx);
        if(s==ST_DONE||s==ST_INT||sd)break;
        usleep(5000);
    }
    munmap(sh,sizeof(SharedState));sem_close(mtx);
}

int main(int argc,char*argv[]){
    log_fd=open("hospital_ind_shm.log",O_WRONLY|O_CREAT|O_APPEND,0644);

    if(argc<2){printf("Usage: %s coordinator|doctor|patient|auto ...\n",argv[0]);return 1;}

    if(strcmp(argv[1],"coordinator")==0){
        int t=(argc>2)?atoi(argv[2]):15;unsigned s=(argc>3)?atoi(argv[3]):42;
        run_coordinator(t,s);
    }
    else if(strcmp(argv[1],"doctor")==0){
        run_doctor(argv[2],(argc>3)?atoi(argv[3]):0);
    }
    else if(strcmp(argv[1],"patient")==0){
        run_patient((argc>2)?atoi(argv[2]):0);
    }
    else if(strcmp(argv[1],"auto")==0){
        /* Автозапуск: координатор + врачи + пациенты через fork+exec */
        int total=(argc>2)?atoi(argv[2]):15;
        unsigned seed=(argc>3)?atoi(argv[3]):42;
        pid_t procs[20];int n=0;

        /* Сначала создаём ресурсы */
        shm_unlink(SHM_NAME);sem_unlink(SEM_MTX);sem_unlink(SEM_PRIM);
        sem_unlink(SEM_S0);sem_unlink(SEM_S1);sem_unlink(SEM_S2);

        /* Координатор */
        procs[n++]=fork();
        if(!procs[n-1]){execl(argv[0],argv[0],"coordinator",
            (char[16]){0},(char[16]){0},NULL);exit(1);}
        usleep(100000); /* даём координатору создать ресурсы */

        /* Врачи */
        procs[n++]=fork();
        if(!procs[n-1]){execl(argv[0],argv[0],"doctor","duty","0",NULL);exit(1);}
        procs[n++]=fork();
        if(!procs[n-1]){execl(argv[0],argv[0],"doctor","duty","1",NULL);exit(1);}
        for(int i=0;i<SPEC_COUNT;i++){
            procs[n++]=fork();
            if(!procs[n-1]){
                char id[8];snprintf(id,sizeof(id),"%d",i);
                execl(argv[0],argv[0],"doctor","spec",id,NULL);exit(1);
            }
        }

        /* Пациенты */
        for(int i=0;i<total;i++){
            procs[n++]=fork();
            if(!procs[n-1]){
                char id[8];snprintf(id,sizeof(id),"%d",i);
                execl(argv[0],argv[0],"patient",id,NULL);exit(1);
            }
        }

        for(int i=0;i<n;i++)waitpid(procs[i],NULL,0);
    }

    if(log_fd>=0)close(log_fd);
    return 0;
}
