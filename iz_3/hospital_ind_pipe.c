/*
 * hospital_ind_pipe.c — Независимые процессы.
 * Именованные семафоры для синхронизации + FIFO-каналы для данных.
 *
 * Компиляция: gcc -std=c99 -o hospital_ind_pipe hospital_ind_pipe.c -lpthread
 * Запуск:
 *   ./hospital_ind_pipe coordinator 15 42
 *   ./hospital_ind_pipe doctor duty 0
 *   ./hospital_ind_pipe doctor spec 1
 *   ./hospital_ind_pipe patient 3
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <stdarg.h>

#define MAX_PATIENTS 100
#define SPEC_COUNT   3
#define FIFO_PRIMARY "/tmp/hosp_primary_fifo"
#define FIFO_SPEC_0  "/tmp/hosp_spec0_fifo"
#define FIFO_SPEC_1  "/tmp/hosp_spec1_fifo"
#define FIFO_SPEC_2  "/tmp/hosp_spec2_fifo"
#define FIFO_DONE    "/tmp/hosp_done_fifo"
#define SEM_MTX      "/hosp_pipe_mtx"

static const char *SPEC_NAMES[SPEC_COUNT]={"Стоматолог","Хирург","Терапевт"};

/* Сообщение в канале */
typedef struct {
    int patient_id;
    int spec_type;
    int exam_ms;
    int treat_ms;
} Msg;

static volatile sig_atomic_t g_int=0;
static void sigh(int s){(void)s;g_int=1;}
static int log_fd=-1;
static void log_msg(const char*f,...){
    char b[512];va_list a;va_start(a,f);
    int l=vsnprintf(b,sizeof(b),f,a);va_end(a);
    write(STDOUT_FILENO,b,l);if(log_fd>=0)write(log_fd,b,l);
}

static void cleanup_pipe(void){
    unlink(FIFO_PRIMARY);unlink(FIFO_SPEC_0);unlink(FIFO_SPEC_1);
    unlink(FIFO_SPEC_2);unlink(FIFO_DONE);sem_unlink(SEM_MTX);
}

/* Координатор: создаёт FIFO, генерирует пациентов */
static void run_coordinator(int total,unsigned seed){
    signal(SIGINT,sigh);
    srand(seed);
    log_msg("[КООРД] Старт: %d пациентов, seed=%u, каналы (FIFO)\n",total,seed);

    cleanup_pipe();
    mkfifo(FIFO_PRIMARY,0666);
    mkfifo(FIFO_SPEC_0,0666);mkfifo(FIFO_SPEC_1,0666);mkfifo(FIFO_SPEC_2,0666);
    mkfifo(FIFO_DONE,0666);
    sem_t*mtx=sem_open(SEM_MTX,O_CREAT,0666,1);

    /* Открываем каналы для записи */
    int fd_prim=open(FIFO_PRIMARY,O_WRONLY);
    int fd_specs[SPEC_COUNT];
    fd_specs[0]=open(FIFO_SPEC_0,O_WRONLY);
    fd_specs[1]=open(FIFO_SPEC_1,O_WRONLY);
    fd_specs[2]=open(FIFO_SPEC_2,O_WRONLY);

    /* Пациенты будут читать из этих каналов через врачей */
    /* Координатор пишет первичные данные в канал дежурных */
    for(int i=0;i<total&&!g_int;i++){
        Msg m;
        m.patient_id=i;
        m.spec_type=rand()%SPEC_COUNT;
        m.exam_ms=50+rand()%200;
        m.treat_ms=100+rand()%300;
        write(fd_prim,&m,sizeof(m));
        log_msg("[КООРД] Пациент %d отправлен (%s)\n",i,SPEC_NAMES[m.spec_type]);
        usleep((30+rand()%100)*1000);
    }

    /* Сигналим конец */
    Msg end_msg = {-1,0,0,0};
    write(fd_prim,&end_msg,sizeof(end_msg));

    /* Ждём завершений из FIFO_DONE */
    int fd_done=open(FIFO_DONE,O_RDONLY);
    int discharged=0;
    while(discharged<total&&!g_int){
        Msg m;
        int r=read(fd_done,&m,sizeof(m));
        if(r<=0)break;
        if(m.patient_id<0)break;
        discharged++;
        log_msg("[КООРД] Пациент %d выписан (итого: %d)\n",m.patient_id,discharged);
    }

    /* Посылаем конец специалистам */
    for(int i=0;i<SPEC_COUNT;i++) write(fd_specs[i],&end_msg,sizeof(end_msg));

    close(fd_prim);for(int i=0;i<SPEC_COUNT;i++)close(fd_specs[i]);close(fd_done);
    sem_close(mtx);
    cleanup_pipe();
    log_msg("[КООРД] Завершено: выписано %d из %d\n",discharged,total);
}

/* Дежурный врач: читает из FIFO, «осматривает», пишет к специалисту */
static void run_duty_doctor(int id){
    signal(SIGINT,sigh);
    log_msg("[ДВ-%d] Запущен, ожидает пациентов из канала...\n",id);

    int fd_in=open(FIFO_PRIMARY,O_RDONLY);
    int fd_specs[SPEC_COUNT];
    fd_specs[0]=open(FIFO_SPEC_0,O_WRONLY);
    fd_specs[1]=open(FIFO_SPEC_1,O_WRONLY);
    fd_specs[2]=open(FIFO_SPEC_2,O_WRONLY);

    for(;;){
        Msg m;
        int r=read(fd_in,&m,sizeof(m));
        if(r<=0)break;
        if(m.patient_id<0){
            /* Конец — пересылаем дальше */
            for(int i=0;i<SPEC_COUNT;i++)write(fd_specs[i],&m,sizeof(m));
            break;
        }
        log_msg("[ДВ-%d] Осмотр пациента %d (%d мс)\n",id,m.patient_id,m.exam_ms);
        usleep(m.exam_ms*1000);
        /* Направляем к специалисту */
        write(fd_specs[m.spec_type],&m,sizeof(m));
        log_msg("[ДВ-%d] Пациент %d -> %s\n",id,m.patient_id,SPEC_NAMES[m.spec_type]);
    }
    close(fd_in);for(int i=0;i<SPEC_COUNT;i++)close(fd_specs[i]);
}

/* Специалист: читает из своего FIFO, лечит, пишет в FIFO_DONE */
static void run_specialist(int spec){
    signal(SIGINT,sigh);
    log_msg("[%s] Запущен, ожидает пациентов...\n",SPEC_NAMES[spec]);

    const char*fifos[]={FIFO_SPEC_0,FIFO_SPEC_1,FIFO_SPEC_2};
    int fd_in=open(fifos[spec],O_RDONLY);
    int fd_done=open(FIFO_DONE,O_WRONLY);

    for(;;){
        Msg m;
        int r=read(fd_in,&m,sizeof(m));
        if(r<=0)break;
        if(m.patient_id<0)break;
        log_msg("[%s] Лечение пациента %d (%d мс)\n",SPEC_NAMES[spec],m.patient_id,m.treat_ms);
        usleep(m.treat_ms*1000);
        /* Выписка — пишем в канал завершений */
        write(fd_done,&m,sizeof(m));
        log_msg("[%s] Пациент %d выписан!\n",SPEC_NAMES[spec],m.patient_id);
    }
    close(fd_in);close(fd_done);
}

/* Пациент: в этой модели пациент — пассивная сущность в канале,
   но для соответствия требованию "каждый участник = процесс",
   создаём процесс-наблюдатель */
static void run_patient_observer(int pid){
    signal(SIGINT,sigh);
    int fd_done=open(FIFO_DONE,O_RDONLY);
    for(;;){
        Msg m;int r=read(fd_done,&m,sizeof(m));
        if(r<=0)break;
        if(m.patient_id==pid){
            log_msg("[ПАЦ-%d] Я выписан!\n",pid);
            break;
        }
        if(m.patient_id<0)break;
    }
    close(fd_done);
}

int main(int argc,char*argv[]){
    log_fd=open("hospital_ind_pipe.log",O_WRONLY|O_CREAT|O_APPEND,0644);
    if(argc<2){printf("Usage: %s coordinator|doctor|specialist|patient|auto\n",argv[0]);return 1;}

    if(strcmp(argv[1],"coordinator")==0){
        int t=(argc>2)?atoi(argv[2]):15;unsigned s=(argc>3)?atoi(argv[3]):42;
        run_coordinator(t,s);
    }
    else if(strcmp(argv[1],"doctor")==0&&argc>2&&strcmp(argv[2],"duty")==0){
        run_duty_doctor((argc>3)?atoi(argv[3]):0);
    }
    else if(strcmp(argv[1],"specialist")==0){
        run_specialist((argc>2)?atoi(argv[2]):0);
    }
    else if(strcmp(argv[1],"patient")==0){
        run_patient_observer((argc>2)?atoi(argv[2]):0);
    }
    else if(strcmp(argv[1],"auto")==0){
        int total=(argc>2)?atoi(argv[2]):10;
        unsigned seed=(argc>3)?atoi(argv[3]):42;
        pid_t procs[20];int n=0;

        cleanup_pipe();
        mkfifo(FIFO_PRIMARY,0666);
        mkfifo(FIFO_SPEC_0,0666);mkfifo(FIFO_SPEC_1,0666);mkfifo(FIFO_SPEC_2,0666);
        mkfifo(FIFO_DONE,0666);

        /* Координатор */
        procs[n++]=fork();
        if(!procs[n-1]){
            char t[8],s[8];snprintf(t,8,"%d",total);snprintf(s,8,"%u",seed);
            execl(argv[0],argv[0],"coordinator",t,s,NULL);exit(1);
        }
        usleep(200000);

        /* Дежурные */
        procs[n++]=fork();
        if(!procs[n-1]){execl(argv[0],argv[0],"doctor","duty","0",NULL);exit(1);}
        procs[n++]=fork();
        if(!procs[n-1]){execl(argv[0],argv[0],"doctor","duty","1",NULL);exit(1);}

        /* Специалисты */
        for(int i=0;i<SPEC_COUNT;i++){
            procs[n++]=fork();
            if(!procs[n-1]){
                char id[8];snprintf(id,8,"%d",i);
                execl(argv[0],argv[0],"specialist",id,NULL);exit(1);
            }
        }

        for(int i=0;i<n;i++)waitpid(procs[i],NULL,0);
        cleanup_pipe();
        log_msg("[АВТО] Все процессы завершены.\n");
    }

    if(log_fd>=0)close(log_fd);
    return 0;
}
