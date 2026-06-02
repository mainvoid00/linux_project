#include <wiringPi.h>
#include <softPwm.h>
#include <softTone.h>
#include "device.h"

/* ── GPIO 핀 번호 (wiringPi 기준) ──────────────────────────────
 * 실제 결선에 맞춰 반드시 수정할 것. (미확정 — 결선 확인 필요) */
#define LED_PIN     1   /* softPwm 으로 밝기 제어 */
#define BUZZER_PIN  2
#define CDS_PIN     0

/* 7세그먼트 = SN74LS47 BCD 디코더 구동 (공통 애노드 디스플레이)
 * BCD 입력 A,B,C,D (A=LSB) 4핀만 제어 → 디코더가 a~g 세그먼트 생성 */
static const int FND_BCD[4] = { 21, 22, 23, 24 };  /* A, B, C, D */
#define FND_BLANK  25   /* SN74LS47 BI(pin4): HIGH=표시, LOW=소등 */

/* 밝기 레벨별 PWM 듀티(0~100): off / 최저 / 중간 / 최대 */
static const int LED_LEVEL[4] = { 0, 20, 55, 100 };

int device_init(void)
{
    int i;

    if (wiringPiSetup() == -1)
        return -1;

    /* LED: softPwm 채널 생성 (범위 0~100) */
    if (softPwmCreate(LED_PIN, 0, 100) != 0)
        return -1;

    pinMode(CDS_PIN, INPUT);

    for (i = 0; i < 4; i++)
        pinMode(FND_BCD[i], OUTPUT);
    pinMode(FND_BLANK, OUTPUT);
    digitalWrite(FND_BLANK, LOW);  /* 초기 소등 */

    /* 압전(passive) 부저: softTone 채널 생성 */
    if (softToneCreate(BUZZER_PIN) != 0)
        return -1;

    return 0;
}

void device_cleanup(void)
{
    led_off();
    buzzer_off();
    fnd_clear();
}

int led_on(void)
{
    softPwmWrite(LED_PIN, 100);
    return 0;
}

int led_off(void)
{
    softPwmWrite(LED_PIN, 0);
    return 0;
}

int led_bright(int level)
{
    if (level < 0 || level > 3)
        return -1;
    softPwmWrite(LED_PIN, LED_LEVEL[level]);
    return 0;
}

int buzzer_tone(int hz)
{
    if (hz < 0)
        return -1;
    softToneWrite(BUZZER_PIN, hz);  /* hz=0 이면 무음 */
    return 0;
}

int buzzer_off(void)
{
    softToneWrite(BUZZER_PIN, 0);
    return 0;
}

int cds_read(void)
{
    /* 0=어두움(빛 없음), 1=밝음 (결선 극성에 따라 반전될 수 있음 — 확인 필요) */
    return digitalRead(CDS_PIN);
}

int fnd_display(int num)
{
    int i;

    if (num < 0 || num > 9)
        return -1;

    /* num 을 4비트 BCD 로 출력 (A=LSB) → SN74LS47 이 세그먼트 디코딩 */
    for (i = 0; i < 4; i++)
        digitalWrite(FND_BCD[i], (num >> i) & 1);
    digitalWrite(FND_BLANK, HIGH);  /* 표시 활성 */

    return 0;
}

int fnd_clear(void)
{
    digitalWrite(FND_BLANK, LOW);  /* BI=LOW → 전 세그먼트 소등 */
    return 0;
}
