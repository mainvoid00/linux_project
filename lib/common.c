/* 공통 초기화/정리 — wiringPi 셋업 후 각 장치 init 호출 */
#include <wiringPi.h>
#include "device.h"

int device_init(void)
{
    if (wiringPiSetup() == -1)
        return -1;

    if (led_init()    != 0) return -1;
    if (buzzer_init() != 0) return -1;
    if (cds_init()    != 0) return -1;
    if (fnd_init()    != 0) return -1;

    return 0;
}

void device_cleanup(void)
{
    led_off();
    buzzer_off();
    fnd_clear();
}
