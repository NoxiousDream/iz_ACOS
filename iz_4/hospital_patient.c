/*
 * hospital_patient.c — независимый клиент-пациент.
 * Сообщает о поступлении, ожидает направления и выписки.
 * Отображает ТОЛЬКО свои события (требование на 4-5 баллов).
 *
 * Компиляция: gcc -std=c99 -Wall -o hospital_patient hospital_patient.c -lpthread
 * Запуск:  ./hospital_patient --host 127.0.0.1 --port 5555 --urgent 1 --exam 300 --treat 700
 */
#include "hospital_proto.h"

static volatile sig_atomic_t run = 1;
static void on_sig(int s) { (void)s; run = 0; }

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 5555, urgent = 0, exam = 250, treat = 500, seed = 0;

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--host")   && i + 1 < argc) host   = argv[++i];
        else if (!strcmp(argv[i], "--port")   && i + 1 < argc) port   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--urgent") && i + 1 < argc) urgent = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--exam")   && i + 1 < argc) exam   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--treat")  && i + 1 < argc) treat  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed")   && i + 1 < argc) seed   = atoi(argv[++i]);
    }
    if (seed) { srand((unsigned)seed); exam = 100 + rand() % 400; treat = 200 + rand() % 600;
                urgent = (rand() % 100) < 20; }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);
    double t0 = now_sec();

    int fd = tcp_connect(host, port);
    if (fd < 0) { fprintf(stderr, "Не удалось подключиться к %s:%d\n", host, port); return 1; }

    int logfd = open("patient.log", O_WRONLY | O_CREAT | O_APPEND, 0644);

    send_line(fd, "HELLO PATIENT");
    char line[MAX_LINE];
    if (read_line(fd, line, sizeof line) <= 0) { close(fd); return 1; }
    if (strstr(line, "REJECT") == line) {
        log_write(logfd, t0, "Отказ сервера: %s\n", line); close(fd); return 1;
    }
    int myid = -1;
    sscanf(line, "WELCOME PATIENT %d", &myid);
    log_write(logfd, t0, "Я пациент %d, подключился к %s:%d\n", myid, host, port);

    send_line(fd, "ARRIVE %d %d %d", urgent, exam, treat);
    log_write(logfd, t0, "Пациент %d поступил в приёмное отделение%s "
              "(осмотр %d мс, лечение %d мс)\n", myid, urgent ? " [СРОЧНЫЙ]" : "", exam, treat);

    while (run) {
        ssize_t n = read_line(fd, line, sizeof line);
        if (n <= 0) { log_write(logfd, t0, "Пациент %d: связь с сервером потеряна\n", myid); break; }

        if (strstr(line, "QUEUED ") == line) {
            log_write(logfd, t0, "Пациент %d встал в очередь к дежурным (позиция %s)\n",
                      myid, line + 7);
        }
        else if (strstr(line, "REFERRAL ") == line) {
            int sp = atoi(line + 9);
            if (sp >= 0 && sp < SPEC_COUNT)
                log_write(logfd, t0, "Пациент %d получил направление к %s, переходу в его очередь\n",
                          myid, SPEC_NAME[sp]);
        }
        else if (strstr(line, "DISCHARGED") == line) {
            log_write(logfd, t0, "Пациент %d ВЫПИСАН. Всего времени: %.3f с\n",
                      myid, now_sec() - t0);
            break;
        }
        else if (strstr(line, "SHUTDOWN") == line) {
            log_write(logfd, t0, "Пациент %d: больница закрывается, завершаемся\n", myid);
            break;
        }
    }

    send_line(fd, "BYE");
    close(fd);
    if (logfd >= 0) close(logfd);
    return 0;
}
