# UNDERTALE Sans Battle — Win32 GDI 구현 계획 (#3 기본)

- **작성일**: 2026-06-17 · **마감**: 2026-06-26(금) 17:00
- **결정**: #3(Win32 GDI 창 게임)을 **기본**으로 먼저 제작, #1(콘솔 픽셀 렌더)은 이후 병행
- **절대 요건**: Visual Studio에서 **별도 세팅 없이 `.sln` 열고 F5 → 즉시 실행** (에셋 파일 없이도 동작)
- **레퍼런스**: Bad Time Simulator (jcw87/c2-sans-fight, Construct2/브라우저) — 콘솔/창 C로 충실히 오마주

---

## 1. 아키텍처 (왜 이렇게)

콘솔 창에 GDI를 얹는 ImageLayer 방식은 **GDI 휘발성**(콘솔 텍스트 repaint 시 그림 소거)으로 실시간 게임에 부적합(검증 완료). 대신:

- **우리 소유의 Win32 윈도우**를 `CreateWindowEx`로 생성 → WM_PAINT/repaint를 우리가 통제.
- **더블버퍼링**: 메모리 DC(백버퍼)에 한 프레임을 전부 그린 뒤 `BitBlt`로 1회 전송 → 깜빡임 0.
- **고정 타임스텝 루프**: `PeekMessage`(논블로킹) + `QueryPerformanceCounter` 누적 → 60fps, 프레임 독립 물리.
- **입력**: `GetAsyncKeyState`로 키 홀드 직접 폴링(부드러운 연속 이동·동시입력) — 콘솔 _getch의 2바이트/씹힘 문제 원천 회피.
- **그래픽**: 1차는 GDI 도형(`FillRect`/`Rectangle`)으로 절차적 렌더(에셋 0). 이후 BMP 스프라이트(BTS `Textures/`)를 `LoadImage`+`TransparentBlt`로 교체.
- **라이브러리**: `gdi32/user32/winmm`만(윈도우 시스템 = 외부 라이브러리 아님). 코드 내 `#pragma comment(lib,...)`로 링크.
- **인코딩**: UI/대사는 영어(BTS 원문도 영어) → `TextOutA` ASCII로 인코딩 문제 원천 회피.

## 2. "세팅 없이 F5" 보장 방법

- `UndertaleSans/` 폴더에 `UndertaleSans.sln` + `UndertaleSans.vcxproj` + 소스 동봉.
- `SubSystem=Windows` + `WinMain` 진입점 → 콘솔창 안 뜨는 깔끔한 게임 창.
- `CharacterSet=NotSet` + 명시적 `...A` API 사용 → TCHAR/유니코드 혼선 제거.
- `WindowsTargetPlatformVersion=10.0`(최신 SDK 자동 선택), `PlatformToolset=v143`(VS2022). **구버전 VS면 "retarget" 1클릭** 안내.
- 첫 슬라이스는 **에셋 파일 의존 0** → 클론 후 F5만으로 실행.

## 3. 파일 구조

```
UndertaleSans/
├─ UndertaleSans.sln
├─ UndertaleSans.vcxproj
├─ main.c                 ← 슬라이스1: 단일 파일(빌드 리스크 최소화)
└─ (이후) render.c/.h, game.c/.h, input.c/.h, assets/   ← 모듈 분리·에셋은 동작 확인 후
```

> 슬라이스1은 단일 `main.c`로 시작(작성자가 Mac이라 직접 컴파일 불가 → 빌드 실패모드 최소화). 빌드 성공 확인 후 모듈로 분리.

## 4. 구현 로드맵 (수직 슬라이스 우선, 핑퐁)

- **슬라이스 1 (지금):** Win32 창 + 더블버퍼 + 상태머신(TITLE/BATTLE/GAMEOVER) + 빨간 영혼(WASD/화살표 이동) + **좌우 뼈 1패턴**(스폰/이동/충돌/무적) + HP바 + 게임오버/재시작. **에셋 0, F5로 실행.** → 사용자 빌드/스샷.
- **슬라이스 2:** 게이스터 블래스터 패턴(예고→빔), 대사 타이핑(상단), 샌즈 도형/스프라이트, FIGHT/ITEM 메뉴 골격.
- **슬라이스 3:** 난이도 선택 화면, 파란 영혼 중력 점프, 파란/흰 뼈, ACT/MERCY, 4턴 아크 + 자비 엔딩.
- **슬라이스 4:** BGM(winmm `PlaySound` wav), BTS 스프라이트 BMP 교체, 폴리시.
- **슬라이스 5 (병행, 후순위):** #1 콘솔 픽셀 버전(블록문자+WriteConsoleOutput) — 동일 게임로직 공유, 렌더만 교체.

### 채점 매핑 (Win32 경로에서도 전부 충족)
구조체(Soul/Bone/Blaster/...), 배열(bones[]/...), 랜덤(rand 뼈 생성), 사용자정의함수 5개+(update/render/spawn/collision/...), 키보드입력(GetAsyncKeyState), 음악(PlaySound). 화면 3개+(타이틀/전투/게임오버/난이도/엔딩). 주석 컨벤션 `// [구현조건: XXX]`.

### Cut list (일정 압박 시)
스프라이트 BMP → 도형 유지 / 패턴 3·4 → 1·2만 / 난이도 3 → 1 / MERCY·ACT 연출 축소. **절대 사수**: 창 실행 무오류, 1~2패턴, HP/충돌, 게임오버, 주석, 설명서.

## 5. 리스크

- **VS 버전 차이** → vcxproj retarget 1클릭(안내). 최신SDK 자동선택으로 완화.
- **작성자 Mac 실행 불가** → 슬라이스마다 사용자 빌드/스샷 핑퐁. 단일파일·도형렌더로 첫 빌드 실패율 최소화.
- **고DPI 스케일** → 1차는 무시(고정 640×480), 필요시 `SetProcessDPIAware`.
