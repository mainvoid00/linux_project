# TCP 기반 원격 장치 제어 시스템 — 빌드
#
#   make                                  네이티브 빌드 (RPi 위에서 직접)
#   make CROSS_COMPILE=aarch64-linux-gnu- 크로스 빌드 (서버/라이브러리 → aarch64 RPi 타겟)
#   make send                             빌드 후 devserver/libdevice.so/index.html 를 RPi 로 scp 전송
#                                         (대상 변경: make send PI_HOST=<RPi-IP> PI_USER=pi PI_DIR=~/app)
#   make clean                            산출물 삭제
#
# 산출물:
#   - devserver  : TCP 데몬 서버 (RPi). 소켓/pthread/dl 만 사용 → 의존성 없이 크로스 가능.
#   - libdevice.so: 장치 제어 동적 라이브러리 (RPi). wiringPi 필요.
#   - devclient  : Ubuntu TCP 클라이언트 (빌드 호스트 = Ubuntu x86-64 → native gcc).
#
# 크로스 빌드 시 libdevice.so 는 타겟용 wiringPi 헤더/.so 경로를 지정:
#   make CROSS_COMPILE=aarch64-linux-gnu- WIRINGPI=/path/to/wiringpi
#   (구조: $(WIRINGPI)/include/{wiringPi.h,softPwm.h,softTone.h}, $(WIRINGPI)/lib/libwiringPi.so)
#
# devserver, libdevice.so 는 RPi 같은 디렉토리에 두고 그 위치에서 실행
# (devserver 는 ./libdevice.so 를 dlopen — 실행 디렉토리에 .so 가 있어야 함).

CROSS_COMPILE ?= aarch64-linux-gnu-
CC      = $(CROSS_COMPILE)gcc   # 서버/라이브러리 (RPi 타겟)
HOSTCC  = gcc                   # 클라이언트 (Ubuntu 호스트)
CFLAGS  = -Wall -Wextra -O2

# make send — RPi 전송 대상 (override: PI_HOST=<RPi-IP> PI_USER=pi PI_DIR=~/app)
PI_USER ?= pi
PI_HOST ?= 10.144.236.106
PI_DIR  ?= ~/linux_project
# 전송 파일 — devclient 은 Ubuntu 호스트용이라 제외, 로그뷰어용 index.html 포함
SEND_FILES = devserver libdevice.so index.html

# wiringPi 헤더/라이브러리 위치
# 크로스 빌드: /usr/aarch64-linux-gnu (Ubuntu에 설치된 aarch64용 wiringPi)
# 네이티브 RPi 빌드: make CROSS_COMPILE= WIRINGPI= (시스템 경로 사용)
WIRINGPI    ?= $(if $(CROSS_COMPILE),/usr/aarch64-linux-gnu,)
WPI_CFLAGS   = $(if $(WIRINGPI),-I$(WIRINGPI)/include,)
WPI_LDFLAGS  = $(if $(WIRINGPI),-L$(WIRINGPI)/lib,)
# wiringPi 링크 라이브러리. 공유 libwiringPi.so 에 링크하면 crypt/m/rt 는 런타임에 자동 해결됨.
# 정적(libwiringPi.a) 링크이거나 "undefined reference to 'crypt'" 가 나면 아래처럼 추가:
#   make ... WPI_LIBS="-lwiringPi -lcrypt -lm -lrt"
WPI_LIBS    ?= -lwiringPi

# 동적 라이브러리 소스 (장치별로 분리) — common/led/buzzer/cds/fnd
LIB_SRC = lib/common.c lib/led.c lib/buzzer.c lib/cds.c lib/fnd.c

# 서버 소스 (기능별로 분리) — main/binding/command/modes/weblog
SRV_SRC = server/main.c server/binding.c server/command.c server/modes.c server/weblog.c

# 출력 바이너리는 server/ · client/ 디렉토리와 이름이 충돌하므로 dev* 로 둔다.
all: libdevice.so devserver devclient

# 동적 라이브러리 (런타임 dlopen 대상) — wiringPi 필요 (I2C 포함, $(WPI_LIBS))
libdevice.so: $(LIB_SRC) lib/device.h
	$(CC) $(CFLAGS) $(WPI_CFLAGS) -shared -fPIC -o $@ $(LIB_SRC) $(WPI_LDFLAGS) $(WPI_LIBS)

# TCP 데몬 서버 (libdevice.so 는 링크하지 않고 dlopen 으로 로드) — RPi 의존성 없음
devserver: $(SRV_SRC) server/server.h
	$(CC) $(CFLAGS) -o $@ $(SRV_SRC) -lpthread -ldl

# Ubuntu TCP 클라이언트 — 호스트(x86-64) native 빌드
devclient: client/client.c
	$(HOSTCC) $(CFLAGS) -o $@ client/client.c -lpthread

# 빌드 산출물을 RPi 로 scp 전송 (같은 디렉토리에 배치 → 그 위치에서 ./devserver 실행)
send: devserver libdevice.so
	scp $(SEND_FILES) $(PI_USER)@$(PI_HOST):$(PI_DIR)/
	@echo "전송 완료 → $(PI_USER)@$(PI_HOST):$(PI_DIR)/  ($(SEND_FILES))"

clean:
	rm -f devserver devclient libdevice.so

.PHONY: all send clean
