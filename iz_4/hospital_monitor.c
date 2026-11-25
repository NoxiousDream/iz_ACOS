/*
 * hospital_monitor.c — клиент-наблюдатель (требование 6-8 баллов).
 * Получает от сервера поток событий и агрегированные снимки,
 * отображает КОМПЛЕКСНУЮ картину работы больницы.
 *
 * Можно запустить сколько угодно экземпляров на разных компьютерах,
 * отключать и подключать в любой момент — сервер продолжает работу.
 *
 * Компиляция: gcc -std=c99 -Wall -o hospital_monitor hospital_monitor.c -lpthread
 * Запуск:  ./hospital_monitor --host 127.0.0.1 --port 5555 --reconnect
 */
#include "hospital_proto.h"

static volatile sig_atomic_t run = 1;
static void on_sig(int s) { (void)s; run = 0; }

/* Разбор строки SNAPSHOT и вывод панели состояния */
static void render_snapshot(const char *s, int logfd, double t0) {
    int prim = 0, q0 = 0, q1 = 0, q2 = 0, ar = 0, di = 0, ab = 0, tg = 0;
    char duty[128] = "-", spec[128] = "-";
    sscanf(s, "SNAPSHOT prim=%d s0=%d s1=%d s2=%d duty=%127s spec=%127s "
              "arrived=%d discharged=%d aborted=%d target=%d",
           &prim, &q0, &q1, &q2, duty, spec, &ar, &di, &ab, &tg);

    char panel[1024];
    int n = snprintf(panel, sizeof panel,
        "\n+------------------ СОСТОЯНИЕ БОЛЬНИЦЫ ------------------+\n"
        " Очередь к дежурным : %-4d   Стоматолог: %-4d\n"
        " Хирург             : %-4d   Терапевт  : %-4d\n"
        " Дежурные врачи     : %s\n"
        " Специалисты        : %s\n"
        " Прибыло: %-4d  Выписано: %-4d  Отказов: %-4d  План: %-4d\n"
        "+-------------------------------------------------------+\n",
        prim, q0, q1, q2, duty, spec, ar, di, ab, tg);
    if (n > 0) {
        write(STDOUT_FILENO, panel, (size_t)n);
        if (logfd >= 0) write(logfd, panel, (size_t)n);
    }
    (void)t0;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 5555, reconnect = 0, quiet = 0;

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reconnect")) reconnect = 1;
        else if (!strcmp(argv[i], "--quiet"))     quiet = 1;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);
    double t0 = now_sec();
    int logfd = open("monitor.log", O_WRONLY | O_CREAT | O_APPEND, 0644);

    while (run) {
        int fd = tcp_connect(host, port);
        if (fd < 0) {
            log_write(logfd, t0, "МОНИТОР: сервер недоступен, повтор через 1 с\n");
            if (!reconnect) break;
            sleep(1); continue;
        }
        send_line(fd, "HELLO MONITOR");
        char line[MAX_LINE];
        if (read_line(fd, line, sizeof line) <= 0) { close(fd); sleep(1); continue; }
        if (strstr(line, "REJECT") == line) {
            log_write(logfd, t0, "МОНИТОР: отказ: %s\n", line); close(fd); break;
        }
        log_write(logfd, t0, "МОНИТОР подключён к %s:%d (%s)\n", host, port, line);

        while (run) {
            ssize_t n = read_line(fd, line, sizeof line);
            if (n <= 0) break;
            if (strstr(line, "EVENT ") == line) {
                log_write(logfd, t0, "%s\n", line + 6);
            } else if (strstr(line, "SNAPSHOT ") == line) {
                if (!quiet) render_snapshot(line, logfd, t0);
            } else if (strstr(line, "SHUTDOWN") == line) {
                log_write(logfd, t0, "МОНИТОР: сервер завершает работу, выходим\n");
                run = 0; break;
            }
        }
        close(fd);
        if (!run || !reconnect) break;
        log_write(logfd, t0, "МОНИТОР: переподключение через 1 с...\n");
        sleep(1);
    }

    log_write(logfd, t0, "МОНИТОР завершил работу\n");
    if (logfd >= 0) close(logfd);
    return 0;
}
