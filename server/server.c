/*
 * server.c — TCP 기반 원격 장치 제어 시스템 (HTTP 서버)
 *
 * raw 소켓으로 직접 구현한 경량 HTTP/1.1 서버. 연결마다 pthread.
 * 장치 제어 로직은 libdevice.so 를 dlopen 으로 런타임 로딩한다.
 * CDS/LOG 실시간 값은 SSE(text/event-stream)로 push.
 *
 * 빌드: gcc -o server server.c -lpthread -ldl
 * 실행: ./server 8080   (브라우저: http://<RPi-IP>:8080/)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* ----------------------------- libdevice.so ----------------------------- */
static void *g_lib;
static int  (*dev_init)(void);
static void (*dev_cleanup)(void);
static int  (*led_on)(void);
static int  (*led_off)(void);
static int  (*buzzer_tone)(int);
static int  (*buzzer_off)(void);
static int  (*cds_read)(void);
static int  (*fnd_display)(int);
static int  (*fnd_clear)(void);

static void load_device_lib(void)
{
	g_lib = dlopen("./libdevice.so", RTLD_NOW);
	if (!g_lib) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		exit(1);
	}
#define SYM(fp, name)                                                   \
	do {                                                                \
		*(void **)(&fp) = dlsym(g_lib, name);                           \
		if (!fp) { fprintf(stderr, "dlsym %s: %s\n", name, dlerror());   \
		           exit(1); }                                           \
	} while (0)
	SYM(dev_init,    "device_init");
	SYM(dev_cleanup, "device_cleanup");
	SYM(led_on,      "led_on");
	SYM(led_off,     "led_off");
	SYM(buzzer_tone, "buzzer_tone");
	SYM(buzzer_off,  "buzzer_off");
	SYM(cds_read,    "cds_read");
	SYM(fnd_display, "fnd_display");
	SYM(fnd_clear,   "fnd_clear");
#undef SYM
}

/* ------------------------------- 공유 상태 ------------------------------- */
/* g_lock: GPIO(하드웨어) 접근 + 공유 상태를 함께 보호. sleep 중에는 잡지 않는다. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

typedef enum { LED_OFF, LED_ON, LED_BLINK, LED_CDS } led_mode_t;
static led_mode_t g_led_mode = LED_OFF;

static volatile int g_blink_run, g_cds_run, g_fnd_run, g_buzz_run;
static pthread_t    g_blink_tid, g_cds_tid, g_fnd_tid, g_buzz_tid;

static int          g_cds_value;            /* digitalRead 결과 0/1 */
static const char  *g_cds_state = "DARK";   /* "DARK" | "BRIGHT" */
static int          g_fnd_count;
static int          g_buzz_on;
static int          g_log_on;

/* 로그 링버퍼 (SSE /log/stream 가 커서로 읽는다) */
#define LOG_CAP 128
static char           g_logbuf[LOG_CAP][192];
static unsigned long  g_log_head;           /* 누적 카운트 */

static void msleep(int ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

static void log_add(const char *fmt, ...)
{
	if (!g_log_on)
		return;

	char msg[128];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);

	time_t t = time(NULL);
	struct tm tm;
	char ts[16];
	localtime_r(&t, &tm);
	strftime(ts, sizeof ts, "%H:%M:%S", &tm);

	pthread_mutex_lock(&g_lock);
	snprintf(g_logbuf[g_log_head % LOG_CAP], sizeof g_logbuf[0],
	         "{\"time\":\"%s\",\"level\":\"INFO\",\"msg\":\"%s\"}", ts, msg);
	g_log_head++;
	pthread_mutex_unlock(&g_lock);
}

/* ------------------------------ 모드 스레드 ------------------------------ */
static void *blink_fn(void *arg)
{
	(void)arg;
	int on = 0;
	while (g_blink_run) {
		pthread_mutex_lock(&g_lock);
		if (on) led_off(); else led_on();
		pthread_mutex_unlock(&g_lock);
		on = !on;
		sleep(1);
	}
	pthread_mutex_lock(&g_lock);
	led_off();
	pthread_mutex_unlock(&g_lock);
	return NULL;
}

static void *cds_fn(void *arg)
{
	(void)arg;
	while (g_cds_run) {
		pthread_mutex_lock(&g_lock);
		int v = cds_read();
		if (v) led_on(); else led_off();         /* 밝으면 ON, 어두우면 OFF */
		g_cds_value = v;
		g_cds_state = v ? "BRIGHT" : "DARK";
		pthread_mutex_unlock(&g_lock);
		sleep(1);
	}
	return NULL;
}

static void *fnd_fn(void *arg)
{
	(void)arg;
	while (g_fnd_run) {
		pthread_mutex_lock(&g_lock);
		fnd_display(g_fnd_count);
		g_fnd_count = (g_fnd_count + 1) % 10;     /* 0→9 무한 순환 */
		pthread_mutex_unlock(&g_lock);
		sleep(1);
	}
	return NULL;
}

/* 저장된 계이름 멜로디 — "학교종" 첫 소절 (곡은 변경 가능). {주파수Hz, 길이ms}, 0=쉼표 */
static const int g_melody[][2] = {
	{392,400},{392,400},{440,400},{440,400},{392,400},{392,400},{330,800},
	{392,400},{392,400},{330,400},{330,400},{294,800},
	{0,0}
};

static void *buzz_fn(void *arg)
{
	(void)arg;
	while (g_buzz_run) {
		for (int i = 0; g_melody[i][1] && g_buzz_run; i++) {
			pthread_mutex_lock(&g_lock);
			if (g_melody[i][0]) buzzer_tone(g_melody[i][0]);
			else                buzzer_off();
			pthread_mutex_unlock(&g_lock);
			msleep(g_melody[i][1]);
			pthread_mutex_lock(&g_lock);
			buzzer_off();
			pthread_mutex_unlock(&g_lock);
			msleep(40);
		}
	}
	pthread_mutex_lock(&g_lock);
	buzzer_off();
	pthread_mutex_unlock(&g_lock);
	return NULL;
}

/* ------------------------- 모드 start/stop 헬퍼 -------------------------- */
/* join 을 포함하므로 g_lock 을 잡지 않은 상태에서 호출해야 한다. */
static void stop_blink(void) { if (g_blink_run) { g_blink_run = 0; pthread_join(g_blink_tid, NULL); } }
static void stop_cds(void)   { if (g_cds_run)   { g_cds_run   = 0; pthread_join(g_cds_tid,   NULL); } }

static void stop_fnd(void)
{
	if (!g_fnd_run) return;
	g_fnd_run = 0;
	pthread_join(g_fnd_tid, NULL);
	pthread_mutex_lock(&g_lock);
	fnd_clear();
	g_fnd_count = 0;
	pthread_mutex_unlock(&g_lock);
}

static void stop_buzz(void)
{
	if (!g_buzz_run) return;
	g_buzz_run = 0;
	pthread_join(g_buzz_tid, NULL);
	g_buzz_on = 0;
}

/* LED 를 소유한 자동 모드(blink/cds)를 모두 중단 — LED 모드 배타성 보장 */
static void led_release(void) { stop_blink(); stop_cds(); }

/* ------------------------------ HTTP 응답 ------------------------------- */
static void http_send(int fd, const char *status, const char *ctype, const char *body)
{
	char hdr[256];
	int  blen = body ? (int)strlen(body) : 0;
	int  hlen = snprintf(hdr, sizeof hdr,
		"HTTP/1.1 %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n\r\n",
		status, ctype, blen);
	write(fd, hdr, hlen);
	if (blen) write(fd, body, blen);
}

static void send_json(int fd, const char *status, const char *json)
{
	http_send(fd, status, "application/json", json);
}

static void send_ok(int fd, const char *json) { send_json(fd, "200 OK", json); }

/* GET / : web/index.html 정적 서빙 (없으면 최소 안내 페이지) */
static void serve_index(int fd)
{
	FILE *f = fopen("web/index.html", "rb");
	if (!f) {
		const char *fallback =
			"<!doctype html><meta charset=utf-8><h1>device control</h1>"
			"<p>web/index.html 없음. API: POST /led/on 등, GET /status</p>";
		http_send(fd, "200 OK", "text/html; charset=utf-8", fallback);
		return;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(sz + 1);
	if (!buf) { fclose(f); send_json(fd, "500 Internal Server Error", "{\"ok\":false,\"error\":\"INTERNAL\"}"); return; }
	size_t rd = fread(buf, 1, sz, f);
	fclose(f);

	char hdr[160];
	int hlen = snprintf(hdr, sizeof hdr,
		"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
		"Content-Length: %zu\r\nConnection: close\r\n\r\n", rd);
	write(fd, hdr, hlen);
	write(fd, buf, rd);
	free(buf);
}

/* GET /status */
static void send_status(int fd)
{
	char body[256];
	pthread_mutex_lock(&g_lock);
	const char *led = g_led_mode == LED_ON   ? "on"
	                : g_led_mode == LED_BLINK ? "blink"
	                : g_led_mode == LED_CDS   ? "cds" : "off";
	snprintf(body, sizeof body,
		"{\"led\":\"%s\","
		"\"cds\":{\"active\":%s,\"value\":%d,\"state\":\"%s\"},"
		"\"buzzer\":\"%s\","
		"\"fnd\":{\"active\":%s,\"count\":%d},"
		"\"log\":\"%s\"}",
		led,
		g_cds_run ? "true" : "false", g_cds_value, g_cds_state,
		g_buzz_on ? "on" : "off",
		g_fnd_run ? "true" : "false", g_fnd_count,
		g_log_on ? "on" : "off");
	pthread_mutex_unlock(&g_lock);
	send_ok(fd, body);
}

/* --------------------------------- SSE --------------------------------- */
static void sse_begin(int fd)
{
	const char *h =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/event-stream\r\n"
		"Cache-Control: no-cache\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: keep-alive\r\n\r\n";
	write(fd, h, strlen(h));
}

/* GET /cds/stream : 공유 상태를 1초마다 push (연결 종료 시 write 실패로 탈출) */
static void stream_cds(int fd)
{
	sse_begin(fd);
	for (;;) {
		char ev[96];
		pthread_mutex_lock(&g_lock);
		int n = snprintf(ev, sizeof ev,
			"event: cds\ndata: {\"value\":%d,\"state\":\"%s\"}\n\n",
			g_cds_value, g_cds_state);
		pthread_mutex_unlock(&g_lock);
		if (write(fd, ev, n) <= 0)
			break;
		sleep(1);
	}
}

/* GET /log/stream : 링버퍼를 커서로 따라가며 새 로그를 push */
static void stream_log(int fd)
{
	sse_begin(fd);
	unsigned long cur;
	pthread_mutex_lock(&g_lock);
	cur = g_log_head;                 /* 구독 시점 이후 로그만 전송 */
	pthread_mutex_unlock(&g_lock);

	for (;;) {
		char line[256];
		int have = 0;
		pthread_mutex_lock(&g_lock);
		if (cur < g_log_head) {
			if (g_log_head - cur > LOG_CAP)      /* 밀린 경우 최신 쪽으로 점프 */
				cur = g_log_head - LOG_CAP;
			snprintf(line, sizeof line, "event: log\ndata: %s\n\n",
			         g_logbuf[cur % LOG_CAP]);
			cur++;
			have = 1;
		}
		pthread_mutex_unlock(&g_lock);

		if (have) {
			if (write(fd, line, strlen(line)) <= 0)
				break;
		} else {
			msleep(200);
		}
	}
}

/* ------------------------------- 라우팅 -------------------------------- */
/* 반환: 1=일반 응답 후 연결 종료, 0=SSE 등으로 핸들러가 직접 마감 */
static void route(int fd, const char *method, const char *path)
{
	int is_post = strcmp(method, "POST") == 0;
	int is_get  = strcmp(method, "GET")  == 0;

	/* 브라우저 fetch preflight */
	if (strcmp(method, "OPTIONS") == 0) {
		http_send(fd, "204 No Content", "text/plain", NULL);
		return;
	}

	/* ---- GET 조회/스트림/UI ---- */
	if (is_get && strcmp(path, "/") == 0)            { serve_index(fd); return; }
	if (is_get && strcmp(path, "/status") == 0)      { send_status(fd); return; }
	if (is_get && strcmp(path, "/cds/stream") == 0)  { stream_cds(fd);  return; }
	if (is_get && strcmp(path, "/log/stream") == 0)  { stream_log(fd);  return; }

	/* ---- POST 제어 ---- */
	if (is_post && strcmp(path, "/led/on") == 0) {
		led_release();
		pthread_mutex_lock(&g_lock); led_on(); g_led_mode = LED_ON; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"led\":\"on\"}"); return;
	}
	if (is_post && strcmp(path, "/led/off") == 0) {
		led_release();
		pthread_mutex_lock(&g_lock); led_off(); g_led_mode = LED_OFF; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"led\":\"off\"}"); return;
	}
	if (is_post && strcmp(path, "/led/blink/on") == 0) {
		led_release();
		g_blink_run = 1; pthread_create(&g_blink_tid, NULL, blink_fn, NULL);
		pthread_mutex_lock(&g_lock); g_led_mode = LED_BLINK; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"led\":\"blink\"}"); return;
	}
	if (is_post && strcmp(path, "/led/blink/off") == 0) {
		stop_blink();
		pthread_mutex_lock(&g_lock); led_off(); g_led_mode = LED_OFF; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"led\":\"off\"}"); return;
	}
	if (is_post && strcmp(path, "/cds/on") == 0) {
		led_release();
		g_cds_run = 1; pthread_create(&g_cds_tid, NULL, cds_fn, NULL);
		pthread_mutex_lock(&g_lock); g_led_mode = LED_CDS; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"cds\":\"on\"}"); return;
	}
	if (is_post && strcmp(path, "/cds/off") == 0) {
		stop_cds();
		pthread_mutex_lock(&g_lock); led_off(); g_led_mode = LED_OFF; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"cds\":\"off\"}"); return;
	}
	if (is_post && strcmp(path, "/buzzer/on") == 0) {
		if (!g_buzz_run) { g_buzz_run = 1; g_buzz_on = 1; pthread_create(&g_buzz_tid, NULL, buzz_fn, NULL); }
		send_ok(fd, "{\"ok\":true,\"buzzer\":\"on\"}"); return;
	}
	if (is_post && strcmp(path, "/buzzer/off") == 0) {
		stop_buzz();
		send_ok(fd, "{\"ok\":true,\"buzzer\":\"off\"}"); return;
	}
	if (is_post && strcmp(path, "/fnd/on") == 0) {
		if (!g_fnd_run) { g_fnd_run = 1; pthread_create(&g_fnd_tid, NULL, fnd_fn, NULL); }
		send_ok(fd, "{\"ok\":true,\"fnd\":\"on\"}"); return;
	}
	if (is_post && strcmp(path, "/fnd/off") == 0) {
		stop_fnd();
		send_ok(fd, "{\"ok\":true,\"fnd\":\"off\"}"); return;
	}
	if (is_post && strcmp(path, "/log/on") == 0) {
		pthread_mutex_lock(&g_lock); g_log_on = 1; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"log\":\"on\"}"); return;
	}
	if (is_post && strcmp(path, "/log/off") == 0) {
		pthread_mutex_lock(&g_lock); g_log_on = 0; pthread_mutex_unlock(&g_lock);
		send_ok(fd, "{\"ok\":true,\"log\":\"off\"}"); return;
	}

	/* ---- 매칭 실패 ---- */
	if (is_post || is_get)
		send_json(fd, "404 Not Found", "{\"ok\":false,\"error\":\"UNKNOWN_ENDPOINT\"}");
	else
		send_json(fd, "405 Method Not Allowed", "{\"ok\":false,\"error\":\"METHOD_NOT_ALLOWED\"}");
}

/* --------------------------- 연결 핸들러 스레드 --------------------------- */
static void *client_fn(void *arg)
{
	int fd = *(int *)arg;
	free(arg);

	char buf[4096];
	int n = recv(fd, buf, sizeof buf - 1, 0);
	if (n <= 0) { close(fd); return NULL; }
	buf[n] = '\0';

	char method[8] = "", path[256] = "";
	if (sscanf(buf, "%7s %255s", method, path) == 2) {
		log_add("%s %s", method, path);
		route(fd, method, path);
	}
	close(fd);
	return NULL;
}

/* --------------------------------- main -------------------------------- */
int main(int argc, char **argv)
{
	int port = (argc > 1) ? atoi(argv[1]) : 8080;

	signal(SIGPIPE, SIG_IGN);          /* 끊긴 SSE 연결에 write 시 종료 방지 */
	load_device_lib();

	if (dev_init() < 0) {
		fprintf(stderr, "device_init 실패 — 서버 기동 중단\n");
		exit(1);
	}

	int srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) { perror("socket"); exit(1); }
	int yes = 1;
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

	struct sockaddr_in addr = {0};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(port);

	if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); exit(1); }
	if (listen(srv, 16) < 0) { perror("listen"); exit(1); }
	printf("HTTP 서버 시작: http://<RPi-IP>:%d/\n", port);

	for (;;) {
		int *c = malloc(sizeof(int));
		if (!c) continue;
		*c = accept(srv, NULL, NULL);
		if (*c < 0) { free(c); continue; }
		pthread_t t;
		pthread_create(&t, NULL, client_fn, c);
		pthread_detach(t);
	}

	dev_cleanup();
	dlclose(g_lib);
	return 0;
}
