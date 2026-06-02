/* 텍스트 라인 프로토콜 명령 파싱/처리 + 응답 송신 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include "server.h"

/* 응답/이벤트 1줄 송신 ("\n" 자동 부착) */
void send_line(int sock, const char *s)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s\n", s);
    if (n > 0)
        send(sock, buf, (size_t)n, MSG_NOSIGNAL);
}

/* 명령 처리: 0=계속, -1=연결 종료 */
int handle_cmd(int sock, char *line)
{
    char *cmd = strtok(line, " \t");
    char *a1  = strtok(NULL, " \t");
    pthread_t tid;

    if (!cmd)
        return 0;

    if (!strcmp(cmd, "LED")) {
        if (!a1) { send_line(sock, "ERR INVALID_ARG"); return 0; }
        g_cds_run = 0;   /* 수동 LED 제어 → 조도 자동 모드 해제 (LED 모드 배타성) */
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
            pthread_mutex_lock(&g_state);
            if (!g_buz_run) { g_buz_run = 1; pthread_create(&tid, NULL, buzzer_thread, NULL); pthread_detach(tid); }
            pthread_mutex_unlock(&g_state);
            send_line(sock, "OK BUZZER ON");
        } else if (a1 && !strcmp(a1, "OFF")) {
            g_buz_run = 0;
            send_line(sock, "OK BUZZER OFF");
        } else send_line(sock, "ERR INVALID_ARG");
    }
    else if (!strcmp(cmd, "CDS")) {
        if (a1 && !strcmp(a1, "ON")) {
            pthread_mutex_lock(&g_state);
            g_cds_run = 0; usleep(1100*1000);          /* 기존 모드 정리 */
            g_cds_sock = sock; g_cds_run = 1;
            pthread_create(&tid, NULL, cds_thread, (void *)(intptr_t)sock);
            pthread_detach(tid);
            pthread_mutex_unlock(&g_state);
            send_line(sock, "OK CDS ON");
        } else if (a1 && !strcmp(a1, "OFF")) {
            g_cds_run = 0;
            send_line(sock, "OK CDS OFF");
        } else if (a1 && !strcmp(a1, "READ")) {
            int v, th; char msg[80];
            pthread_mutex_lock(&g_dev);
            v = cds_read(); th = cds_get_threshold();
            pthread_mutex_unlock(&g_dev);
            snprintf(msg, sizeof(msg), "OK CDS %d %s TH %d", v, v >= th ? "DARK" : "LIGHT", th);
            send_line(sock, msg);
        } else if (a1 && !strcmp(a1, "THRESHOLD")) {
            char *a2 = strtok(NULL, " \t");
            int t = a2 ? atoi(a2) : -1;
            pthread_mutex_lock(&g_dev);
            int r = cds_set_threshold(t);
            pthread_mutex_unlock(&g_dev);
            if (r == 0) {
                char msg[48];
                snprintf(msg, sizeof(msg), "OK CDS THRESHOLD %d", t);
                send_line(sock, msg);
            } else send_line(sock, "ERR INVALID_ARG");
        } else send_line(sock, "ERR INVALID_ARG");
    }
    else if (!strcmp(cmd, "FND")) {
        if (a1 && !strcmp(a1, "STOP")) {
            g_fnd_run = 0;
            pthread_mutex_lock(&g_dev); fnd_clear(); pthread_mutex_unlock(&g_dev);
            send_line(sock, "OK FND STOP");
        } else if (a1 && a1[0] >= '0' && a1[0] <= '9' && a1[1] == '\0') {
            pthread_mutex_lock(&g_state);
            g_fnd_run = 0; usleep(100*1000);
            g_fnd_sock = sock; g_fnd_run = 1;
            pthread_create(&tid, NULL, fnd_thread, (void *)(intptr_t)(a1[0]-'0'));
            pthread_detach(tid);
            pthread_mutex_unlock(&g_state);
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
