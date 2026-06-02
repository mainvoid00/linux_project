/* TCP 원격 장치 제어 서버 (데몬)
 *   - raw TCP 소켓 + 연결당 pthread
 *   - libdevice.so 를 dlopen/dlsym 으로 런타임 동적 로딩
 *   - 텍스트 라인 프로토콜 (요청 1줄 / 응답·이벤트 1줄)
 *
 * 빌드: gcc -Wall -Wextra -o devserver server/server.c -lpthread -ldl
 * 실행: ./devserver [port]   (기본 5000) — 데몬으로 백그라운드 동작
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 5000
#define LIB_PATH     "./libdevice.so"

/* ── libdevice.so 함수 포인터 ─────────────────────────── */
static void *g_lib;
static int  (*dev_init)(void);
static void (*dev_cleanup)(void);
static int  (*led_on)(void);
static int  (*led_off)(void);
static int  (*led_bright)(int);
static int  (*buzzer_tone)(int);
static int  (*buzzer_off)(void);
static int  (*cds_read)(void);
static int  (*fnd_display)(int);
static int  (*fnd_clear)(void);

/* GPIO/장치는 공유 자원 → mutex 로 보호 */
static pthread_mutex_t g_dev = PTHREAD_MUTEX_INITIALIZER;

/* ── 모드 상태 (한 시점에 cds/fnd/buzzer 각 1개) ──────── */
static volatile int g_cds_run, g_fnd_run, g_buz_run;
static int g_cds_sock = -1, g_fnd_sock = -1;

/* 저장된 멜로디(계이름 주파수 Hz, 지속 ms): 도레미파솔라시도 */
static const int MELODY[][2] = {
    {262,300},{294,300},{330,300},{349,300},
    {392,300},{440,300},{494,300},{523,500},
};
#define MELODY_LEN ((int)(sizeof(MELODY)/sizeof(MELODY[0])))

/* ── 유틸 ─────────────────────────────────────────────── */
static void send_line(int sock, const char *s)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s\n", s);
    if (n > 0)
        send(sock, buf, (size_t)n, MSG_NOSIGNAL);
}

static void bind_symbols(void)
{
    dev_init    = dlsym(g_lib, "device_init");
    dev_cleanup = dlsym(g_lib, "device_cleanup");
    led_on      = dlsym(g_lib, "led_on");
    led_off     = dlsym(g_lib, "led_off");
    led_bright  = dlsym(g_lib, "led_bright");
    buzzer_tone = dlsym(g_lib, "buzzer_tone");
    buzzer_off  = dlsym(g_lib, "buzzer_off");
    cds_read    = dlsym(g_lib, "cds_read");
    fnd_display = dlsym(g_lib, "fnd_display");
    fnd_clear   = dlsym(g_lib, "fnd_clear");
}

/* ── 모드 스레드 ──────────────────────────────────────── */

/* 조도 자동 연동: 매초 조도 읽어 LED 자동제어(빛 없으면 ON) + 클라이언트로 값 전송 */
static void *cds_thread(void *arg)
{
    int sock = (int)(intptr_t)arg;
    while (g_cds_run) {
        int v;
        pthread_mutex_lock(&g_dev);
        v = cds_read();
        if (v == 0) led_on();   /* 빛 없음(어두움) → LED ON */
        else        led_off();  /* 빛 있음(밝음)   → LED OFF */
        pthread_mutex_unlock(&g_dev);

        char msg[64];
        snprintf(msg, sizeof(msg), "EVT CDS %d %s", v, v ? "LIGHT" : "DARK");
        send_line(sock, msg);
        sleep(1);
    }
    return NULL;
}

/* 7세그 카운트다운: n→0 매초 -1 표시, 0 도달 시 부저 1초 */
static void *fnd_thread(void *arg)
{
    int start = (int)(intptr_t)arg;
    int i;
    for (i = start; i >= 0 && g_fnd_run; i--) {
        char msg[32];
        pthread_mutex_lock(&g_dev);
        fnd_display(i);
        pthread_mutex_unlock(&g_dev);
        snprintf(msg, sizeof(msg), "EVT FND %d", i);
        send_line(g_fnd_sock, msg);
        if (i == 0) break;
        sleep(1);
    }
    if (g_fnd_run) {            /* 정상적으로 0 도달 → 부저 울림 */
        pthread_mutex_lock(&g_dev);
        buzzer_tone(1000);
        pthread_mutex_unlock(&g_dev);
        sleep(1);
        pthread_mutex_lock(&g_dev);
        buzzer_off();
        fnd_clear();
        pthread_mutex_unlock(&g_dev);
        send_line(g_fnd_sock, "EVT FND DONE");
    }
    g_fnd_run = 0;
    return NULL;
}

/* 부저 멜로디 반복 재생 */
static void *buzzer_thread(void *arg)
{
    (void)arg;
    while (g_buz_run) {
        int i;
        for (i = 0; i < MELODY_LEN && g_buz_run; i++) {
            pthread_mutex_lock(&g_dev);
            buzzer_tone(MELODY[i][0]);
            pthread_mutex_unlock(&g_dev);
            usleep((useconds_t)MELODY[i][1] * 1000);
        }
    }
    pthread_mutex_lock(&g_dev);
    buzzer_off();
    pthread_mutex_unlock(&g_dev);
    return NULL;
}

/* ── 명령 처리: 0=계속, -1=연결 종료 ──────────────────── */
static int handle_cmd(int sock, char *line)
{
    char *cmd = strtok(line, " \t");
    char *a1  = strtok(NULL, " \t");
    pthread_t tid;

    if (!cmd)
        return 0;

    if (!strcmp(cmd, "LED")) {
        if (!a1) { send_line(sock, "ERR INVALID_ARG"); return 0; }
        pthread_mutex_lock(&g_dev);
        if      (!strcmp(a1, "ON"))   { led_on();      send_line(sock, "OK LED ON"); }
        else if (!strcmp(a1, "OFF"))  { led_off();     send_line(sock, "OK LED OFF"); }
        else if (!strcmp(a1, "HIGH")) { led_bright(3); send_line(sock, "OK LED HIGH"); }
        else if (!strcmp(a1, "MID"))  { led_bright(2); send_line(sock, "OK LED MID"); }
        else if (!strcmp(a1, "LOW"))  { led_bright(1); send_line(sock, "OK LED LOW"); }
        else                          { send_line(sock, "ERR INVALID_ARG"); }
        pthread_mutex_unlock(&g_dev);
    }
    else if (!strcmp(cmd, "BUZZER")) {
        if (a1 && !strcmp(a1, "ON")) {
            if (!g_buz_run) { g_buz_run = 1; pthread_create(&tid, NULL, buzzer_thread, NULL); pthread_detach(tid); }
            send_line(sock, "OK BUZZER ON");
        } else if (a1 && !strcmp(a1, "OFF")) {
            g_buz_run = 0;
            send_line(sock, "OK BUZZER OFF");
        } else send_line(sock, "ERR INVALID_ARG");
    }
    else if (!strcmp(cmd, "CDS")) {
        if (a1 && !strcmp(a1, "ON")) {
            g_cds_run = 0; usleep(1100*1000);          /* 기존 모드 정리 */
            g_cds_sock = sock; g_cds_run = 1;
            pthread_create(&tid, NULL, cds_thread, (void *)(intptr_t)sock);
            pthread_detach(tid);
            send_line(sock, "OK CDS ON");
        } else if (a1 && !strcmp(a1, "OFF")) {
            g_cds_run = 0;
            send_line(sock, "OK CDS OFF");
        } else if (a1 && !strcmp(a1, "READ")) {
            int v; char msg[64];
            pthread_mutex_lock(&g_dev); v = cds_read(); pthread_mutex_unlock(&g_dev);
            snprintf(msg, sizeof(msg), "OK CDS %d %s", v, v ? "LIGHT" : "DARK");
            send_line(sock, msg);
        } else send_line(sock, "ERR INVALID_ARG");
    }
    else if (!strcmp(cmd, "FND")) {
        if (a1 && !strcmp(a1, "STOP")) {
            g_fnd_run = 0;
            pthread_mutex_lock(&g_dev); fnd_clear(); pthread_mutex_unlock(&g_dev);
            send_line(sock, "OK FND STOP");
        } else if (a1 && a1[0] >= '0' && a1[0] <= '9' && a1[1] == '\0') {
            g_fnd_run = 0; usleep(100*1000);
            g_fnd_sock = sock; g_fnd_run = 1;
            pthread_create(&tid, NULL, fnd_thread, (void *)(intptr_t)(a1[0]-'0'));
            pthread_detach(tid);
            send_line(sock, "OK FND START");
        } else send_line(sock, "ERR INVALID_ARG");
    }
    else if (!strcmp(cmd, "STATUS")) {
        char msg[96];
        snprintf(msg, sizeof(msg), "OK STATUS cds=%d fnd=%d buzzer=%d",
                 g_cds_run, g_fnd_run, g_buz_run);
        send_line(sock, msg);
    }
    else if (!strcmp(cmd, "QUIT")) {
        send_line(sock, "OK BYE");
        return -1;
    }
    else {
        send_line(sock, "ERR UNKNOWN_COMMAND");
    }
    return 0;
}

/* ── 연결 핸들러 ──────────────────────────────────────── */
static void *client_thread(void *arg)
{
    int sock = (int)(intptr_t)arg;
    char buf[1024];
    int len = 0;

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
            int r = handle_cmd(sock, buf);
            int consumed = (int)(nl - buf) + 1;
            len -= consumed;
            memmove(buf, nl + 1, (size_t)len);
            buf[len] = '\0';
            if (r < 0) { close(sock); return NULL; }
        }
        if (len >= (int)sizeof(buf) - 1) len = 0;  /* 과도한 라인 폐기 */
    }

    /* 연결 종료 시 이 소켓이 소유한 모드 정리 */
    if (g_cds_sock == sock) g_cds_run = 0;
    if (g_fnd_sock == sock) g_fnd_run = 0;
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
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    /* libdevice.so 는 cwd 상대경로 → 데몬화 전에 절대경로로 고정 */
    char abslib[PATH_MAX] = {0};
    if (!realpath(LIB_PATH, abslib))
        strncpy(abslib, LIB_PATH, sizeof(abslib) - 1);

    daemonize();
    openlog("devserver", LOG_PID, LOG_DAEMON);

    g_lib = dlopen(abslib, RTLD_NOW);
    if (!g_lib) { syslog(LOG_ERR, "dlopen: %s", dlerror()); return 1; }
    bind_symbols();
    if (!dev_init || !dev_cleanup || !led_on || !led_off || !led_bright ||
        !buzzer_tone || !buzzer_off || !cds_read || !fnd_display || !fnd_clear) {
        syslog(LOG_ERR, "dlsym: missing symbol"); return 1;
    }
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
    syslog(LOG_INFO, "devserver listening on port %d", port);

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
    dlclose(g_lib);
    closelog();
    return 0;
}
