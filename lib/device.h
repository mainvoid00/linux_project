#ifndef DEVICE_H
#define DEVICE_H

/* 장치 제어 동적 라이브러리 (libdevice.so)
 * 서버가 dlopen()/dlsym() 으로 런타임 로딩한다 (.so 파일만 교체로 기능 업그레이드).
 * 반환 규약: 0 = 성공, -1 = 실패.
 *   - cds_read() 만 예외: 0/1(조도 상태) 반환, 오류 시 -1 */

int  device_init(void);      /* wiringPi 초기화 + 핀 모드 설정. 0/-1 */
void device_cleanup(void);   /* 모든 장치 OFF */

int  led_on(void);           /* 최대 밝기로 점등 */
int  led_off(void);          /* 소등 */
int  led_bright(int level);  /* 밝기 조절: 0=off, 1=최저, 2=중간, 3=최대 */

int  buzzer_tone(int hz);    /* 압전(passive) 부저: softTone 주파수 출력. hz=0 이면 무음 */
int  buzzer_off(void);

int  cds_read(void);         /* digitalRead: 0=어두움(빛 없음), 1=밝음, -1=오류 */

int  fnd_display(int num);   /* 7세그먼트에 0~9 표시. 범위 밖이면 -1 */
int  fnd_clear(void);        /* 7세그먼트 소등 */

#endif /* DEVICE_H */
