/* 서버 로그 링버퍼 + HTML 실시간 로그 뷰어 (2번째 포트, 추가기능)
 *   dlog(): syslog 기록 + 링버퍼 적재 → 브라우저가 /log 를 1초마다 폴링 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "server.h"

#define LOG_RING 200            /* 최근 로그 보관 줄 수 */
#define LOG_LINE 256

static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static char g_log[LOG_RING][LOG_LINE];
static int  g_log_head, g_log_count;

/* syslog 기록 + 링버퍼 적재(시각 포함) — HTML 뷰어가 폴링 */
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
    pthread_mutex_unlock(&g_log_mtx);
}

static const char LOG_PAGE[] =
    "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
    "<!doctype html><html><head><meta charset=utf-8><title>devserver log</title>"
    "<style>body{background:#111;color:#0f0;font-family:monospace;margin:0;padding:12px}"
    "h1{color:#fff;font-size:16px}pre{white-space:pre-wrap;word-break:break-all}</style></head>"
    "<body><h1>devserver 실시간 로그</h1><pre id=log>로딩...</pre>"
    "<script>async function u(){try{let r=await fetch('/log');let t=await r.text();"
    "let e=document.getElementById('log');e.textContent=t;"
    "window.scrollTo(0,document.body.scrollHeight);}catch(x){}}"
    "setInterval(u,1000);u();</script></body></html>";

static void log_serve(int c)
{
    char req[1024];
    int n = recv(c, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(c); return; }
    req[n] = '\0';

    if (strncmp(req, "GET /log", 8) == 0) {       /* 로그 본문(텍스트) */
        static char body[LOG_RING * LOG_LINE];
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
        send(c, hdr, strlen(hdr), MSG_NOSIGNAL);
        send(c, body, off, MSG_NOSIGNAL);
    } else {                                       /* 그 외 → HTML 페이지 */
        send(c, LOG_PAGE, sizeof(LOG_PAGE) - 1, MSG_NOSIGNAL);
    }
    close(c);
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
    if (listen(srv, 4) < 0) { close(srv); return NULL; }
    dlog(LOG_INFO, "로그 뷰어 시작: http://<RPi-IP>:%d", port);

    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;
        log_serve(c);
    }
    return NULL;
}
