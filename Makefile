# TCP 기반 원격 장치 제어 시스템 — 빌드
#
#   make                                  네이티브 빌드 (RPi 위에서 직접)
#   make CROSS_COMPILE=aarch64-linux-gnu- 크로스 빌드 (aarch64 RPi 타겟)
#   make run                              빌드 후 ./devserver 8080 실행
#   make clean                            산출물 삭제
#
# devserver(server.c): 소켓/pthread/dl 만 사용 → 의존성 없이 크로스 컴파일.
# libdevice.so(device.c): wiringPi 필요. 크로스 빌드 시 타겟용 헤더/.so 경로를 지정:
#   make CROSS_COMPILE=aarch64-linux-gnu- WIRINGPI=/path/to/wiringpi
#   (구조: $(WIRINGPI)/include/{wiringPi.h,softTone.h}, $(WIRINGPI)/lib/libwiringPi.so)
#   RPi 의 /usr/include/{wiringPi,softTone}.h 와 /usr/lib/libwiringPi.so* 를 복사해 쓴다.
#
# 산출물 devserver, libdevice.so 를 RPi 같은 디렉토리에 두고 그 위치에서 실행.
# (devserver 는 ./libdevice.so 를 dlopen, web/index.html 을 상대경로로 서빙)

CROSS_COMPILE ?=
CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -Wall -Wextra -O2
PORT   ?= 8080

# wiringPi 헤더/라이브러리 위치 (네이티브 RPi 빌드면 비워두면 됨)
WIRINGPI    ?=
WPI_CFLAGS   = $(if $(WIRINGPI),-I$(WIRINGPI)/include,)
WPI_LDFLAGS  = $(if $(WIRINGPI),-L$(WIRINGPI)/lib,)

# 출력 바이너리는 'server/' 디렉토리와 이름이 충돌하므로 devserver 로 둔다.
all: libdevice.so devserver

# 동적 라이브러리 (런타임 dlopen 대상) — wiringPi 필요
libdevice.so: lib/device.c lib/device.h
	$(CC) $(CFLAGS) $(WPI_CFLAGS) -shared -fPIC -o $@ lib/device.c $(WPI_LDFLAGS) -lwiringPi

# HTTP 서버 (libdevice.so 는 링크하지 않고 dlopen 으로 로드) — RPi 의존성 없음
devserver: server/server.c
	$(CC) $(CFLAGS) -o $@ server/server.c -lpthread -ldl

run: all
	./devserver $(PORT)

clean:
	rm -f devserver libdevice.so

.PHONY: all run clean
