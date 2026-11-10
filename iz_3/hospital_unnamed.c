/*
 * hospital_unnamed.c — Задача о больнице (вариант 11)
 * Безымянные семафоры В разделяемой памяти + разделяемая память.
 *
 * Компиляция: gcc -std=c99 -o hospital_unnamed hospital_unnamed.c -lrt -lpthread
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <stdarg.h>

#define MAX_PATIENTS 100
#define MAX_QUEUE    50
#define SPEC_COUNT   3
#define NUM_DUTY     2

#ifdef __APPLE__
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

static const char *SPEC_NAMES[SPEC_COUNT] = {"Стоматолог","Хирург","Терапевт"};
enum { ST_WAIT_PRIMARY=0, ST_IN_PRIMARY, ST_WAIT_SPEC, ST_IN_TREAT, ST_DONE, ST_INT };

typedef struct {
    /* Семафоры прямо в структуре разделяемой памяти */
    sem_t mutex;
    sem_t primary_sem;
    sem_t spec_sems[SPEC_COUNT];

    int primary_queue[MAX_QUEUE];
    int primary_count;
    int spec_queues[SPEC_COUNT][MAX_QUEUE];
    int spec_counts[SPEC_COUNT];
    int patient_state[MAX_PATIENTS];
    int patient_spec[MAX_PATIENTS];
    int patient_exam_ms[MAX_PATIENTS];
    int patient_treat_ms[MAX_PATIENTS];
    int total_arrived, total_discharged;
    int all_generated, shutdown;
} SharedState;

static volatile sig_atomic_t g_int = 0;
static void sigh(int s){(void)s; g_int=1;}
static int log_fd = -1;
static void log_msg(const char *fmt,...){
    char buf[512]; va_list ap; va_start(ap,fmt);
    int len=vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    write(STDOUT_FILENO,buf,len);
    if(log_fd>=0) write(log_fd,buf,len);
}

static void duty_doc(int id, SharedState *sh){
    for(;;){
        sem_wait(&sh->primary_sem);
        sem_wait(&sh->mutex);
        if(sh->shutdown){sem_post(&sh->mutex);break;}
        if(sh->primary_count<=0){sem_post(&sh->mutex);continue;}
        int pid=sh->primary_queue[0];
        for(int i=0;i<sh->primary_count-1;i++) sh->primary_queue[i]=sh->primary_queue[i+1];
        sh->primary_count--;
        sh->patient_state[pid]=ST_IN_PRIMARY;
        sem_post(&sh->mutex);
        log_msg("[ДВ-%d] Осматривает пациента %d\n",id,pid);
        usleep(sh->patient_exam_ms[pid]*1000);
        sem_wait(&sh->mutex);
        int spec=sh->patient_spec[pid];
        sh->patient_state[pid]=ST_WAIT_SPEC;
        if(sh->spec_counts[spec]<MAX_QUEUE)
            sh->spec_queues[spec][sh->spec_counts[spec]++]=pid;
        sem_post(&sh->mutex);
        log_msg("[ДВ-%d] Пациент %d -> %s\n",id,pid,SPEC_NAMES[spec]);
    }
}

static void spec_doc(int spec, SharedState *sh){
    for(;;){
        sem_wait(&sh->spec_sems[spec]);
        sem_wait(&sh->mutex);
        if(sh->shutdown){sem_post(&sh->mutex);break;}
        if(sh->spec_counts[spec]<=0){sem_post(&sh->mutex);continue;}
        int pid=sh->spec_queues[spec][0];
        for(int i=0;i<sh->spec_counts[spec]-1;i++)
            sh->spec_queues[spec][i]=sh->spec_queues[spec][i+1];
        sh->spec_counts[spec]--;
        sh->patient_state[pid]=ST_IN_TREAT;
        sem_post(&sh->mutex);
        log_msg("[%s] Лечит пациента %d\n",SPEC_NAMES[spec],pid);
        usleep(sh->patient_treat_ms[pid]*1000);
        sem_wait(&sh->mutex);
        sh->patient_state[pid]=ST_DONE;
        sh->total_discharged++;
        sem_post(&sh->mutex);
        log_msg("[%s] Пациент %d выписан\n",SPEC_NAMES[spec],pid);
    }
}

static void pat_proc(int pid, SharedState *sh){
    for(;;){sem_wait(&sh->mutex);int s=sh->patient_state[pid];int shu=sh->shutdown;sem_post(&sh->mutex);
        if(s!=ST_WAIT_PRIMARY||shu)break; usleep(5000);}
    for(;;){sem_wait(&sh->mutex);int s=sh->patient_state[pid];int shu=sh->shutdown;sem_post(&sh->mutex);
        if(s!=ST_IN_PRIMARY||shu)break; usleep(5000);}
    sem_wait(&sh->mutex);
    if(sh->shutdown){sh->patient_state[pid]=ST_INT;sem_post(&sh->mutex);return;}
    sem_post(&sh->mutex);
    sem_post(&sh->spec_sems[sh->patient_spec[pid]]);
    for(;;){sem_wait(&sh->mutex);int s=sh->patient_state[pid];int shu=sh->shutdown;sem_post(&sh->mutex);
        if(s!=ST_WAIT_SPEC||shu)break; usleep(5000);}
    for(;;){sem_wait(&sh->mutex);int s=sh->patient_state[pid];int shu=sh->shutdown;sem_post(&sh->mutex);
        if(s!=ST_IN_TREAT||shu)break; usleep(5000);}
}

int main(int argc,char*argv[]){
    int total=(argc>1)?atoi(argv[1]):15;
    unsigned seed=(argc>2)?atoi(argv[2]):42;
    if(total>MAX_PATIENTS)total=MAX_PATIENTS;
    srand(seed); signal(SIGINT,sigh);
    log_fd=open("hospital_unnamed.log",O_WRONLY|O_CREAT|O_TRUNC,0644);
    log_msg("=== БОЛЬНИЦА: безымянные семафоры в shared memory ===\n");

    /* Анонимная разделяемая память (для родитель-потомки) */
    SharedState *sh = mmap(NULL,sizeof(SharedState),PROT_READ|PROT_WRITE,
                           MAP_SHARED|MAP_ANONYMOUS,-1,0);
    memset(sh,0,sizeof(SharedState));

    /* Безымянные семафоры ИНИЦИАЛИЗИРУЮТСЯ В РАЗДЕЛЯЕМОЙ ПАМЯТИ */
    sem_init(&sh->mutex, 1, 1);       /* pshared=1 — виден потомкам */
    sem_init(&sh->primary_sem, 1, 0);
    for(int i=0;i<SPEC_COUNT;i++) sem_init(&sh->spec_sems[i], 1, 0);

    pid_t docs[NUM_DUTY+SPEC_COUNT];
    for(int i=0;i<NUM_DUTY;i++){docs[i]=fork();if(!docs[i]){duty_doc(i,sh);exit(0);}}
    for(int i=0;i<SPEC_COUNT;i++){docs[NUM_DUTY+i]=fork();if(!docs[NUM_DUTY+i]){spec_doc(i,sh);exit(0);}}

    pid_t pats[MAX_PATIENTS];
    for(int i=0;i<total&&!g_int;i++){
        sem_wait(&sh->mutex);
        sh->patient_spec[i]=rand()%SPEC_COUNT;
        sh->patient_exam_ms[i]=50+rand()%200;
        sh->patient_treat_ms[i]=100+rand()%300;
        sh->patient_state[i]=ST_WAIT_PRIMARY;
        if(sh->primary_count<MAX_QUEUE) sh->primary_queue[sh->primary_count++]=i;
        sh->total_arrived++;
        sem_post(&sh->mutex);
        log_msg("Пациент %d прибыл\n",i);
        sem_post(&sh->primary_sem);
        pats[i]=fork();
        if(!pats[i]){pat_proc(i,sh);exit(0);}
        usleep((50+rand()%150)*1000);
    }
    sem_wait(&sh->mutex);sh->all_generated=1;sem_post(&sh->mutex);

    while(!g_int){
        sem_wait(&sh->mutex);int d=sh->total_discharged>=total;sem_post(&sh->mutex);
        if(d)break; usleep(50000);
    }

    sem_wait(&sh->mutex);sh->shutdown=1;sem_post(&sh->mutex);
    for(int i=0;i<NUM_DUTY;i++)sem_post(&sh->primary_sem);
    for(int i=0;i<SPEC_COUNT;i++)sem_post(&sh->spec_sems[i]);

    for(int i=0;i<NUM_DUTY+SPEC_COUNT;i++)waitpid(docs[i],NULL,0);
    for(int i=0;i<total;i++)waitpid(pats[i],NULL,0);

    log_msg("\n=== ИТОГО ===\nПрибыло: %d, Выписано: %d\n",sh->total_arrived,sh->total_discharged);

    sem_destroy(&sh->mutex);
    sem_destroy(&sh->primary_sem);
    for(int i=0;i<SPEC_COUNT;i++) sem_destroy(&sh->spec_sems[i]);
    munmap(sh,sizeof(SharedState));
    if(log_fd>=0)close(log_fd);
    return 0;
}
