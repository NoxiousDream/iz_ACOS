/*
 * hospital_doctor.c — независимый клиент-врач.
 * Подключается к серверу, запрашивает пациентов, «обслуживает» их
 * (задержка = длительность процедуры), сообщает о завершении.
 *
 * Поддерживает автоматическое переподключение (--reconnect),
 * что демонстрирует устойчивость сервера к отключению клиентов.
 *
 * Компиляция: gcc -std=c99 -Wall -o hospital_doctor hospital_doctor.c -lpthread
 * Запуск:  ./hospital_doctor --host 127.0.0.1 --port 5555 --role duty --num 0
 *          ./hospital_doctor --host 127.0.0.1 --port 5555 --role spec --type 1 --num 0
 */
#include "hospital_proto.h"

static volatile sig_atomic_t run = 1;
static void on_sig(int s) { (void)s; run = 0; }

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 5555, kind = 0, type = 0, num = 0, reconnect = 0;

    for (int i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--role") && i + 1 < argc) kind = !strcmp(argv[++i], "spec");
        else if (!strcmp(argv[i], "--type") && i + 1 < argc) type = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--num")  && i + 1 < argc) num  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reconnect")) reconnect = 1;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);
    double t0 = now_sec();

    char logname[128];
    snprintf(logname, sizeof logname, "doctor_%s%d_%d.log", kind ? "spec" : "duty", type, num);
    int logfd = open(logname, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    const char *name = kind ? SPEC_NAME[type] : "Дежурный врач";

    while (run) {
        int fd = tcp_connect(host, port);
        if (fd < 0) {
            log_write(logfd, t0, "%s %d: сервер недоступен, повтор через 1 с\n", name, num);
            if (!reconnect) break;
            sleep(1); continue;
        }

        if (kind) send_line(fd, "HELLO DOCTOR SPEC %d %d", type, num);
        else      send_line(fd, "HELLO DOCTOR DUTY %d", num);

        char line[MAX_LINE];
        if (read_line(fd, line, sizeof line) <= 0) { close(fd); sleep(1); continue; }
        if (strstr(line, "REJECT") == line) {
            log_write(logfd, t0, "%s %d: отказ сервера: %s\n", name, num, line);
            close(fd);
            if (!reconnect) break;
            sleep(1); continue;
        }
        log_write(logfd, t0, "%s %d подключился к %s:%d (%s)\n", name, num, host, port, line);

        int served = 0;
        while (run) {
            if (send_line(fd, "READY") < 0) break;
            ssize_t n = read_line(fd, line, sizeof line);
            if (n <= 0) break;

            if (strstr(line, "ASSIGN_EXAM ") == line) {
                int pid, ms, urgent;
                sscanf(line + 12, "%d %d %d", &pid, &ms, &urgent);
                log_write(logfd, t0, "%s %d: НАЧАЛ ОСМОТР пациента %d%s (%d мс)\n",
                          name, num, pid, urgent ? " [СРОЧНЫЙ]" : "", ms);
                usleep((useconds_t)ms * 1000);
                if (send_line(fd, "EXAM_DONE %d", pid) < 0) break;
                log_write(logfd, t0, "%s %d: ЗАВЕРШИЛ ОСМОТР пациента %d\n", name, num, pid);
                served++;
            }
            else if (strstr(line, "ASSIGN_TREAT ") == line) {
                int pid, ms;
                sscanf(line + 13, "%d %d", &pid, &ms);
                log_write(logfd, t0, "%s %d: НАЧАЛ ЛЕЧЕНИЕ пациента %d (%d мс)\n",
                          name, num, pid, ms);
                usleep((useconds_t)ms * 1000);
                if (send_line(fd, "TREAT_DONE %d", pid) < 0) break;
                log_write(logfd, t0, "%s %d: ЗАВЕРШИЛ ЛЕЧЕНИЕ пациента %d\n", name, num, pid);
                served++;
            }
            else if (!strcmp(line, "WAIT") || strstr(line, "WAIT") == line) {
                usleep(100000);   /* нет пациентов — «спим» */
            }
            else if (strstr(line, "SHUTDOWN") == line) {
                log_write(logfd, t0, "%s %d: сервер завершает работу, выходим\n", name, num);
                run = 0; break;
            }
        }
        close(fd);
        log_write(logfd, t0, "%s %d: соединение закрыто (обслужено %d)\n", name, num, served);
        if (!run || !reconnect) break;
        log_write(logfd, t0, "%s %d: переподключение через 1 с...\n", name, num);
        sleep(1);
    }

    log_write(logfd, t0, "%s %d завершил работу\n", name, num);
    if (logfd >= 0) close(logfd);
    return 0;
}
