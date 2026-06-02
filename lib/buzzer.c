/* 부저 — 압전(passive) 부저, softTone 으로 멜로디 재생 */
#include <wiringPi.h>
#include <softTone.h>
#include "device.h"

#define BUZZER_PIN  2   /* wiringPi 핀 번호 (실 결선에 맞춰 수정) */

int buzzer_init(void)
{
    return (softToneCreate(BUZZER_PIN) != 0) ? -1 : 0;
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
