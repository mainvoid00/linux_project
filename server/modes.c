/* 장치 모드 스레드 — CDS 자동연동 / FND 카운트다운 / BUZZER 멜로디
 * 공유 장치 뮤텍스와 모드 상태도 여기서 정의 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "server.h"

/* GPIO/장치는 공유 자원 → mutex 로 보호 (다중 클라이언트 동시 접근 직렬화) */
pthread_mutex_t g_dev = PTHREAD_MUTEX_INITIALIZER;
/* 모드 상태 전이(start/stop, 소유 소켓) 보호 */
pthread_mutex_t g_state = PTHREAD_MUTEX_INITIALIZER;

/* 한 시점에 cds/fnd/buzzer 각 1개 */
volatile int g_cds_run, g_fnd_run, g_buz_run;
int g_cds_sock = -1, g_fnd_sock = -1;

/* 저장된 멜로디(계이름 주파수 Hz, 지속 ms): 도레미파솔라시도 */
static const int MELODY[][2] = {
    {262,300},{294,300},{330,300},{349,300},
    {392,300},{440,300},{494,300},{523,500},
};
#define MELODY_LEN ((int)(sizeof(MELODY)/sizeof(MELODY[0])))

/* 조도 자동 연동: 매초 조도 읽어 LED 자동제어(빛 없으면 ON) + 클라이언트로 값 전송 */
void *cds_thread(void *arg)
{
    int sock = (int)(intptr_t)arg;
    while (g_cds_run) {
        int v, th, dark;
        pthread_mutex_lock(&g_dev);
        v  = cds_read();                    /* AIN0 아날로그 0~255 */
        th = cds_get_threshold();
        dark = (v >= th);                   /* 임계값 이상 = 어두움 (극성 실HW 확정) */
        if (dark) led_on();                 /* 빛 없음(어두움) → LED ON */
        else      led_off();                /* 빛 있음(밝음)   → LED OFF */
        pthread_mutex_unlock(&g_dev);

        char msg[64];
        snprintf(msg, sizeof(msg), "EVT CDS %d %s", v, dark ? "DARK" : "LIGHT");
        send_line(sock, msg);
        sleep(1);
    }
    return NULL;
}

/* 7세그 카운트다운: n→0 매초 -1 표시, 0 도달 시 부저 1초 */
void *fnd_thread(void *arg)
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
void *buzzer_thread(void *arg)
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
