# TCP 기반 원격 장치 제어 시스템 — 빌드
#
#   make                                  네이티브 빌드 (RPi 위에서 직접)
#   make CROSS_COMPILE=aarch64-linux-gnu- 크로스 빌드 (서버/라이브러리 → aarch64 RPi 타겟)
#   make run                              빌드 후 ./devserver 실행 (데몬)
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

CROSS_COMPILE ?=
CC      = $(CROSS_COMPILE)gcc   # 서버/라이브러리 (RPi 타겟)
HOSTCC  = gcc                   # 클라이언트 (Ubuntu 호스트)
CFLAGS  = -Wall -Wextra -O2
PORT   ?= 5000

# wiringPi 헤더/라이브러리 위치 (네이티브 RPi 빌드면 비워두면 됨)
WIRINGPI    ?=
WPI_CFLAGS   = $(if $(WIRINGPI),-I$(WIRINGPI)/include,)
WPI_LDFLAGS  = $(if $(WIRINGPI),-L$(WIRINGPI)/lib,)

# 출력 바이너리는 server/ · client/ 디렉토리와 이름이 충돌하므로 dev* 로 둔다.
all: libdevice.so devserver devclient

# 동적 라이브러리 (런타임 dlopen 대상) — wiringPi 필요
libdevice.so: lib/device.c lib/device.h
	$(CC) $(CFLAGS) $(WPI_CFLAGS) -shared -fPIC -o $@ lib/device.c $(WPI_LDFLAGS) -lwiringPi

# TCP 데몬 서버 (libdevice.so 는 링크하지 않고 dlopen 으로 로드) — RPi 의존성 없음
devserver: server/server.c
	$(CC) $(CFLAGS) -o $@ server/server.c -lpthread -ldl

# Ubuntu TCP 클라이언트 — 호스트(x86-64) native 빌드
devclient: client/client.c
	$(HOSTCC) $(CFLAGS) -o $@ client/client.c -lpthread

run: libdevice.so devserver
	./devserver $(PORT)

clean:
	rm -f devserver devclient libdevice.so

.PHONY: all run clean
