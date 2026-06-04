# TCP 기반 원격 장치 제어 시스템

x86-64 리눅스(Ubuntu) 클라이언트에서 **TCP 통신**으로 Raspberry Pi에 연결된 장치
(LED · 부저 · 조도센서 · 7세그먼트)를 원격 제어하는 시스템.
VEDA 리눅스 프로그래밍 심화 실습 평가 과제.

- **서버**: RPi의 raw TCP 소켓 **데몬 프로세스**. 연결당 pthread, 장치 제어 로직은
  `libdevice.so`를 `dlopen()`으로 **런타임 동적 로딩**. 로그는 `syslog`.
- **클라이언트**: Ubuntu CLI 프로그램(`devclient`). 송신 + 수신 스레드 구조,
  **SIGINT만 정상 종료**(그 외 시그널 무시), 번호식 계층 메뉴.
- **동적 라이브러리**: 장치 제어를 `libdevice.so`로 분리 → `.so`만 교체해 기능 업그레이드.

---

## 시스템 구성

| 구분 | 서버 | 클라이언트 |
|------|------|-----------|
| 하드웨어 | Raspberry Pi 4 | x86-64 PC/노트북 |
| OS | Raspberry Pi OS | Ubuntu Linux |
| 라이브러리 | wiringPi(GPIO/I2C), pthread, dl | pthread |
| 언어 | C / gcc | C / gcc |

```mermaid
flowchart LR
    C["devclient<br/>(Ubuntu CLI)"]
    B["웹 브라우저<br/>(로그 뷰어)"]
    S["devserver (데몬)<br/>소켓 + pthread + dlopen<br/>+ 로그뷰어 HTTP/SSE"]
    L["libdevice.so<br/>wiringPi GPIO/I2C"]
    D["LED · 부저 · CDS · 7세그"]

    C -- "TCP 5000 : CMD\\n (요청)" --> S
    S -- "TCP 5000 : OK / ERR / EVT\\n (응답·이벤트)" --> C
    B -- "HTTP 8080 : GET /events" --> S
    S -- "HTTP 8080 : SSE 로그 푸시" --> B
    S -- "dlopen" --> L
    L --> D
```

---

## 빌드

이 프로젝트는 **하나의 루트 Makefile**로 3개 산출물(`devserver`·`libdevice.so`·`devclient`)을
빌드한다. **기본(권장)은 Ubuntu 빌드머신에서의 크로스 컴파일**이고, 그게 안 될 때 RPi 위에서
네이티브로 빌드한다.

### 산출물

| 바이너리 | 실행 대상 | 설명 | 의존성 |
|---------|----------|------|--------|
| `devserver` | RPi | TCP 데몬 서버 | 소켓/pthread/dl (의존성 없이 크로스 가능) |
| `libdevice.so` | RPi | 장치 제어 동적 라이브러리 | **wiringPi 필요** (`softPwm`/`softTone`/I2C) |
| `devclient` | Ubuntu 호스트 | TCP 클라이언트 | pthread (항상 native gcc로 빌드) |

> `devclient`는 어느 방식이든 **빌드 호스트(Ubuntu x86-64)용 native**로만 빌드된다
> (Makefile의 `HOSTCC=gcc`). RPi에는 보내지 않는다.

### 방식 1 (권장) — 크로스 컴파일 후 전송 (Ubuntu 빌드머신)

Makefile **기본값이 이미 aarch64 크로스 설정**이라, Ubuntu에서 그냥 `make` 한 번이면
서버·라이브러리(aarch64)와 클라이언트(native)까지 **전부 빌드**된다. 이어서 `make send` 하면 RPi로 전송 끝.

```bash
# 사전 준비 (최초 1회): aarch64 크로스 툴체인 (+ 타겟용 wiringPi)
sudo apt update
sudo apt install -y gcc-aarch64-linux-gnu        # aarch64-linux-gnu-gcc 제공
# 타겟(aarch64)용 wiringPi 헤더/.so 가 /usr/aarch64-linux-gnu 에 없으면 WIRINGPI= 로 경로 지정

# ① 빌드 — make 한 번이면 세 산출물 모두 생성 (별도 인자 불필요)
make

# ② 전송 — devserver·libdevice.so·index.html 을 RPi 로 scp
make send PI_HOST=<RPi-IP>
```

> Makefile 기본값: `CROSS_COMPILE=aarch64-linux-gnu-`, `WIRINGPI=/usr/aarch64-linux-gnu`.
> 그래서 인자 없이 `make` = 크로스 빌드다. wiringPi 경로가 다르면
> `make WIRINGPI=/path/to/wiringpi` (구조: `include/{wiringPi.h,softPwm.h,softTone.h}`, `lib/libwiringPi.so`).
>
> 링크 시 `undefined reference to 'crypt'` 등이 나오면(정적 wiringPi 등):
> `make WPI_LIBS="-lwiringPi -lcrypt -lm -lrt"`

### 방식 2 (대안) — 네이티브 빌드 (크로스가 안 될 때, RPi 위에서 직접)

크로스 툴체인/타겟 wiringPi 준비가 어려우면 소스를 RPi에 올려 RPi 셸에서 직접 빌드한다.

```bash
# 사전 준비 (최초 1회): 빌드 도구 + wiringPi + I2C 활성화
sudo apt update
sudo apt install -y build-essential wiringpi    # wiringpi 패키지가 없으면 아래 "wiringPi 설치" 참고
sudo raspi-config nonint do_i2c 0               # I2C 인터페이스 켜기 (CDS/PCF8591용)

# 빌드 — CROSS_COMPILE 과 WIRINGPI 를 빈 값으로 덮어써서 native gcc·시스템 wiringPi 사용
make CROSS_COMPILE= WIRINGPI=
```

> 네이티브에서는 `CROSS_COMPILE=` `WIRINGPI=` 를 **빈 값으로 명시**해야 한다. 비우면 `gcc`(native)와
> 시스템 wiringPi(`-lwiringPi`)를 쓴다. 생략하면 기본값(크로스 툴체인)을 찾으려다 실패한다.
> 같은 머신(RPi)에서 빌드·실행하므로 `make send`는 필요 없다. 실행은 아래 "실행" 섹션 참고.

### wiringPi 설치 (RPi에 패키지가 없을 때)

최신 Raspberry Pi OS는 `apt`에 `wiringpi`가 없을 수 있다. 그 경우 소스로 설치:

```bash
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi && ./build
gpio -v        # 설치 확인
```

### 기타 타겟

```bash
make clean      # 산출물(devserver/devclient/libdevice.so) 삭제
```

> `devserver`는 데몬화 전 `./libdevice.so`의 절대경로를 `realpath`로 고정한 뒤 `dlopen`하므로
> **실행 디렉토리에 `libdevice.so`가 있어야** 한다.

---

## RPi로 전송 (크로스 빌드 시)

크로스 빌드한 `devserver`·`libdevice.so`와 로그뷰어 UI `index.html`을 RPi로 보낸다.

```bash
# Makefile 자동 전송 (빌드 후 scp) — 기본 대상 pi@<Makefile의 PI_HOST>:~/linux_project
make send PI_HOST=<RPi-IP>

# 대상 변경
make send PI_HOST=<RPi-IP> PI_USER=pi PI_DIR=~/app
```

전송되는 파일은 `devserver libdevice.so index.html` 세 개다(`devclient`은 Ubuntu 호스트용이라 제외).
수동으로 보내려면:

```bash
scp devserver libdevice.so index.html pi@<RPi-IP>:~/linux_project/
```

> **반드시 세 파일을 RPi의 같은 디렉토리에 둬야** 한다. `devserver`는 같은 디렉토리의
> `./libdevice.so`를 `dlopen`하고, 로그뷰어는 같은 디렉토리의 `index.html`을 서빙한다
> (`index.html`이 없으면 내장 폴백 페이지 사용).

---

## 실행

### 1) 서버 (RPi)

```bash
cd ~/linux_project              # devserver·libdevice.so·index.html 이 있는 디렉토리
./devserver [port] [log_port]   # 기본: 5000(TCP) / 8080(로그뷰어). 예: ./devserver 5000 9090
```

- 즉시 **데몬으로 백그라운드 전환**(`fork`/`setsid`)되어 프롬프트가 바로 돌아온다.
- 로그는 `syslog`로 기록 → `sudo journalctl -t devserver -f` 또는 `tail -f /var/log/syslog`로 확인.
- 정지: `pkill devserver`.

### 2) 클라이언트 (Ubuntu)

```bash
./devclient <서버-IP> [port]    # 기본 port 5000. 예: ./devclient 10.144.236.106 5000
```

- 번호식 **계층 메뉴**로 명령을 선택해 서버로 전송한다.
- **Ctrl+C(SIGINT)** 로만 정상 종료(`QUIT` 전송 후 소켓 정리). 그 외 시그널은 무시 → 강제 종료 방지.
- **CDS 자동연동**: 켜면 매초 `EVT CDS`를 표시하는 모니터링 화면으로 들어가고,
  **Enter**를 누르면 `CDS OFF`를 보내고 메뉴로 복귀한다(별도 OFF 메뉴 없음).

### 3) 로그 뷰어 (브라우저)

- `http://<RPi-IP>:8080` 접속 → 서버 로그 **실시간 푸시(SSE)** 확인. 일시정지·지우기·다시불러오기 지원.

### 빠른 점검 (클라이언트 없이)

```bash
# 서버가 떴는지 / 프로토콜 응답 확인
nc <RPi-IP> 5000        # 연결 후 STATUS 입력 → OK STATUS ... 응답
```

---

## 통신 프로토콜 (TCP 텍스트 라인)

- 요청(C→S): `<CMD> [ARG]\n`
- 응답(S→C): `OK ...` / `ERR ...`
- 비동기 이벤트: `EVT ...`

| 명령 | 동작 | 응답 |
|------|------|------|
| `LED ON` / `LED OFF` | 점등 / 소등 | `OK LED ON` / `OK LED OFF` |
| `LED HIGH\|MID\|LOW` | 밝기 3단계 (PWM `softPwm`) | `OK LED HIGH\|MID\|LOW` |
| `BUZZER ON` / `BUZZER OFF` | 저장된 멜로디 재생 / 정지 (passive, `softTone`) | `OK BUZZER ON\|OFF` |
| `CDS ON` / `CDS OFF` | 자동 조도 연동 (어두우면 LED ON) + 매초 `EVT CDS` (읽기 실패 시 `EVT CDS ERROR` 후 자동 종료) | `OK CDS ON\|OFF` |
| `CDS READ` | 1회 조회 | `OK CDS <값> <DARK\|LIGHT> TH <임계값>` |
| `CDS THRESHOLD <0-255>` | 임계값 설정 (클라이언트 제어) | `OK CDS THRESHOLD <값>` |
| `FND <0-9>` | 해당 숫자부터 1초마다 -1 카운트다운 → 0 도달 시 부저 + `EVT FND DONE` | `OK FND START` (사용 중이면 `ERR FND_BUSY`) |
| `FND STOP` | 카운트다운 정지 | `OK FND STOP` (사용 중이면 `ERR FND_BUSY`) |
| `STATUS` | 모드 상태 조회 | `OK STATUS cds=<0\|1> fnd=<0\|1> buzzer=<0\|1>` |
| `QUIT` | 연결 종료 | `OK BYE` |

이벤트 예: `EVT CDS <0~255> <DARK|LIGHT>` (매초), `EVT CDS ERROR` (조도 읽기 실패 → 자동 모드 종료), `EVT FND DONE`

> **조도값**: PCF8591 ADC(YL-40) AIN0 아날로그 `0~255`. `값 >= 임계값`이면 **어두움(DARK)** 으로 판정.
> I2C 읽기가 실패해 `-1`이 나오면(센서 미연결 등) `EVT CDS ERROR`를 한 번 보내고 자동 모드를 종료한다(무한 `-1` 스트림 방지).
> **FND 다중 클라이언트**: 7세그는 1개뿐이라, 다른 클라이언트가 카운트다운 중이면 `FND`/`FND STOP`은 `ERR FND_BUSY`로 거부된다(소유 클라이언트만 제어, 연결 종료 시 자동 해제).

---

## 디렉토리 구조

```
Makefile
client/client.c                                          # devclient (Ubuntu CLI)
server/{server.h, main.c, binding.c, command.c, modes.c, weblog.c}   # devserver
lib/{device.h, common.c, led.c, buzzer.c, cds.c, fnd.c}              # libdevice.so (dlopen)
```

- **서버 모듈**: `main`(accept·데몬) / `binding`(dlopen) / `command`(프로토콜 파싱) /
  `modes`(CDS·FND·부저 스레드) / `weblog`(HTML 실시간 로그)
- **라이브러리 모듈**: `common`(init/cleanup) / `led` / `buzzer` / `cds` / `fnd`

---

## 하드웨어 결선 (RPi)

> 핀 표기: **물리핀**(RPi 40핀 헤더 번호) / **wPi**(wiringPi 번호). 코드의 핀 정의는
> `lib/led.c`(LED) · `lib/buzzer.c`(부저) · `lib/fnd.c`(7세그) · `lib/cds.c`(I2C) 상단에 있다.
> 네트리스트 원본·회로도: `docs/wiring.md`, `docs/circuit.{svg,png}`.

### 신호선 한눈에

| 장치 | RPi 물리핀 | wPi | 연결 대상 | 비고 |
|------|-----------|-----|-----------|------|
| LED | 12 | **1** | 330Ω → LED(+) | `softPwm` 밝기 3단계 |
| 부저 | 13 | **2** | Buzzer(+) | passive 압전, `softTone` |
| 7세그 A (LSB) | 29 | **21** | SN74LS47 pin7 | BCD 입력 |
| 7세그 B | 31 | **22** | SN74LS47 pin1 | BCD 입력 |
| 7세그 C | 33 | **23** | SN74LS47 pin2 | BCD 입력 |
| 7세그 D (MSB) | 35 | **24** | SN74LS47 pin6 | BCD 입력 |
| 7세그 BLANK | 37 | **25** | SN74LS47 pin4 (B̄Ī) | HIGH=표시, LOW=소등 |
| 조도센서 SDA | 3 | 8 (BCM2) | PCF8591 SDA | I2C 데이터 |
| 조도센서 SCL | 5 | 9 (BCM3) | PCF8591 SCL | I2C 클럭 |

### 전원 / GND

| 노드 | 연결 대상 |
|------|-----------|
| **+5V** (RPi pin2/4) | SN74LS47 pin16(VCC) · pin3(L̄T̄) · pin5(R̄B̄Ī) · 7세그 COM(공통 애노드) · PCF8591 VCC |
| **GND 공통** (RPi pin6 등) | RPi GND · SN74LS47 pin8 · LED(−) · 부저(−) · PCF8591 GND |

> ⚠️ RPi·SN74LS47·디스플레이·PCF8591은 **반드시 GND 공통**. 안 그러면 동작이 들쭉날쭉하다.

### SN74LS47 출력 → 7-세그먼트 (각 330Ω 직렬)

| LS47 출력 | DIP 핀 | → 세그먼트 |
|-----------|--------|-----------|
| a | 13 | seg a |
| b | 12 | seg b |
| c | 11 | seg c |
| d | 10 | seg d |
| e | 9  | seg e |
| f | 15 | seg f |
| g | 14 | seg g |

### 조도센서 — PCF8591 ADC(YL-40) I2C

- I2C 주소 **`0x48`**(기본). 연결 후 `i2cdetect -y 1`로 확인 → `48`이 보여야 한다.
- 모듈의 **P5 점퍼**로 온보드 조도센서 출력을 **AIN0** 채널에 연결(코드는 AIN0만 읽음).
- 읽은 아날로그값 **0~255**에서 `값 >= 임계값`이면 **어두움(DARK)** → 자동연동 시 LED ON.
  (`CDS THRESHOLD <0-255>`로 임계값 조정, 기본 극성은 실HW 검증으로 확정됨.)
- 빌드 전 RPi에서 I2C 인터페이스 활성화 필요: `sudo raspi-config nonint do_i2c 0`.

### 저항 (330Ω)

- 7세그먼트 세그먼트별 **7개** + LED **1개** = **총 8개**.
- BCD 입력·BLANK·부저·PCF8591에는 저항 불필요.

### 주의사항

- 디스플레이는 **공통 애노드** + SN74LS47(active-LOW) 조합. 공통 캐소드면 SN74LS48을 써야 한다.
- SN74LS47의 `L̄T̄`(pin3)·`R̄B̄Ī`(pin5)는 **5V 직결**. floating 두면 전 세그먼트 ghosting이 생긴다.
- RPi 3.3V 출력 → 74LS47 입력은 V_IH=2.0V라 정상 인식. 74LS47 출력은 디스플레이로만 가므로 GPIO로의 5V 역류 없음.

> 참고: `docs/wiring.md`의 CDS 항목은 구버전(조도모듈 DO digitalRead, wPi0) 기준이라 위 PCF8591 I2C 결선과 다르다 — 현재 구현은 위 표가 정확하다.

---

## 주요 특징

- raw TCP 소켓(`socket/bind/listen/accept`) + 연결당 **pthread** 멀티클라이언트
- 서버 **데몬화**(`fork`/`setsid`) + `syslog` 로깅
- 장치 제어 로직 **런타임 동적 로딩**(`dlopen`/`dlsym`/`dlclose`, `-ldl`)
- 모드 스레드(CDS·FND·부저)와 GPIO 공유 자원 **뮤텍스 보호**
- 클라이언트 **SIGINT만 정상 종료**, 그 외 시그널 무시 → 강제 종료 방지
- (추가기능) **HTML 실시간 서버 로그 뷰어** — 2번째 포트(기본 8080), **SSE 실시간 푸시**(`/events`) + 외부 `index.html`(일시정지·지우기·다시불러오기), `/log` 폴백
