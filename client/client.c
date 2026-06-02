/* TCP 원격 장치 제어 클라이언트 (Ubuntu)
 *   - 번호식 계층 메뉴로 명령 선택 → 서버에 텍스트 프로토콜 전송
 *   - 수신 스레드로 응답(OK/ERR)·비동기 이벤트(EVT) 출력
 *   - 시그널 처리: SIGINT(Ctrl+C) 에서만 종료(QUIT 전송), 그 외 모든 시그널은 무시
 *
 * 빌드: gcc -Wall -Wextra -o devclient client/client.c -lpthread
 * 실행: ./devclient <서버IP> [port]   (기본 5000)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
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

/* SIGINT 에서만 종료, 그 외 모든 시그널은 무시 (KILL/STOP 은 변경 불가) */
static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;          /* SA_RESTART 미설정 → fgets 가 EINTR 로 깨어남 */
    sigaction(SIGINT, &sa, NULL);

    for (int s = 1; s < NSIG; s++) {
        if (s == SIGINT || s == SIGKILL || s == SIGSTOP)
            continue;
        signal(s, SIG_IGN);
    }
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
        printf("\r[서버] %s", buf);     /* 응답/이벤트 출력 */
        fflush(stdout);
    }
    return NULL;
}

/* 명령 1줄 전송 ("\n" 자동 부착) */
static void send_cmd(const char *s)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%s\n", s);
    if (n > 0 && send(g_sock, buf, (size_t)n, 0) < 0)
        g_stop = 1;
    usleep(150 * 1000);                 /* 서버 응답이 먼저 출력되도록 잠깐 양보 */
}

/* 한 줄 입력 → 정수. 실패/인터럽트 시 -1 */
static int read_int(const char *prompt)
{
    char line[64];
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
        return -1;                      /* EOF 또는 SIGINT(EINTR) */
    return atoi(line);
}

/* ── 서브 메뉴 ────────────────────────────────────────── */
static void menu_led(void)
{
    printf("\n-- LED --\n"
           " 1) 켜기(ON)   2) 끄기(OFF)\n"
           " 3) 밝게(HIGH) 4) 중간(MID)  5) 약하게(LOW)\n"
           " 0) 뒤로\n");
    switch (read_int("선택> ")) {
        case 1: send_cmd("LED ON");   break;
        case 2: send_cmd("LED OFF");  break;
        case 3: send_cmd("LED HIGH"); break;
        case 4: send_cmd("LED MID");  break;
        case 5: send_cmd("LED LOW");  break;
        default: break;
    }
}

static void menu_buzzer(void)
{
    printf("\n-- 부저(Buzzer) --\n"
           " 1) 멜로디 켜기(ON)  2) 끄기(OFF)\n"
           " 0) 뒤로\n");
    switch (read_int("선택> ")) {
        case 1: send_cmd("BUZZER ON");  break;
        case 2: send_cmd("BUZZER OFF"); break;
        default: break;
    }
}

static void menu_cds(void)
{
    printf("\n-- 조도센서(CDS, PCF8591) --\n"
           " 1) 자동연동 켜기(ON)  2) 자동연동 끄기(OFF)\n"
           " 3) 현재값 읽기(READ)  4) 임계값 설정(THRESHOLD)\n"
           " 0) 뒤로\n");
    switch (read_int("선택> ")) {
        case 1: send_cmd("CDS ON");   break;
        case 2: send_cmd("CDS OFF");  break;
        case 3: send_cmd("CDS READ"); break;
        case 4: {
            int t = read_int("임계값(0~255)> ");
            if (t >= 0 && t <= 255) {
                char cmd[48];
                snprintf(cmd, sizeof(cmd), "CDS THRESHOLD %d", t);
                send_cmd(cmd);
            } else printf("범위(0~255)를 벗어났습니다.\n");
            break;
        }
        default: break;
    }
}

static void menu_fnd(void)
{
    printf("\n-- 7세그먼트(FND) --\n"
           " 1) 카운트다운 시작  2) 정지(STOP)\n"
           " 0) 뒤로\n");
    switch (read_int("선택> ")) {
        case 1: {
            int n = read_int("숫자(0~9)> ");
            if (n >= 0 && n <= 9) {
                char cmd[16];
                snprintf(cmd, sizeof(cmd), "FND %d", n);
                send_cmd(cmd);
            } else printf("범위(0~9)를 벗어났습니다.\n");
            break;
        }
        case 2: send_cmd("FND STOP"); break;
        default: break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <server-ip> [port]\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    setup_signals();

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
    printf("연결됨 %s:%d  (종료: 메뉴 0 또는 Ctrl+C)\n", ip, port);

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, NULL);

    while (!g_stop) {
        printf("\n====== 원격 장치 제어 ======\n"
               " 1) LED\n"
               " 2) 부저(Buzzer)\n"
               " 3) 조도센서(CDS)\n"
               " 4) 7세그먼트(FND)\n"
               " 5) 상태(STATUS)\n"
               " 0) 종료(QUIT)\n"
               "============================\n");
        int sel = read_int("선택> ");
        if (sel < 0) break;             /* EOF/SIGINT → 종료 */
        switch (sel) {
            case 1: menu_led();    break;
            case 2: menu_buzzer(); break;
            case 3: menu_cds();    break;
            case 4: menu_fnd();    break;
            case 5: send_cmd("STATUS"); break;
            case 0: g_stop = 1;    break;
            default: printf("잘못된 선택입니다.\n"); break;
        }
    }

    /* 정상 종료: 서버에 QUIT 통보 후 소켓 닫기 */
    send(g_sock, "QUIT\n", 5, 0);
    shutdown(g_sock, SHUT_RDWR);
    close(g_sock);
    pthread_join(tid, NULL);
    printf("\n종료합니다.\n");
    return 0;
}
