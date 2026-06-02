# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

---

# Project: TCP 기반 원격 장치 제어 시스템

VEDA 리눅스 프로그래밍 심화 실습 평가 과제. x86-64 리눅스 클라이언트에서 TCP 통신으로
Raspberry Pi에 연결된 장치(LED·부저·조도센서·7세그먼트)를 원격 제어한다.

## 환경

| 구분 | 서버 | 클라이언트 |
|------|------|-----------|
| 하드웨어 | Raspberry Pi 4 | x86-64 PC/노트북 |
| OS | Raspberry Pi OS | Ubuntu Linux |
| 라이브러리 | wiringPi(GPIO), pthread | pthread |
| 언어 | C / gcc | C / gcc |

## 시스템 구성

- **서버**: RPi에서 raw TCP 소켓(`socket/bind/listen/accept`)으로 구현한 **TCP 서버**. **대몬(daemon) 프로세스**.
  연결마다 pthread. 텍스트 라인 프로토콜 파싱 → `libdevice.so`(dlopen) → GPIO 제어. 로그는 `syslog`.
- **클라이언트**: **Ubuntu CLI 프로그램**(`devclient`). stdin 명령 전송 + 수신 스레드로 응답/이벤트 출력.
  **SIGINT 시그널 처리**로 강제 종료 방지(정상 종료 시 `QUIT` 전송).
- **동적 라이브러리**: 장치 제어 로직을 `libdevice.so`로 분리. 서버는 `-ldevice` 링크가 아니라
  `dlopen()/dlsym()/dlclose()`로 **런타임 동적 로딩**(빌드 시 `-ldl`). → `.so`만 교체해 기능 업그레이드.

## 통신 프로토콜 (TCP 텍스트 라인)

- 요청(C→S): `<CMD> [ARG]\n`. 응답(S→C): `OK ...` / `ERR ...`, 비동기 이벤트: `EVT ...`
- 명령:
  - `LED ON|OFF` / `LED HIGH|MID|LOW` — 점등·소등 / 밝기 3단계(PWM `softPwm`)
  - `BUZZER ON|OFF` — 저장된 계이름 멜로디 재생/정지 (압전 passive, `softTone`)
  - `CDS ON|OFF` — 자동 조도 연동(**빛 없으면 LED ON / 밝으면 OFF**) + 매초 `EVT CDS <0|1> <DARK|LIGHT>`. `CDS READ`는 1회 조회
  - `FND <0-9>` — 해당 숫자부터 **1초마다 -1 카운트다운**, 0 도달 시 부저 울림 + `EVT FND DONE`. `FND STOP` 정지
  - `STATUS`, `QUIT`
- 상세 규격: API 명세서 (https://www.notion.so/372fdb06b70f8138be93f004d6cb5842)

## 빌드 (make)

```bash
# 네이티브 (RPi 위에서 직접): libdevice.so + devserver + devclient
make ; make run                                   # → ./devserver 5000 (데몬)

# 크로스 (Ubuntu 빌드머신 → aarch64 RPi 타겟): 서버/라이브러리만 크로스, 클라이언트는 호스트 native
make CROSS_COMPILE=aarch64-linux-gnu- WIRINGPI=<wiringpi-경로>
scp devserver libdevice.so pi@<RPi-IP>:~/app/     # 같은 디렉토리에 두고 그 위치에서 실행
```

> - 출력 바이너리명 `devserver`·`devclient` (이름이 `server/`·`client/` 디렉토리와 충돌하지 않게).
> - `devserver`(server.c)는 소켓/pthread/dl 만 사용 → 의존성 없이 크로스 가능.
> - `libdevice.so`(device.c)만 **wiringPi 필요**(`softPwm`/`softTone` 포함) → 크로스 시 `WIRINGPI=` 지정.
> - `devclient`는 Ubuntu 호스트용 → Makefile에서 `HOSTCC=gcc`(native)로 빌드.
> - `devserver`는 데몬화 전 `realpath`로 `./libdevice.so` 절대경로를 고정 후 `dlopen` → 실행 디렉토리에 `.so` 필요.

## 디렉토리 구조 (단일 루트 Makefile)

```
Makefile  server/server.c  client/client.c  lib/{device.c,device.h}
```

## 진행 상황 (2026-06-02)

- [x] 공식 요구사항(jpg 2장) 재확인 — TCP 서버/클라이언트, 데몬, 시그널, make
- [x] 설계서·API 명세서 → Notion (HTTP 기준 작성됨, **TCP로 갱신 필요**)
- [x] `lib/device.c·h` — LED 밝기 3단계(`softPwm`) 추가, CDS digitalRead, 부저 softTone, FND. 스텁 syntax 검증 통과
- [x] `server/server.c` — TCP 데몬 서버(소켓+pthread+dlopen, CDS/FND/부저 모드 스레드). `-Wall -Wextra -fsyntax-only` 통과
- [x] `client/client.c` — Ubuntu TCP 클라이언트(수신 스레드 + SIGINT 처리). syntax 통과
- [x] `Makefile` — `make`(libdevice.so/devserver/devclient), `make run`, `make clean`, 크로스 지원
- [ ] **Notion 설계서·API 명세서 TCP로 갱신**
- [ ] **README.md + 실행과정 text 파일**(제출물)
- [ ] RPi 실 하드웨어 빌드·동작 검증 (GPIO 핀/CDS 극성 확정)

## 미확정 (실 하드웨어에서 확정)

- GPIO 핀 번호: `device.c` 상단 `#define`(LED=1/BUZZER=2/CDS=3/FND=21~27) — 실제 결선에 맞춰 수정
- CDS `digitalRead` 극성(0=어두움/1=밝음)
- 7세그먼트 공통 캐소드/애노드 (애노드면 폰트 HIGH/LOW 반전)
- BUZZER 멜로디 곡 (현재 도레미파솔라시도 placeholder)

## 결정 사항

- (2026-06-02) **공식 요구로 아키텍처 회귀: 브라우저/HTTP 폐기 → 순수 TCP 서버 + Ubuntu CLI 클라이언트**.
  서버는 **데몬 프로세스**, 클라이언트는 **시그널 처리**. (web/index.html, HTTP server.c 폐기)
- (2026-06-02) 동시성 = **멀티스레드(pthread)**: 연결당 스레드 + 장치 모드별 스레드(CDS/FND/부저). GPIO는 mutex 보호.
- (2026-06-02) 기능 스펙 확정: **LED 밝기 3단계(softPwm)**, **CDS 빛없으면 LED ON**, **FND N→0 카운트다운 후 부저**.
- (2026-06-01) 라이브러리 `dlopen`/`dlsym` **런타임 동적 로딩** 유지 (요구사항 "동적 라이브러리 기능 이용"에 부합).
- (2026-06-01) **빌드 = make 자동화**, 크로스 컴파일(aarch64) 지원.
