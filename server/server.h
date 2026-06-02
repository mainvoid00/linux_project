#ifndef SERVER_H
#define SERVER_H

/* devserver 내부 공유 인터페이스 (모듈 간 전역/프로토타입)
 *   binding.c  : libdevice.so dlopen/dlsym 함수 포인터
 *   modes.c    : 장치 뮤텍스 + 모드 상태 + 모드 스레드
 *   command.c  : 텍스트 명령 파싱/처리
 *   weblog.c   : 로그 링버퍼 + HTML 실시간 로그 뷰어
 *   main.c     : 데몬화 + 소켓 accept 루프 */
#include <pthread.h>
#include <syslog.h>

/* ── binding.c: libdevice.so 함수 포인터 ─────────────── */
extern int  (*dev_init)(void);
extern void (*dev_cleanup)(void);
extern int  (*led_on)(void);
extern int  (*led_off)(void);
extern int  (*led_bright)(int);
extern int  (*buzzer_tone)(int);
extern int  (*buzzer_off)(void);
extern int  (*cds_read)(void);
extern int  (*cds_set_threshold)(int);
extern int  (*cds_get_threshold)(void);
extern int  (*fnd_display)(int);
extern int  (*fnd_clear)(void);

int  lib_load(const char *abspath);   /* dlopen + 심볼 바인딩/검증. 0/-1 */
void lib_unload(void);

/* ── modes.c: 공유 장치 뮤텍스 + 모드 상태 ───────────── */
extern pthread_mutex_t g_dev;     /* GPIO/장치 직렬화 (다중 클라이언트) */
extern pthread_mutex_t g_state;   /* 모드 전이(start/stop·소유 소켓) 보호 */
extern volatile int g_cds_run, g_fnd_run, g_buz_run;
extern int g_cds_sock, g_fnd_sock;

void *cds_thread(void *arg);
void *fnd_thread(void *arg);
void *buzzer_thread(void *arg);

/* ── command.c ───────────────────────────────────────── */
void send_line(int sock, const char *s);
int  handle_cmd(int sock, char *line);   /* 0=계속, -1=연결 종료 */

/* ── weblog.c ────────────────────────────────────────── */
void  dlog(int level, const char *fmt, ...)
      __attribute__((format(printf, 2, 3)));
void  weblog_set_index(const char *path); /* index.html 절대경로 설정 */
void *log_server_thread(void *arg);       /* arg=(intptr_t)log_port */

#endif /* SERVER_H */
