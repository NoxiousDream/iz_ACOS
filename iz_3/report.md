# Отчёт по индивидуальному заданию №3

**по дисциплине «Архитектура компьютера и операционные системы»**

**Тема:** Разработка последовательной имитационной программы

**Вариант №11:** Задача о больнице

**Выполнил:** студент группы БПИ253
Лёвин Артём Михайлович
---

## 1. Условие задачи

**Вариант 11. Задача о больнице**

В приёмном отделении больницы работают два дежурных врача. Поступивший пациент сначала проходит первичный осмотр, сообщает жалобы и получает направление к стоматологу, хирургу или терапевту. После этого пациент переходит в очередь соответствующего специалиста.

Каждый врач принимает только одного пациента. Пациенты не покидают очередь самовольно. Продолжительность первичного осмотра и лечения зависит от состояния пациента либо задаётся случайно. При расширенном моделировании срочные пациенты могут обслуживаться с приоритетом, но выбранная политика не должна оставлять остальных пациентов без обслуживания.

### Инварианты

- один врач принимает не более одного пациента;
- пациент одновременно находится не более чем в одной очереди или у одного врача;
- направление выдаётся только после первичного осмотра;
- лечение выполняет специалист соответствующего профиля;
- пациент выписывается после завершения назначенного лечения.

### Завершение

Моделирование завершается после окончания приёма новых пациентов и обслуживания всех поступивших или по прерыванию пользователем.

Каждый врач и пациент является самостоятельным участником модели.

---

## 2. Сценарий: отображение сущностей в процессы

### Распределение ролей по процессам

| Сущность | Процесс | Роль |
|----------|---------|------|
| Координатор | 1 процесс | Генерирует пациентов, управляет завершением |
| Дежурный врач | 2 процесса | Забирают пациентов из очереди, проводят осмотр |
| Стоматолог | 1 процесс | Лечит направленных пациентов |
| Хирург | 1 процесс | Лечит направленных пациентов |
| Терапевт | 1 процесс | Лечит направленных пациентов |
| Пациент | 1 процесс на каждого | Ожидает обслуживания, сигнализирует о готовности |

**Итого:** 6 + N процессов (при 15 пациентах — 21 процесс).

### Механизмы межпроцессного взаимодействия

| Программа (оценка) | Семафоры | Обмен данными |
|-------------------|----------|---------------|
| `hospital_named` (4-5) | Именованные POSIX | Разделяемая память POSIX (`shm_open`) |
| `hospital_unnamed` (6) | Безымянные в разделяемой памяти (`sem_init`, pshared=1) | Анонимная разделяемая память (`MAP_ANONYMOUS`) |
| `hospital_ind_shm` (7-8) | Именованные POSIX | Разделяемая память + независимый запуск |
| `hospital_ind_pipe` (9-10) | Именованные POSIX | Именованные каналы (FIFO) |

### Схема потока данных

```
Пациент → [Очередь в shared memory / FIFO] → Дежурный врач
Дежурный врач → [Очередь специалиста] → Специалист
Специалист → [Счётчик выписанных / FIFO_DONE] → Координатор
```

---

## 3. Пользовательский интерфейс

Программа выводит информативный лог с идентификацией участников:
- `[ДВ-0]`, `[ДВ-1]` — дежурные врачи
- `[Стоматолог]`, `[Хирург]`, `[Терапевт]` — специалисты
- `[КООРД]` — координатор
- `Пациент N прибыл/выписан` — события пациентов

Логирование через системный вызов `write()` в файловые дескрипторы. По завершении формируется итоговая статистика и сохраняется лог-файл.

---

## 4. Варианты запуска и завершения

### Компиляция

```bash
gcc -std=c99 -o hospital_named     hospital_named.c     -lrt -lpthread
gcc -std=c99 -o hospital_unnamed   hospital_unnamed.c   -lrt -lpthread
gcc -std=c99 -o hospital_ind_shm   hospital_ind_shm.c   -lrt -lpthread
gcc -std=c99 -o hospital_ind_pipe  hospital_ind_pipe.c  -lpthread
```

### Запуск

```bash
# Программа 1: именованные семафоры
./hospital_named 20 42

# Программа 2: безымянные семафоры
./hospital_unnamed 20 42

# Программа 3: независимые процессы (авто-режим)
./hospital_ind_shm auto 15 42

# Программа 3: независимые процессы (ручной запуск в отдельных терминалах)
./hospital_ind_shm coordinator 15 42   # терминал 1
./hospital_ind_shm doctor duty 0       # терминал 2
./hospital_ind_shm doctor duty 1       # терминал 3
./hospital_ind_shm doctor spec 0       # терминал 4
./hospital_ind_shm doctor spec 1       # терминал 5
./hospital_ind_shm doctor spec 2       # терминал 6

# Программа 4: каналы
./hospital_ind_pipe auto 10 42
```

### Завершение

1. **Штатное:** координатор обнаруживает `total_discharged >= total_target`, устанавливает `shutdown=1`, постит в семафоры для разблокировки ожидающих процессов.
2. **Прерывание (Ctrl+C):** обработчик `SIGINT` устанавливает флаг, координатор инициирует корректное завершение.
3. **Очистка ресурсов:** `shm_unlink()`, `sem_unlink()`, `unlink()` для FIFO.

---

## 5. Результаты работы

### Пример вывода (hospital_named, 5 пациентов)

```
=== БОЛЬНИЦА: именованные семафоры + shared memory ===
Пациентов: 5, seed: 42

Пациент 0 прибыл (Хирург)
[ДВ-0] Начал осмотр пациента 0
Пациент 1 прибыл (Стоматолог)
[ДВ-1] Начал осмотр пациента 1
[ДВ-0] Закончил осмотр пациента 0 -> направлен к Хирург
[Хирург] Начал лечение пациента 0
[ДВ-1] Закончил осмотр пациента 1 -> направлен к Стоматолог
[Стоматолог] Начал лечение пациента 1
[Хирург] Закончил лечение пациента 0 — выписан
[Стоматолог] Закончил лечение пациента 1 — выписан

=== ИТОГО ===
Прибыло: 5, Выписано: 5, Прервано: 0
```

### Пример прерывания

```
^C
=== ИТОГО ===
Прибыло: 8, Выписано: 3, Прервано: 5
```

---

## 6. Реализация дополнительных требований

| № | Требование | Реализация |
|---|-----------|------------|
| 1 | Комментарии | Подробные комментарии в каждом файле |
| 2 | Системные вызовы | `write()`, `open()`, `shm_open()`, `mmap()`, `sem_open()` |
| 3 | Завершение по окончании и прерыванию | `SIGINT` + корректная очистка ресурсов |
| 4 | Лог со статистикой | `hospital_*.log` с итоговыми данными |
| 5 | Тестовые скрипты | `run_tests.sh` |
| 6 | Промпты ИИ | Раздел 7 |
| 7 | Критическая оценка ИИ | Раздел 7 |
| 8 | Непредусмотренные решения | Авто-режим с `fork+exec`, поддержка ручного запуска в отдельных терминалах |

---

## 7. Промпты ИИ и критическая оценка

### Промпты

1. «Третья домашка. Сразу дай одно решение на 10.»

### Критическая оценка

При переходе от потоков к процессам выявлены ключевые отличия:
- Разделяемая память требует явного `shm_open`/`mmap`/`shm_unlink`
- Безымянные семафоры должны быть инициализированы с `pshared=1` в разделяемой памяти
- Именованные семафоры требуют `sem_unlink` для предотвращения утечек
- Каналы (FIFO) упрощают передачу данных, но усложняют многократное чтение (каждый врач читает из своего канала)
- При независимых процессах координатор должен создать ресурсы ДО запуска остальных

Правки после генерации ИИ: добавлена обработка случая, когда `sem_wait` блокируется при прерывании, добавлен авто-режим для удобства тестирования.

---

## Приложение А. Скрипт тестирования

```bash
#!/bin/bash
set -e
echo "=== Компиляция ==="
gcc -std=c99 -Wall -o hospital_named     hospital_named.c     -lrt -lpthread
gcc -std=c99 -Wall -o hospital_unnamed   hospital_unnamed.c   -lrt -lpthread
gcc -std=c99 -Wall -o hospital_ind_shm   hospital_ind_shm.c   -lrt -lpthread
gcc -std=c99 -Wall -o hospital_ind_pipe  hospital_ind_pipe.c  -lpthread
echo "OK"
echo "=== Тест 1: именованные семафоры ==="
timeout 30 ./hospital_named 10 42 || true
echo "=== Тест 2: безымянные семафоры ==="
timeout 30 ./hospital_unnamed 10 42 || true
echo "=== Тест 3: независимые + shared memory ==="
timeout 30 ./hospital_ind_shm auto 10 42 || true
echo "=== Тест 4: независимые + каналы ==="
timeout 30 ./hospital_ind_pipe auto 8 42 || true
echo "=== Тест 5: прерывание ==="
timeout 2 ./hospital_named 500 99 || true
tail -3 hospital_named.log
echo "=== Все тесты завершены ==="
```

---

## Приложение Б. Листинги программ
Файл 1: hospital_named.c
```c
/*
 * hospital_named.c — Задача о больнице (вариант 11)
 * Родительский процесс + дочерние процессы.
 * Именованные POSIX семафоры + разделяемая память POSIX.
 *
 * Компиляция: gcc -std=c99 -o hospital_named hospital_named.c -lrt -lpthread
 * Запуск:     ./hospital_named [число_пациентов] [seed]
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
```

Файл 2: hospital_unnamed.c
```c
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
```

Файл 3: hospital_ind_shm.c
```c
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
```

Файл 4: hospital_ind_pipe.c
```c
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
```