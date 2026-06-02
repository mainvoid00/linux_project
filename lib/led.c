/* LED — softPwm 으로 on/off 및 밝기 3단계 제어 */
#include <wiringPi.h>
#include <softPwm.h>
#include "device.h"

#define LED_PIN  1   /* wiringPi 핀 번호 (실 결선에 맞춰 수정) */

/* 밝기 레벨별 PWM 듀티(0~100): off / 최저 / 중간 / 최대 */
static const int LED_LEVEL[4] = { 0, 20, 55, 100 };

int led_init(void)
{
    return (softPwmCreate(LED_PIN, 0, 100) != 0) ? -1 : 0;
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
