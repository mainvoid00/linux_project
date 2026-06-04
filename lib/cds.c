/* 조도센서 = PCF8591 ADC/DAC 모듈(YL-40), I2C 통신
 *   - SDA → SDA1 (wiringPi #8, BCM_GPIO #2)
 *   - SCL → SCL1 (wiringPi #9, BCM_GPIO #3)
 *   - 모듈 P5 점퍼로 조도센서 출력을 AIN0 채널에 연결
 *   - 임계값(threshold)은 클라이언트에서 설정 → 아날로그 값과 비교해 LED on/off */
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include "device.h"

#define PCF8591_ADDR  0x48   /* YL-40 기본 I2C 주소 */
#define PCF8591_AIN0  0x40   /* 제어바이트: 채널0 단일 입력 선택 */

static int g_fd = -1;
static int g_threshold = 128;   /* 클라이언트에서 조절. 기본 128 (중간) */

int cds_init(void)
{
    /* 부팅 시엔 I2C 버스를 건드리지 않는다 → fd 는 첫 CDS 사용 때 lazy open.
     * PCF8591 미연결이어도 서버/LED/부저/FND 는 정상 기동 */
    g_fd = -1;
    return 0;
}

int cds_read(void)
{
    if (g_fd < 0) {
        /* 첫 호출(= 첫 CDS ON/READ) 때만 I2C 핸들 오픈 */
        g_fd = wiringPiI2CSetup(PCF8591_ADDR);
        if (g_fd < 0)
            return -1;
    }
    /* PCF8591 은 변환에 1샘플 지연 → 채널 선택 후 더미 read 1회 버리고 읽는다 */
    wiringPiI2CWrite(g_fd, PCF8591_AIN0);
    wiringPiI2CRead(g_fd);              /* 더미 (직전 변환값) */
    return wiringPiI2CRead(g_fd);       /* 현재 AIN0 변환값 0~255 */
}

int cds_set_threshold(int t)
{
    if (t < 0 || t > 255)
        return -1;
    g_threshold = t;
    return 0;
}

int cds_get_threshold(void)
{
    return g_threshold;
}
