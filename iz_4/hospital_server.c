/*
 * hospital_server.c — сервер больницы (вариант 11).
 *
 * Хранит ВСЁ состояние модели: очереди, состояния пациентов,
 * занятость врачей, статистику. Обеспечивает инварианты задачи.
 * Каждый подключённый клиент обслуживается отдельным потоком.
 *
 * Компиляция: gcc -std=c99 -Wall -o hospital_server hospital_server.c -lpthread
 * Запуск:     ./hospital_server --port 5555 --patients 12 --duty 2
 */
#include "hospital_proto.h"
#include <sys/select.h>

#define MAX_PATIENTS   512
#define MAX_MONITORS   32
#define MAX_DUTY       8
#define MAX_PER_SPEC   4
#define MAX_SPEC_SLOTS (SPEC_COUNT * MAX_PER_SPEC)

/* ---------- состояния пациента (конечный автомат) ---------- */
typedef enum {
    P_FREE = 0,     /* слот не занят */
    P_WAIT_PRIM,    /* в очереди к дежурному */
    P_IN_EXAM,      /* на первичном осмотре */
    P_WAIT_SPEC,    /* в очереди к специалисту */
    P_IN_TREAT,     /* на лечении */
    P_DONE,         /* выписан */
    P_ABORT         /* клиент отключился до выписки */
} PState;

typedef struct {
    int used;                    /* слот занят зарегистрированным пациентом */
    int fd;                      /* сокет клиента-пациента */
    int urgent, exam_ms, treat_ms, spec;
    PState st;
    pthread_mutex_t send_mtx;    /* защита записи в сокет пациента */
} Patient;

/* Слот врача: kind=0 дежурный, kind=1 специалист */
typedef struct {
    int kind, type, num;         /* тип профиля и номер внутри группы */
    int online;                  /* подключён ли клиент-врач */
    int fd;
    int busy_pid;                /* индекс обслуживаемого пациента или -1 */
} DocSlot;

typedef struct {
    int used, fd;
} Monitor;

/* ---------- глобальное состояние (защищено state_mtx) ---------- */
static Patient  patients[MAX_PATIENTS];
static DocSlot  duty[MAX_DUTY];
static DocSlot  spec[MAX_SPEC_SLOTS];
static Monitor  monitors[MAX_MONITORS];

static int q_prim[MAX_PATIENTS],       q_prim_n = 0;
static int q_spec[SPEC_COUNT][MAX_PATIENTS], q_spec_n[SPEC_COUNT] = {0};

static pthread_mutex_t state_mtx = PTHREAD_MUTEX_INITIALIZER;

static int duty_count   = 2;
static int per_spec     = 1;
static int target       = 12;         /* целевое число пациентов */
static unsigned rng_seed = 42;

static int arrived = 0, discharged = 0, aborted = 0;
static volatile sig_atomic_t running = 1;
static int listen_fd = -1, log_fd = -1;
static double t0;

/* ==================== ОЧЕРЕДИ ==================== */

/* Постановка в первичную очередь. Срочные — в начало (приоритет). */
static void q_prim_push(int idx, int urgent) {
    if (q_prim_n >= MAX_PATIENTS) return;
    if (urgent) {
        for (int i = q_prim_n; i > 0; --i) q_prim[i] = q_prim[i - 1];
        q_prim[0] = idx;
    } else {
        q_prim[q_prim_n] = idx;
    }
    q_prim_n++;
}

/* Возврат пациента в НАЧАЛО очереди (при отключении врача) */
static void q_prim_front(int idx) {
    if (q_prim_n >= MAX_PATIENTS) return;
    for (int i = q_prim_n; i > 0; --i) q_prim[i] = q_prim[i - 1];
    q_prim[0] = idx; q_prim_n++;
}

static int q_prim_pop(void) {
    if (q_prim_n == 0) return -1;
    int v = q_prim[0];
    for (int i = 0; i < q_prim_n - 1; ++i) q_prim[i] = q_prim[i + 1];
    q_prim_n--; return v;
}

static void q_prim_remove(int idx) {
    for (int i = 0; i < q_prim_n; ++i)
        if (q_prim[i] == idx) {
            for (int j = i; j < q_prim_n - 1; ++j) q_prim[j] = q_prim[j + 1];
            q_prim_n--; return;
        }
}

static void q_spec_push(int s, int idx) {
    if (q_spec_n[s] >= MAX_PATIENTS) return;
    q_spec[s][q_spec_n[s]++] = idx;
}

static void q_spec_front(int s, int idx) {
    if (q_spec_n[s] >= MAX_PATIENTS) return;
    for (int i = q_spec_n[s]; i > 0; --i) q_spec[s][i] = q_spec[s][i - 1];
    q_spec[s][0] = idx; q_spec_n[s]++;
}

static int q_spec_pop(int s) {
    if (q_spec_n[s] == 0) return -1;
    int v = q_spec[s][0];
    for (int i = 0; i < q_spec_n[s] - 1; ++i) q_spec[s][i] = q_spec[s][i + 1];
    q_spec_n[s]--; return v;
}

static void q_spec_remove(int s, int idx) {
    for (int i = 0; i < q_spec_n[s]; ++i)
        if (q_spec[s][i] == idx) {
            for (int j = i; j < q_spec_n[s] - 1; ++j) q_spec[s][j] = q_spec[s][j + 1];
            q_spec_n[s]--; return;
        }
}

/* ==================== РАССЫЛКА МОНИТОРАМ ==================== */
/*
 * Копируем дескрипторы под мьютексом, отправляем вне мьютекса.
 * При ошибке записи делаем shutdown — поток монитора проснётся
 * на read() и освободит слот. Так отключение монитора не ломает сервер.
 */
static void broadcast_event(const char *fmt, ...) {
    char msg[MAX_LINE];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    log_write(log_fd, t0, "%s\n", msg);

    int fds[MAX_MONITORS], n = 0;
    pthread_mutex_lock(&state_mtx);
    for (int i = 0; i < MAX_MONITORS; ++i)
        if (monitors[i].used) fds[n++] = monitors[i].fd;
    pthread_mutex_unlock(&state_mtx);

    for (int i = 0; i < n; ++i)
        if (send_line(fds[i], "EVENT %s", msg) < 0) shutdown(fds[i], SHUT_RDWR);
}

/* Формирование агрегированного снимка системы для мониторов */
static void make_snapshot(char *out, size_t cap) {
    char duty_s[128] = "", spec_s[128] = "";
    size_t dl = 0, sl = 0;

    pthread_mutex_lock(&state_mtx);
    for (int i = 0; i < duty_count; ++i)
        dl += (size_t)snprintf(duty_s + dl, sizeof(duty_s) - dl, "%s%d:%s",
                               i ? "," : "", i,
                               !duty[i].online ? "off" :
                               (duty[i].busy_pid >= 0 ? "busy" : "free"));
    for (int i = 0; i < SPEC_COUNT * per_spec; ++i)
        sl += (size_t)snprintf(spec_s + sl, sizeof(spec_s) - sl, "%s%s%d:%s",
                               i ? "," : "", SPEC_NAME[spec[i].type], spec[i].num,
                               !spec[i].online ? "off" :
                               (spec[i].busy_pid >= 0 ? "busy" : "free"));
    snprintf(out, cap,
             "SNAPSHOT prim=%d s0=%d s1=%d s2=%d duty=%s spec=%s "
             "arrived=%d discharged=%d aborted=%d target=%d",
             q_prim_n, q_spec_n[0], q_spec_n[1], q_spec_n[2],
             duty_s, spec_s, arrived, discharged, aborted, target);
    pthread_mutex_unlock(&state_mtx);
}

/* ==================== ОТПРАВКА ПАЦИЕНТУ ==================== */
static void patient_send(int idx, const char *fmt, ...) {
    char buf[MAX_LINE];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (buf[n - 1] != '\n') { buf[n] = '\n'; n++; }

    pthread_mutex_lock(&state_mtx);
    int fd = patients[idx].used ? patients[idx].fd : -1;
    pthread_mutex_t *m = &patients[idx].send_mtx;
    pthread_mutex_unlock(&state_mtx);

    if (fd >= 0) {
        pthread_mutex_lock(m);
        write_all(fd, buf, (size_t)n);
        pthread_mutex_unlock(m);
    }
}

/* ==================== РЕГИСТРАЦИЯ СЛОТОВ ==================== */
static int patient_alloc(int fd) {
    int idx = -1;
    pthread_mutex_lock(&state_mtx);
    for (int i = 0; i < MAX_PATIENTS; ++i)
        if (!patients[i].used) {
            patients[i].used = 1;
            patients[i].fd = fd;
            patients[i].st = P_FREE;
            patients[i].urgent = patients[i].exam_ms = patients[i].treat_ms = 0;
            patients[i].spec = 0;
            idx = i; break;
        }
    pthread_mutex_unlock(&state_mtx);
    return idx;
}

static DocSlot *doc_find(int kind, int type, int num) {
    if (kind == 0) return (num >= 0 && num < duty_count) ? &duty[num] : NULL;
    int si = type * per_spec + num;
    return (type >= 0 && type < SPEC_COUNT && num >= 0 && num < per_spec &&
            si < MAX_SPEC_SLOTS) ? &spec[si] : NULL;
}

/* ==================== ПОТОК: КЛИЕНТ-ПАЦИЕНТ ==================== */
static void serve_patient(int fd) {
    char line[MAX_LINE];
    int idx = patient_alloc(fd);
    if (idx < 0) { send_line(fd, "REJECT no_slots"); close(fd); return; }

    send_line(fd, "WELCOME PATIENT %d", idx);

    while (read_line(fd, line, sizeof line) > 0) {
        int urgent, exam, treat;
        if (sscanf(line, "ARRIVE %d %d %d", &urgent, &exam, &treat) == 3) {
            if (exam < 1)   exam = 1;
            if (treat < 1)  treat = 1;
            if (exam > 10000)  exam = 10000;
            if (treat > 10000) treat = 10000;

            pthread_mutex_lock(&state_mtx);
            if (patients[idx].st != P_FREE) { pthread_mutex_unlock(&state_mtx); continue; }
            Patient *p = &patients[idx];
            p->urgent  = urgent ? 1 : 0;
            p->exam_ms = exam;
            p->treat_ms = treat;
            p->spec    = rand() % SPEC_COUNT;   /* распределение заболеваний */
            p->st      = P_WAIT_PRIM;
            q_prim_push(idx, p->urgent);
            arrived++;
            int pos = q_prim_n, sp = p->spec, u = p->urgent;
            pthread_mutex_unlock(&state_mtx);

            send_line(fd, "QUEUED %d", pos);
            broadcast_event("Пациент %d прибыл%s, в очереди к дежурным (позиция %d)",
                            idx, u ? " [СРОЧНЫЙ]" : "", pos);
            (void)sp;
        }
        else if (!strcmp(line, "BYE")) break;
    }

    /* Клиент-пациент отключился: корректно освобождаем ресурсы */
    pthread_mutex_lock(&state_mtx);
    Patient *p = &patients[idx];
    PState st = p->st;
    if (st == P_WAIT_PRIM)  q_prim_remove(idx);
    if (st == P_WAIT_SPEC)  q_spec_remove(p->spec, idx);
    if (st != P_DONE && st != P_FREE && st != P_ABORT) { p->st = P_ABORT; aborted++; }
    int was_aborted = (p->st == P_ABORT);
    pthread_mutex_unlock(&state_mtx);

    if (was_aborted)
        broadcast_event("Пациент %d отключился до выписки (учтён как незавершённый)", idx);

    close(fd);
    pthread_mutex_lock(&state_mtx);
    patients[idx].used = 0; patients[idx].fd = -1;
    pthread_mutex_unlock(&state_mtx);
}

/* ==================== ПОТОК: КЛИЕНТ-ВРАЧ ==================== */
static void serve_doctor(int fd, int kind, int type, int num) {
    char line[MAX_LINE];
    DocSlot *slot = doc_find(kind, type, num);
    if (!slot) { send_line(fd, "REJECT bad_slot"); close(fd); return; }

    pthread_mutex_lock(&state_mtx);
    if (slot->online) { pthread_mutex_unlock(&state_mtx);
                        send_line(fd, "REJECT slot_busy"); close(fd); return; }
    slot->online = 1; slot->fd = fd; slot->busy_pid = -1;
    pthread_mutex_unlock(&state_mtx);

    const char *name = kind == 0 ? "Дежурный врач" : SPEC_NAME[type];
    send_line(fd, "WELCOME DOCTOR %d", kind == 0 ? num : type * per_spec + num);
    broadcast_event("%s %d подключился и готов к работе", name, num);

    while (read_line(fd, line, sizeof line) > 0) {

        /* --- врач запрашивает пациента --- */
        if (!strcmp(line, "READY")) {
            pthread_mutex_lock(&state_mtx);
            if (slot->busy_pid >= 0) {
                pthread_mutex_unlock(&state_mtx);
                send_line(fd, "WAIT busy"); continue;
            }
            int idx = kind == 0 ? q_prim_pop() : q_spec_pop(type);
            if (idx < 0) {
                pthread_mutex_unlock(&state_mtx);
                send_line(fd, "WAIT"); continue;
            }
            Patient *p = &patients[idx];
            slot->busy_pid = idx;
            p->st = (kind == 0) ? P_IN_EXAM : P_IN_TREAT;
            int pid = idx, ms = (kind == 0) ? p->exam_ms : p->treat_ms, u = p->urgent;
            pthread_mutex_unlock(&state_mtx);

            if (kind == 0) send_line(fd, "ASSIGN_EXAM %d %d %d", pid, ms, u);
            else           send_line(fd, "ASSIGN_TREAT %d %d", pid, ms);

            broadcast_event("%s %d начал %s пациента %d (%d мс)",
                            name, num, kind == 0 ? "осмотр" : "лечение", pid, ms);
        }

        /* --- дежурный завершил осмотр: выдаём направление --- */
        else if (strstr(line, "EXAM_DONE ") == line) {
            int pid = atoi(line + 10);
            pthread_mutex_lock(&state_mtx);
            if (slot->busy_pid != pid) { pthread_mutex_unlock(&state_mtx); continue; }
            slot->busy_pid = -1;
            Patient *p = &patients[pid];
            if (p->st == P_ABORT) { pthread_mutex_unlock(&state_mtx);
                broadcast_event("%s %d: осмотр пациента %d прерван (пациент отключён)",
                                name, num, pid); continue; }
            p->st = P_WAIT_SPEC;
            q_spec_push(p->spec, pid);
            int sp = p->spec, qn = q_spec_n[sp];
            pthread_mutex_unlock(&state_mtx);

            patient_send(pid, "REFERRAL %d", sp);
            broadcast_event("%s %d завершил осмотр пациента %d -> направление к %s "
                            "(в очереди %d)", name, num, pid, SPEC_NAME[sp], qn);
        }

        /* --- специалист завершил лечение: выписка --- */
        else if (strstr(line, "TREAT_DONE ") == line) {
            int pid = atoi(line + 11);
            pthread_mutex_lock(&state_mtx);
            if (slot->busy_pid != pid) { pthread_mutex_unlock(&state_mtx); continue; }
            slot->busy_pid = -1;
            Patient *p = &patients[pid];
            if (p->st == P_ABORT) { pthread_mutex_unlock(&state_mtx);
                broadcast_event("%s %d: лечение пациента %d прервано (пациент отключён)",
                                name, num, pid); continue; }
            p->st = P_DONE;
            discharged++;
            int done = (arrived >= target && discharged + aborted >= arrived);
            pthread_mutex_unlock(&state_mtx);

            patient_send(pid, "DISCHARGED");
            broadcast_event("%s %d завершил лечение пациента %d — ВЫПИСАН (%d/%d)",
                            name, num, pid, discharged, target);
            if (done) running = 0;
        }
        else if (!strcmp(line, "BYE")) break;
    }

    /*
     * КЛЮЧЕВАЯ ЛОГИКА для оценки >8:
     * при отключении врача пациент возвращается в НАЧАЛО очереди,
     * слот освобождается и может быть занят переподключившимся врачом.
     */
    pthread_mutex_lock(&state_mtx);
    int returned = -1;
    if (slot->busy_pid >= 0) {
        int idx = slot->busy_pid;
        Patient *p = &patients[idx];
        if (p->st == P_IN_EXAM)  { p->st = P_WAIT_PRIM; q_prim_front(idx);      returned = idx; }
        if (p->st == P_IN_TREAT) { p->st = P_WAIT_SPEC; q_spec_front(p->spec, idx); returned = idx; }
        slot->busy_pid = -1;
    }
    slot->online = 0; slot->fd = -1;
    pthread_mutex_unlock(&state_mtx);

    if (returned >= 0)
        broadcast_event("%s %d ОТКЛЮЧИЛСЯ: пациент %d возвращён в начало очереди",
                        name, num, returned);
    else
        broadcast_event("%s %d отключился", name, num);

    close(fd);
}

/* ==================== ПОТОК: КЛИЕНТ-МОНИТОР ==================== */
static void serve_monitor(int fd) {
    char line[MAX_LINE];
    int mi = -1;
    pthread_mutex_lock(&state_mtx);
    for (int i = 0; i < MAX_MONITORS; ++i)
        if (!monitors[i].used) { monitors[i].used = 1; monitors[i].fd = fd; mi = i; break; }
    pthread_mutex_unlock(&state_mtx);

    if (mi < 0) { send_line(fd, "REJECT monitor_limit"); close(fd); return; }

    send_line(fd, "WELCOME MONITOR %d", mi);
    char snap[MAX_LINE];
    make_snapshot(snap, sizeof snap);
    send_line(fd, "%s", snap);

    /* Монитор только читает; вся рассылка идёт через broadcast_event */
    while (read_line(fd, line, sizeof line) > 0) {
        if (!strcmp(line, "PING")) send_line(fd, "PONG");
        else if (!strcmp(line, "SNAP")) { make_snapshot(snap, sizeof snap); send_line(fd, "%s", snap); }
        else if (!strcmp(line, "BYE")) break;
    }

    pthread_mutex_lock(&state_mtx);
    monitors[mi].used = 0; monitors[mi].fd = -1;
    pthread_mutex_unlock(&state_mtx);
    close(fd);
}

/* ==================== ДИСПЕТЧЕР ПОДКЛЮЧЕНИЙ ==================== */
static void *client_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char line[MAX_LINE];
    if (read_line(fd, line, sizeof line) <= 0) { close(fd); return NULL; }

    if (!strcmp(line, "HELLO PATIENT")) {
        serve_patient(fd);
    } else if (strstr(line, "HELLO DOCTOR DUTY ") == line) {
        serve_doctor(fd, 0, 0, atoi(line + 18));
    } else if (strstr(line, "HELLO DOCTOR SPEC ") == line) {
        int type = 0, num = 0;
        sscanf(line + 18, "%d %d", &type, &num);
        serve_doctor(fd, 1, type, num);
    } else if (!strcmp(line, "HELLO MONITOR")) {
        serve_monitor(fd);
    } else {
        send_line(fd, "REJECT unknown_role");
        close(fd);
    }
    return NULL;
}

/* ==================== ПОТОК: ПЕРИОДИЧЕСКИЙ СНИМОК ==================== */
static void *snapshot_thread(void *arg) {
    (void)arg;
    while (running) {
        usleep(500000);
        char snap[MAX_LINE];
        make_snapshot(snap, sizeof snap);
        int fds[MAX_MONITORS], n = 0;
        pthread_mutex_lock(&state_mtx);
        for (int i = 0; i < MAX_MONITORS; ++i)
            if (monitors[i].used) fds[n++] = monitors[i].fd;
        pthread_mutex_unlock(&state_mtx);
        for (int i = 0; i < n; ++i)
            if (send_line(fds[i], "%s", snap) < 0) shutdown(fds[i], SHUT_RDWR);
    }
    return NULL;
}

/* ==================== ЗАВЕРШЕНИЕ ==================== */
static void on_sigint(int s) { (void)s; running = 0; }

/* Рассылка SHUTDOWN всем подключённым клиентам (бонус >8) */
static void shutdown_all_clients(void) {
    int fds[MAX_PATIENTS + MAX_DUTY + MAX_SPEC_SLOTS + MAX_MONITORS], n = 0;
    pthread_mutex_lock(&state_mtx);
    for (int i = 0; i < MAX_PATIENTS; ++i)   if (patients[i].used) fds[n++] = patients[i].fd;
    for (int i = 0; i < MAX_DUTY; ++i)       if (duty[i].online)   fds[n++] = duty[i].fd;
    for (int i = 0; i < MAX_SPEC_SLOTS; ++i) if (spec[i].online)   fds[n++] = spec[i].fd;
    for (int i = 0; i < MAX_MONITORS; ++i)   if (monitors[i].used) fds[n++] = monitors[i].fd;
    pthread_mutex_unlock(&state_mtx);

    for (int i = 0; i < n; ++i) {
        send_line(fds[i], "SHUTDOWN server_stopping");
        shutdown(fds[i], SHUT_RDWR);
    }
    log_write(log_fd, t0, "Разослан SHUTDOWN %d клиентам\n", n);
    usleep(300000);   /* даём клиентам время корректно завершиться */
}

/* ==================== MAIN ==================== */
int main(int argc, char *argv[]) {
    int port = 5555;
    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--port")     && i + 1 < argc) port       = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--patients") && i + 1 < argc) target     = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--duty")     && i + 1 < argc) duty_count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--per-spec") && i + 1 < argc) per_spec   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed")     && i + 1 < argc) rng_seed   = (unsigned)atoi(argv[++i]);
    }
    if (duty_count < 1) duty_count = 1;
    if (duty_count > MAX_DUTY) duty_count = MAX_DUTY;
    if (per_spec < 1) per_spec = 1;
    if (per_spec > MAX_PER_SPEC) per_spec = MAX_PER_SPEC;
    if (target < 1) target = 1;
    if (target > MAX_PATIENTS) target = MAX_PATIENTS;

    srand(rng_seed);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);
    t0 = now_sec();

    log_fd = open("hospital_server.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    for (int i = 0; i < MAX_PATIENTS; ++i) pthread_mutex_init(&patients[i].send_mtx, NULL);
    for (int i = 0; i < MAX_DUTY; ++i) { duty[i].kind = 0; duty[i].num = i; duty[i].busy_pid = -1; }
    for (int i = 0; i < SPEC_COUNT * MAX_PER_SPEC; ++i) {
        spec[i].kind = 1; spec[i].type = i / MAX_PER_SPEC;
        spec[i].num = i % MAX_PER_SPEC; spec[i].busy_pid = -1;
    }

    /* --- создаём слушающий сокет --- */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* доступен из сети */
    addr.sin_port        = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, 64) < 0) { perror("listen"); return 1; }

    log_write(log_fd, t0, "=== СЕРВЕР БОЛЬНИЦЫ запущен на порту %d ===\n", port);
    log_write(log_fd, t0, "Пациентов=%d, дежурных=%d, специалистов на профиль=%d, seed=%u\n",
              target, duty_count, per_spec, rng_seed);

    pthread_t snap_tid;
    pthread_create(&snap_tid, NULL, snapshot_thread, NULL);
    pthread_detach(snap_tid);

    /* --- цикл приёма подключений с таймаутом для проверки running --- */
    while (running) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(listen_fd, &rfds);
        struct timeval tv = {0, 200000};
        int r = select(listen_fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) continue;

        struct sockaddr_in ca; socklen_t cl = sizeof ca;
        int cfd = accept(listen_fd, (struct sockaddr *)&ca, &cl);
        if (cfd < 0) continue;
        int nodelay = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, (void *)(intptr_t)cfd) == 0)
            pthread_detach(tid);
        else close(cfd);
    }

    /* --- корректное завершение --- */
    log_write(log_fd, t0, "=== ЗАВЕРШЕНИЕ РАБОТЫ СЕРВЕРА ===\n");
    shutdown_all_clients();

    pthread_mutex_lock(&state_mtx);
    int a = arrived, d = discharged, ab = aborted;
    int left_prim = q_prim_n, left0 = q_spec_n[0], left1 = q_spec_n[1], left2 = q_spec_n[2];
    pthread_mutex_unlock(&state_mtx);

    log_write(log_fd, t0, "--- ИТОГОВАЯ СТАТИСТИКА ---\n");
    log_write(log_fd, t0, "Прибыло пациентов:      %d из %d\n", a, target);
    log_write(log_fd, t0, "Выписано после лечения: %d\n", d);
    log_write(log_fd, t0, "Не завершено (отказ):   %d\n", ab);
    log_write(log_fd, t0, "Осталось в очередях: дежурные=%d, стоматолог=%d, хирург=%d, терапевт=%d\n",
              left_prim, left0, left1, left2);
    log_write(log_fd, t0, "Лог: hospital_server.log\n");

    close(listen_fd);
    for (int i = 0; i < MAX_PATIENTS; ++i) pthread_mutex_destroy(&patients[i].send_mtx);
    if (log_fd >= 0) close(log_fd);
    return 0;
}
