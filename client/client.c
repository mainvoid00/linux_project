/* TCP 원격 장치 제어 클라이언트 (Ubuntu)
 *   - 서버에 접속해 표준입력 명령을 전송, 수신 스레드로 응답/이벤트 출력
 *   - SIGINT(Ctrl+C) 시그널 처리: 강제 종료되지 않고 정상 종료(QUIT 전송)
 *
 * 빌드: gcc -Wall -Wextra -o devclient client/client.c -lpthread
 * 실행: ./devclient <서버IP> [port]   (기본 5000)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 5000

static int g_sock = -1;
static volatile sig_atomic_t g_stop = 0;

/* SIGINT 핸들러: 즉시 죽지 않고 종료 플래그만 설정 (강제 종료 방지) */
static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* 서버 응답/이벤트 수신 스레드 */
static void *recv_thread(void *arg)
{
    (void)arg;
    char buf[1024];
    for (;;) {
        int n = recv(g_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("\n[서버 연결 종료]\n");
            g_stop = 1;
            break;
        }
        buf[n] = '\0';
        fputs(buf, stdout);
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <server-ip> [port]\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    /* Ctrl+C 로 강제 종료되지 않도록 시그널 처리 */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid ip: %s\n", ip);
        return 1;
    }
    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("연결됨 %s:%d  (명령 예: LED ON / LED MID / CDS ON / FND 9 / BUZZER ON / QUIT)\n", ip, port);
    printf("Ctrl+C 또는 QUIT 로 종료\n");

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, NULL);

    char line[512];
    while (!g_stop) {
        if (!fgets(line, sizeof(line), stdin)) {
            /* SIGINT 로 fgets 인터럽트되면 EINTR → 정상 종료 경로로 */
            if (g_stop) break;
            continue;
        }
        if (send(g_sock, line, strlen(line), 0) < 0) {
            perror("send");
            break;
        }
    }

    /* 정상 종료: 서버에 QUIT 통보 후 소켓 닫기 */
    send(g_sock, "QUIT\n", 5, 0);
    shutdown(g_sock, SHUT_RDWR);
    close(g_sock);
    pthread_join(tid, NULL);
    printf("종료합니다.\n");
    return 0;
}
