/*
 * hospital_proto.h — общий протокол и утилиты TCP-взаимодействия.
 * Протокол текстовый, построчный (сообщение = строка, завершается '\n').
 * Это позволяет отлаживать систему через telnet/netcat.
 */
#ifndef HOSPITAL_PROTO_H
#define HOSPITAL_PROTO_H

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_LINE   1024
#define SPEC_COUNT 3

/* Профили специалистов */
enum { SPEC_DENTIST = 0, SPEC_SURGEON = 1, SPEC_THERAPIST = 2 };
static const char *SPEC_NAME[SPEC_COUNT] = {"Стоматолог", "Хирург", "Терапевт"};

/* ---------- надёжная запись всех байт ---------- */
static inline int write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/* ---------- форматированная отправка строки ---------- */
static inline int send_line(int fd, const char *fmt, ...) {
    char buf[MAX_LINE];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n == 0 || buf[n - 1] != '\n') { buf[n] = '\n'; n++; }
    return write_all(fd, buf, (size_t)n);
}

/* ---------- чтение строки до '\n'; -1 при EOF/ошибке ---------- */
static inline ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t len = 0;
    for (;;) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return len > 0 ? (ssize_t)len : -1;   /* EOF */
        if (c == '\n') { buf[len] = '\0'; return (ssize_t)len; }
        if (c == '\r') continue;
        if (len + 1 < cap) buf[len++] = c;
    }
}

/* ---------- монотонное время в секундах ---------- */
static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ---------- подключение к серверу (IPv4/IPv6, имя или адрес) ---------- */
static inline int tcp_connect(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;

    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

/* ---------- логгер на системных вызовах (stdout + файл) ---------- */
static inline void log_write(int logfd, double t0, const char *fmt, ...) {
    char ts[32], msg[MAX_LINE], out[MAX_LINE + 64];
    snprintf(ts, sizeof ts, "[%7.3f] ", now_sec() - t0);
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    int n = snprintf(out, sizeof out, "%s%s", ts, msg);
    if (n > 0) {
        write(STDOUT_FILENO, out, (size_t)n);
        if (logfd >= 0) write(logfd, out, (size_t)n);
    }
}

#endif /* HOSPITAL_PROTO_H */
