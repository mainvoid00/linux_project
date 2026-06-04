# 결선 네트리스트 (SN74LS47 7-Segment)

> `circuit.fzz`(Fritzing)가 부품 깨짐으로 안 열리면 이 표를 기준으로 배선하세요.
> 핀 번호: RPi=물리핀(wiringPi), SN74LS47=DIP 핀번호.

## 전원 / GND
| 노드 | 연결 대상 |
|------|-----------|
| **+5V** | SN74LS47 pin16(VCC) · pin3(L̄T̄) · pin5(R̄B̄Ī) · 7seg COM(공통 애노드) · PCF8591(YL-40) VCC |
| **GND(공통)** | RPi GND · SN74LS47 pin8 · LED(−) · Buzzer(−) · PCF8591 GND |

## 신호선
| From (RPi) | wiringPi | To |
|------------|----------|-----|
| pin3  | wPi8 (BCM2)  | PCF8591 SDA (I2C 데이터) |
| pin5  | wPi9 (BCM3)  | PCF8591 SCL (I2C 클럭) |
| pin12 | wPi1  | 330Ω → LED(+) anode |
| pin13 | wPi2  | Buzzer(+) |
| pin29 | wPi21 | SN74LS47 pin7 (A, LSB) |
| pin31 | wPi22 | SN74LS47 pin1 (B) |
| pin33 | wPi23 | SN74LS47 pin2 (C) |
| pin35 | wPi24 | SN74LS47 pin6 (D, MSB) |
| pin37 | wPi25 | SN74LS47 pin4 (B̄Ī, 소등 제어) |

## 조도센서 — PCF8591 ADC(YL-40) I2C
| 항목 | 값 |
|------|-----|
| I2C 주소 | **0x48**(기본). `i2cdetect -y 1`로 `48` 확인 |
| 채널 | **AIN0** — 모듈 **P5 점퍼**로 온보드 조도센서 출력을 AIN0에 연결 |
| 읽는 값 | 아날로그 0~255. `값 >= 임계값`이면 어두움(DARK) |
| RPi 설정 | 빌드/실행 전 `sudo raspi-config nonint do_i2c 0`로 I2C 활성화 |

## SN74LS47 출력 → 7-Segment (각 330Ω 직렬)
| LS47 출력 | DIP 핀 | 330Ω → 세그먼트 |
|-----------|--------|------------------|
| a | 13 | seg a |
| b | 12 | seg b |
| c | 11 | seg c |
| d | 10 | seg d |
| e | 9  | seg e |
| f | 15 | seg f |
| g | 14 | seg g |

## 저항 (330Ω)
- 7세그먼트 세그먼트별 7개 + LED 1개 = **8개** (권장)
- BCD 입력·BLANK·부저·PCF8591 모듈에는 저항 불필요

## 주의
- 공통 **애노드** 디스플레이 + SN74LS47 조합 (공통 캐소드면 SN74LS48)
- RPi 3.3V → 74LS47 입력 정상 인식(V_IH=2.0V), 출력은 디스플레이로만 가므로 GPIO 5V 역류 없음
- SN74LS47·디스플레이·PCF8591·RPi는 **반드시 GND 공통**
- 조도센서는 디지털 DO(digitalRead)가 아니라 **PCF8591 ADC를 통한 I2C 아날로그**로 읽는다(코드: `lib/cds.c`)
