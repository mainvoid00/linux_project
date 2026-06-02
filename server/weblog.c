/* 서버 로그 링버퍼 + 실시간 로그 뷰어 (2번째 포트, 추가기능)
 *   - SSE(text/event-stream)로 로그를 브라우저에 푸시 (dlog → condvar broadcast)
 *   - GET /        : index.html 파일 서빙 (없으면 내장 폴백 페이지)
 *   - GET /events  : SSE 스트림 (이벤트 발생 시에만 전송 + 15초 하트비트)
 *   - GET /log     : 전체 버퍼 1회 응답 (수동 "다시 불러오기"·폴백) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "server.h"

#define LOG_RING 200            /* 최근 로그 보관 줄 수 */
#define LOG_LINE 256

static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_log_cv  = PTHREAD_COND_INITIALIZER;
static char g_log[LOG_RING][LOG_LINE];
static int  g_log_head, g_log_count;
static unsigned long g_log_seq;          /* 누적 로그 시퀀스(단조 증가) */

static char g_index_path[PATH_MAX];      /* index.html 절대경로 (main 이 설정) */

/* main 이 데몬화 전에 realpath 로 고정한 index.html 경로를 전달 */
void weblog_set_index(const char *path)
{
    if (path) {
        strncpy(g_index_path, path, sizeof(g_index_path) - 1);
        g_index_path[sizeof(g_index_path) - 1] = '\0';
    }
}

/* syslog 기록 + 링버퍼 적재(시각 포함) + SSE 스트림 깨우기 */
void dlog(int level, const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    syslog(level, "%s", line);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    pthread_mutex_lock(&g_log_mtx);
    snprintf(g_log[g_log_head], LOG_LINE, "%02d:%02d:%02d  %s",
             tm.tm_hour, tm.tm_min, tm.tm_sec, line);
    g_log_head = (g_log_head + 1) % LOG_RING;
    if (g_log_count < LOG_RING) g_log_count++;
    g_log_seq++;
    pthread_cond_broadcast(&g_log_cv);   /* 대기 중인 SSE 스트림 깨움 */
    pthread_mutex_unlock(&g_log_mtx);
}

/* ── HTTP 유틸 ───────────────────────────────────────── */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

/* index.html 미배포 시 사용하는 최소 내장 페이지 (SSE 동일 동작) */
static const char FALLBACK_PAGE[] =
    "<!doctype html><meta charset=utf-8><title>devserver log</title>"
    "<body style='background:#111;color:#0f0;font-family:monospace;padding:12px'>"
    "<h3>devserver 실시간 로그 (내장 폴백 — index.html 없음)</h3><pre id=l></pre>"
    "<script>let l=document.getElementById('l');"
    "let e=new EventSource('/events');e.onmessage=m=>{l.textContent+=m.data+'\\n';"
    "scrollTo(0,document.body.scrollHeight)};</script>";

static void serve_index(int fd)
{
    const char *hdr =
        "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n";
    if (send_all(fd, hdr, strlen(hdr)) < 0) return;

    int f = (g_index_path[0]) ? open(g_index_path, O_RDONLY) : -1;
    if (f < 0) {                                  /* 파일 없음 → 폴백 */
        send_all(fd, FALLBACK_PAGE, sizeof(FALLBACK_PAGE) - 1);
        return;
    }
    char fb[4096];
    ssize_t r;
    while ((r = read(f, fb, sizeof(fb))) > 0)
        if (send_all(fd, fb, (size_t)r) < 0) break;
    close(f);
}

static void serve_log(int fd)
{
    char body[LOG_RING * LOG_LINE];
    size_t off = 0;
    pthread_mutex_lock(&g_log_mtx);
    int start = (g_log_count < LOG_RING) ? 0 : g_log_head;
    for (int i = 0; i < g_log_count; i++) {
        int idx = (start + i) % LOG_RING;
        int w = snprintf(body + off, sizeof(body) - off, "%s\n", g_log[idx]);
        if (w < 0) break;
        off += (size_t)w;
        if (off >= sizeof(body)) { off = sizeof(body); break; }
    }
    pthread_mutex_unlock(&g_log_mtx);

    const char *hdr =
        "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Connection: close\r\n\r\n";
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, body, off);
}

/* SSE: 로그가 생길 때마다 data: 라인 푸시. 변화 없으면 15초 하트비트로 끊김 감지 */
static void serve_events(int fd)
{
    const char *hdr =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n\r\n";
    if (send_all(fd, hdr, strlen(hdr)) < 0) return;

    unsigned long last;
    pthread_mutex_lock(&g_log_mtx);
    last = (g_log_seq > (unsigned long)LOG_RING) ? g_log_seq - LOG_RING : 0;
    pthread_mutex_unlock(&g_log_mtx);

    for (;;) {
        char out[LOG_RING * (LOG_LINE + 8)];
        size_t off = 0;

        pthread_mutex_lock(&g_log_mtx);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 15;                          /* 하트비트 주기 */
        while (g_log_seq == last) {
            if (pthread_cond_timedwait(&g_log_cv, &g_log_mtx, &ts) == ETIMEDOUT)
                break;
        }
        /* 클라이언트가 너무 뒤처져 버퍼가 덮였으면 가장 오래된 것부터 */
        unsigned long startseq = last + 1;
        if (g_log_count > 0) {
            unsigned long oldest = g_log_seq - (unsigned long)g_log_count + 1;
            if (startseq < oldest)
                startseq = oldest;
        }
        for (unsigned long s = startseq; s <= g_log_seq; s++) {
            int idx = (int)((s - 1) % LOG_RING);
            int w = snprintf(out + off, sizeof(out) - off, "data: %s\n\n", g_log[idx]);
            if (w < 0) break;
            off += (size_t)w;
            if (off >= sizeof(out)) { off = sizeof(out); break; }
        }
        last = g_log_seq;
        pthread_mutex_unlock(&g_log_mtx);

        if (off == 0) {                           /* 변화 없음 → 하트비트(주석) */
            if (send_all(fd, ": ping\n\n", 8) < 0) break;
        } else {
            if (send_all(fd, out, off) < 0) break; /* 전송 실패 = 클라이언트 끊김 */
        }
    }
}

/* 연결 1개 처리 (연결당 스레드 — SSE 스트림이 다른 요청을 막지 않도록) */
static void *conn_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char req[1024];
    int n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(fd); return NULL; }
    req[n] = '\0';

    if      (strncmp(req, "GET /events", 11) == 0) serve_events(fd);
    else if (strncmp(req, "GET /log", 8) == 0)     serve_log(fd);
    else                                           serve_index(fd);

    close(fd);
    return NULL;
}

void *log_server_thread(void *arg)
{
    int port = (int)(intptr_t)arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return NULL;
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        dlog(LOG_ERR, "로그뷰어 bind 실패: port %d", port);
        close(srv);
        return NULL;
    }
    if (listen(srv, 8) < 0) { close(srv); return NULL; }
    dlog(LOG_INFO, "로그 뷰어 시작: http://<RPi-IP>:%d", port);

    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;
        pthread_t t;
        if (pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)c) != 0)
            close(c);
        else
            pthread_detach(t);
    }
    return NULL;
}
