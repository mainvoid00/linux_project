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

- **서버**: RPi에서 raw TCP 소켓(`socket/bind/listen/accept`)으로 직접 구현한 경량 **HTTP/1.1 서버**.
  연결마다 pthread. 요청 라우팅 → `libdevice.so`(dlopen) → GPIO 제어. CDS·LOG는 SSE로 push. `web/index.html` 정적 서빙.
- **클라이언트**: **웹 브라우저**. `fetch`로 REST 호출, `EventSource`로 실시간 조도·로그 수신. (`curl` 테스트 가능)
- **동적 라이브러리**: 장치 제어 로직을 `libdevice.so`로 분리. 서버는 `-ldevice` 링크가 아니라
  `dlopen()/dlsym()/dlclose()`로 **런타임 동적 로딩**한다 (빌드 시 `-ldl` 필요).

## 통신 프로토콜 (HTTP REST + SSE)

- 제어: `POST` / 조회·스트림: `GET`. 응답: JSON + HTTP 상태코드.
- 엔드포인트:
  - `POST /led/on|off` — 수동 점등/소등
  - `POST /led/blink/on|off` — 1초 점멸 (서버 스레드)
  - `POST /cds/on|off` — 자동 조도 연동(어두우면 LED OFF/밝으면 ON), `GET /cds/stream`(SSE) 1초마다 값 push
  - `POST /buzzer/on|off` — 저장된 계이름 멜로디 재생/정지 (압전 passive, `softTone`)
  - `POST /fnd/on|off` — 1초마다 0→9 증가(순환) / 정지·초기화
  - `POST /log/on|off` — 서버 로그 스트리밍, `GET /log/stream`(SSE)
  - `GET /status`, `GET /`(웹 UI)
- 상세 규격: API 명세서 (https://www.notion.so/372fdb06b70f8138be93f004d6cb5842)
- **LED 모드 배타성**: 수동/blink/cds자동은 같은 물리 LED를 공유 → 한 모드만 소유, 새 모드 진입 시 이전 모드 자동 종료.

## 빌드

```bash
# 네이티브 (RPi 위에서 직접)
make ; make run                                   # → ./devserver 8080

# 크로스 컴파일 (Linux 빌드머신 → aarch64 RPi 타겟). 산출물만 Pi로 복사해 실행.
make CROSS_COMPILE=aarch64-linux-gnu- WIRINGPI=<wiringpi-경로>
scp devserver libdevice.so pi@<RPi-IP>:~/app/    # web/index.html 도 함께 복사
```

빌드 산출물: **`devserver`** + **`libdevice.so`** (+ `web/index.html`). Pi의 한 디렉토리에 두고 그 위치에서 `./devserver 8080`.

> - 바이너리명이 `devserver` 인 이유: 출력 `server` 가 `server/` 디렉토리와 충돌(EISDIR).
> - `devserver`(server.c)는 소켓/pthread/dl 만 사용 → **의존성 없이 크로스 컴파일** (검증: GLIBC_2.17 만 요구).
> - `libdevice.so`(device.c)만 **wiringPi 필요** → 크로스 시 `WIRINGPI=` 로 타겟용 헤더+`.so` 경로 지정
>   (Pi의 `/usr/include/{wiringPi,softTone}.h`, `/usr/lib/libwiringPi.so*` 복사). 미지정 시 그 부분만 Pi에서 빌드해도 됨.
> - `libdevice.so`·`web/index.html` 은 cwd 상대경로 → **실행 디렉토리 = 산출물 위치**.

## 디렉토리 구조 (단일 루트 Makefile)

```
Makefile  server/server.c  web/index.html  lib/{device.c,device.h}
```

## 진행 상황 (2026-06-01)

- [x] 과제 요구사항 분석 (스크린샷)
- [x] 설계서 작성 → Notion `🛳️ Veda / linux_project`
      (https://www.notion.so/372fdb06b70f8158adfed16a2d31d4c2)
- [x] API 명세서 작성 → Notion `linux_project / API 명세서`
      (https://www.notion.so/372fdb06b70f8138be93f004d6cb5842)
- [x] `web/index.html` 작성 (제어 버튼 + fetch + EventSource SSE)
- [x] `lib/device.c` · `lib/device.h` 작성 (wiringPi GPIO, 스텁 시그니처로 syntax 검증 완료)
- [x] `server/server.c` 작성 (HTTP 서버 + pthread + dlopen + SSE) — `gcc -Wall -Wextra -c` 통과
- [x] 최상위 `Makefile` 작성 (`make` → libdevice.so + devserver, `make run`, `make clean`)
      — 바이너리명 `server`→`devserver` 로 변경(`server/` 디렉토리와 충돌 수정), server.c `-fsyntax-only` 통과
- [x] Makefile 크로스 컴파일 대응 (`CROSS_COMPILE=`, `WIRINGPI=`) — devserver aarch64 크로스 빌드 검증(ELF, GLIBC_2.17)

## 미확정 (구현 전 확인 필요)

- 포트 번호(예: 8080) 확정
- CDS `digitalRead` 극성(1=밝음/0=어두움) 결선 확인
- BUZZER: 저장할 곡(계이름) 확정 (압전 passive + `softTone`)
- GPIO 핀 번호: 실제 결선에 맞춰 확정
- 평가 기준상 별도 CLI(TCP) 클라이언트 필수 여부 — 현재 브라우저로 대체

## 결정 사항

- (2026-06-01) 라이브러리는 `dlopen`/`dlsym` **런타임 동적 로딩** 방식으로 확정 (load-time `-ldevice` 링크 아님).
- (2026-06-01) **아키텍처: 브라우저 ↔ RPi 직접 HTTP**. RPi 서버는 raw TCP 소켓으로 직접 구현한 HTTP/1.1 서버
  (socket+pthread+.so 요구 충족). Ubuntu CLI TCP 클라이언트 → **브라우저로 대체**.
- (2026-06-01) 실시간 push = **SSE**. 제어=POST, 조회·스트림=GET, 응답=JSON. LED는 수동/blink/cds **배타적 모드**.
- (2026-06-01) CDS는 `digitalRead`(0/1, ADC 불필요), 부저는 압전 passive(`softTone`), FND는 0→9 무한 순환.
- (2026-06-01) **빌드 = 크로스 컴파일** (Linux 빌드머신 → aarch64, 크로스툴 설치됨). Pi엔 산출물(devserver/libdevice.so/web)만 전달.
  Makefile은 `CROSS_COMPILE=`·`WIRINGPI=` 변수로 네이티브/크로스 모두 지원. devserver는 의존성 0, libdevice.so만 wiringPi sysroot 필요.
