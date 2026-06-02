/* TCP 원격 장치 제어 서버 (데몬) — 진입점
 *   - raw TCP 소켓 + 연결당 pthread
 *   - libdevice.so 를 dlopen/dlsym 으로 런타임 동적 로딩 (binding.c)
 *   - 텍스트 라인 프로토콜 (command.c), 모드 스레드 (modes.c), HTML 로그 (weblog.c)
 *
 * 빌드: make (server 소스 전체 → devserver, -lpthread -ldl)
 * 실행: ./devserver [port] [log_port]   (기본 5000 / 8080) — 데몬으로 백그라운드 동작
 *   - port     : TCP 장치 제어 포트
 *   - log_port : HTML 실시간 로그 뷰어 포트 (브라우저로 http://<RPi-IP>:log_port 접속)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "server.h"

#define DEFAULT_PORT     5000
#define DEFAULT_LOG_PORT 8080   /* HTML 실시간 로그 뷰어 포트(추가기능) */
#define LIB_PATH         "./libdevice.so"

/* ── 연결 핸들러 ──────────────────────────────────────── */
static void *client_thread(void *arg)
{
    int sock = (int)(intptr_t)arg;
    char buf[1024];
    int len = 0;

    /* 접속 클라이언트 IP 로깅 */
    struct sockaddr_in pa;
    socklen_t pl = sizeof(pa);
    char ip[32] = "?";
    if (getpeername(sock, (struct sockaddr *)&pa, &pl) == 0)
        inet_ntop(AF_INET, &pa.sin_addr, ip, sizeof(ip));
    dlog(LOG_INFO, "연결 [%s] (fd=%d)", ip, sock);

    send_line(sock, "OK WELCOME");
    for (;;) {
        int n = recv(sock, buf + len, sizeof(buf) - 1 - (size_t)len, 0);
        if (n <= 0) break;
        len += n; buf[len] = '\0';

        char *nl;
        while ((nl = memchr(buf, '\n', (size_t)len))) {
            *nl = '\0';
            char *cr = strchr(buf, '\r');
            if (cr) *cr = '\0';
            if (buf[0])
                dlog(LOG_INFO, "[%s] %s", ip, buf);    /* 수신 명령 기록 */
            int r = handle_cmd(sock, buf);
            int consumed = (int)(nl - buf) + 1;
            len -= consumed;
            memmove(buf, nl + 1, (size_t)len);
            buf[len] = '\0';
            if (r < 0) { dlog(LOG_INFO, "종료 [%s] (QUIT)", ip); close(sock); return NULL; }
        }
        if (len >= (int)sizeof(buf) - 1) len = 0;  /* 과도한 라인 폐기 */
    }

    /* 연결 종료 시 이 소켓이 소유한 모드 정리 */
    if (g_cds_sock == sock) g_cds_run = 0;
    if (g_fnd_sock == sock) g_fnd_run = 0;
    dlog(LOG_INFO, "연결 종료 [%s] (fd=%d)", ip, sock);
    close(sock);
    return NULL;
}

/* ── 데몬화 ───────────────────────────────────────────── */
static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);          /* 부모 종료 */
    setsid();                      /* 새 세션 리더 */
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid > 0) exit(0);          /* 세션 리더 종료 → 제어 터미널 분리 */
    umask(0);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

int main(int argc, char **argv)
{
    int port     = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    int log_port = (argc > 2) ? atoi(argv[2]) : DEFAULT_LOG_PORT;

    /* libdevice.so 는 cwd 상대경로 → 데몬화 전에 절대경로로 고정 */
    char abslib[PATH_MAX] = {0};
    if (!realpath(LIB_PATH, abslib))
        strncpy(abslib, LIB_PATH, sizeof(abslib) - 1);

    daemonize();
    openlog("devserver", LOG_PID, LOG_DAEMON);

    if (lib_load(abslib) != 0) return 1;
    if (dev_init() != 0) { syslog(LOG_ERR, "device_init failed"); return 1; }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { syslog(LOG_ERR, "socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "bind: port %d", port); return 1;
    }
    if (listen(srv, 8) < 0) { syslog(LOG_ERR, "listen"); return 1; }
    dlog(LOG_INFO, "devserver 시작: TCP 포트 %d", port);

    /* HTML 실시간 로그 뷰어 (2번째 포트) */
    pthread_t lt;
    if (pthread_create(&lt, NULL, log_server_thread, (void *)(intptr_t)log_port) == 0)
        pthread_detach(lt);

    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, (void *)(intptr_t)c) != 0)
            close(c);
        else
            pthread_detach(tid);
    }

    dev_cleanup();
    lib_unload();
    closelog();
    return 0;
}
