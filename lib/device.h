#ifndef DEVICE_H
#define DEVICE_H

/* 장치 제어 동적 라이브러리 (libdevice.so)
 * 서버가 dlopen()/dlsym() 으로 런타임 로딩한다 (.so 파일만 교체로 기능 업그레이드).
 * 장치별로 .c 파일을 분리: common.c / led.c / buzzer.c / cds.c / fnd.c
 * 반환 규약: 0 = 성공, -1 = 실패.
 *   - cds_read() 만 예외: 0~255 아날로그 값 반환, 오류 시 -1 */

/* 공통 (common.c): wiringPi 초기화 + 각 장치 init/cleanup */
int  device_init(void);      /* wiringPiSetup + 각 장치 init. 0/-1 */
void device_cleanup(void);   /* 모든 장치 OFF */

/* LED (led.c) */
int  led_init(void);         /* softPwm 채널 생성 */
int  led_on(void);           /* 최대 밝기로 점등 */
int  led_off(void);          /* 소등 */
int  led_bright(int level);  /* 밝기 조절: 0=off, 1=최저, 2=중간, 3=최대 */

/* 부저 (buzzer.c) — 압전(passive), softTone */
int  buzzer_init(void);      /* softTone 채널 생성 */
int  buzzer_tone(int hz);    /* 주파수 출력. hz=0 이면 무음 */
int  buzzer_off(void);

/* 조도센서 = PCF8591 ADC(YL-40), I2C (cds.c) */
int  cds_init(void);             /* PCF8591 I2C 핸들 오픈 */
int  cds_read(void);             /* AIN0 아날로그 값 0~255, -1=오류 */
int  cds_set_threshold(int t);   /* 임계값 0~255 설정(client 제어), 범위밖 -1 */
int  cds_get_threshold(void);    /* 현재 임계값 반환 */

/* 7세그먼트 = SN74LS47 BCD 디코더 (fnd.c) */
int  fnd_init(void);         /* BCD/BLANK 핀 OUTPUT 설정 */
int  fnd_display(int num);   /* 0~9 표시. 범위 밖이면 -1 */
int  fnd_clear(void);        /* 7세그먼트 소등 */

#endif /* DEVICE_H */
