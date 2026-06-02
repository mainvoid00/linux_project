/* 7세그먼트 = SN74LS47 BCD 디코더 구동 (공통 애노드 디스플레이)
 * BCD 입력 A,B,C,D (A=LSB) 4핀만 제어 → 디코더가 a~g 세그먼트 생성 */
#include <wiringPi.h>
#include "device.h"

static const int FND_BCD[4] = { 21, 22, 23, 24 };  /* A, B, C, D */
#define FND_BLANK  25   /* SN74LS47 BI(pin4): HIGH=표시, LOW=소등 */

int fnd_init(void)
{
    int i;
    for (i = 0; i < 4; i++)
        pinMode(FND_BCD[i], OUTPUT);
    pinMode(FND_BLANK, OUTPUT);
    digitalWrite(FND_BLANK, LOW);  /* 초기 소등 */
    return 0;
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
