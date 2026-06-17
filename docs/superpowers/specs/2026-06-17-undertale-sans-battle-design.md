# UNDERTALE — Sans Battle (콘솔 C) 설계 문서

- **작성일**: 2026-06-17
- **대상 과목**: 한국디지털미디어고등학교 2026 프로그래밍 수행평가 (콘솔 게임 프로그래밍)
- **제출 기한**: 2026년 6월 26일(금) 17시
- **개발/실행 환경**: Windows + Visual Studio (MSVC), C 언어
- **레퍼런스 느낌**: Bad Time Simulator (jcw87/c2-sans-fight)

---

## 1. 목표 & 컨셉

언더테일 **샌즈 전투**를 콘솔 C 게임으로 구현한다. Bad Time Simulator의 UI·감성·핵심 메커니즘을
충실히 담되, 기간 내 완성 가능하도록 **상징적인 공격 4패턴 오마주**로 범위를 한정한다.

핵심 결정 요약:
- 보스: **샌즈(Sans)**
- 구조: **메뉴 턴제 완전 구현** (FIGHT / ACT / ITEM / MERCY) + 적 턴 탄막 회피
- 그래픽: **하이브리드** — 정적 화면/샌즈 스프라이트는 ImageLayer BMP, 전투 박스·영혼·뼈·메뉴·HP는 텍스트(gotoxy)
- 흐름: **4턴 생존 → MERCY(자비) 엔딩**, HP 0 → 게임오버
- **난이도 선택** 화면 포함 (예: EASY / NORMAL / HARD)

---

## 2. 채점 기준 충족 매핑 (구현 40 + 테스트 10 + 주석 10 = 60점 목표, 설명서 40점 별도)

| 항목 | 배점 | 구현 방식 |
|---|---|---|
| 화면 3개+ | 20 | ①타이틀 ②난이도 선택 ③전투 ④엔딩/게임오버 (4개) |
| 음악 재생 | 5 | `PlaySound` Megalovania 루프 (WAV) |
| 구조체 | 5 | `Soul`, `Bone`, `Blaster`, `Sans`, `Item` 등 |
| 랜덤 함수 | 5 | 뼈/블래스터 위치·간격 `rand()` |
| 사용자 정의 함수 5개+ | 5 | 20개 이상 (렌더·턴·패턴·UI 함수) |
| 키보드 입력 | 5 | `_kbhit`/`_getch` — 영혼 이동·점프, 메뉴 선택 |
| 배열 | 5 | 뼈 배열, 블래스터 배열, 아이템 배열, 대사 배열 |
| 게임 테스트 | 10 | 오류 없이 동작 (VS에서 반복 테스트) |
| 주석 | 10 | 6개 선택조건 사용처에 주석 전부 |

> 선택조건은 택4(20점)면 만점이지만, 6개 모두 자연스럽게 충족 → 안전하게 만점.

---

## 3. 화면 흐름

```
[타이틀]
  UNDERTALE 로고 (BMP), "Press any key", Megalovania 재생 시작
      │ 아무 키
      ▼
[난이도 선택]
  EASY / NORMAL / HARD  (영혼 ♥ 커서로 선택)
  → 탄막 속도·밀도·플레이어 HP가 난이도별로 달라짐
      │ 선택
      ▼
[전투]  ── HP 0 ──▶ [게임오버]  부서진 영혼 + "Stay determined..." → RETRY/종료
  4턴 진행
      │ 4턴 생존
      ▼
[자비 엔딩]  샌즈가 지쳐 잠듦 / 승리 메시지
```

---

## 4. 전투 화면 레이아웃 (Bad Time Simulator 스타일)

```
┌──────────────────────────────────────┐
│            (SANS 스프라이트 BMP)        │  상단: 정적 이미지, 파란 눈 번쩍 2~3컷 교체
│                                        │
│   * heh heh heh... you're gonna        │  대사: '* ' 뒤 한 글자씩 타이핑(Sleep)
│     have a bad time.                    │
│        ┌────────────────┐               │
│        │       ♥        │               │  전투 박스: 흰 테두리, 빨간 영혼 회피
│        └────────────────┘               │
│   CHARA   LV 19   HP ▰▰▰▰▰▰▱▱  92/92   │  이름·LV·노란 HP바
│   [FIGHT] [ ACT ] [ITEM] [MERCY]        │  4버튼, 선택 시 노란 하이라이트
└──────────────────────────────────────┘
```

- 콘솔: ImageLayer 기본 **180×48**. 상단 = 샌즈 BMP(턴 시작 시 1회 렌더), 그 외 = `gotoxy` 텍스트.
- 좌표계는 구현 초반에 먼저 고정한다(박스 위치/크기 상수화).

---

## 5. 전투 루프 (핵심 로직)

```
난이도 적용 → 플레이어 HP 초기화
for turn in [좌우뼈, 블래스터, 파란점프, 파란/흰뼈]:
    1) 샌즈 대사 타이핑 출력
    2) 적 턴: 해당 공격 패턴 탄막 루프
         - 영혼 이동(화살표/WASD), 충돌 시 HP 감소
         - 일정 시간/패턴 종료까지 지속
         - HP 0이면 → 게임오버
    3) 내 턴: 메뉴 입력 대기
         FIGHT → 타이밍 바 → 샌즈 회피(1뎀) + 깐죽 대사
         ACT   → Check(샌즈 능력치) 등 플레이버
         ITEM  → 회복 아이템 사용(HP 증가, 배열 소모)
         MERCY → 4턴 전: "샌즈가 봐주지 않는다"
4턴 생존 → MERCY 활성화 → 봐주기 → [자비 엔딩]
```

---

## 6. 데이터 구조 (구조체)

```c
typedef struct { int x, y; int prevX, prevY; int isBlue; int onGround; } Soul;   // 영혼(빨강/파랑 모드)
typedef struct { int x, y; int dir; int active; int isBlue; } Bone;              // 뼈 (흰/파랑)
typedef struct { int x, y; int dir; int state; int timer; } Blaster;            // 게이스터 블래스터(예고→발사)
typedef struct { int hp, maxHp, atk, def; int turn; } Sans;                     // 샌즈 상태
typedef struct { char name[20]; int heal; } Item;                              // 회복 아이템
typedef enum { EASY, NORMAL, HARD } Difficulty;
```

배열 사용: `Bone bones[MAX_BONES]`, `Blaster blasters[MAX_BLASTERS]`, `Item items[ITEM_COUNT]`,
`const char* dialogues[]`.

---

## 7. 함수 목록 (사용자 정의 5개+ → 20개+ 목표)

**시스템/렌더**: `initConsole()`, `gotoxy()`, `hideCursor()`, `clearScreen()`, `drawBox()`,
`drawSoul()`, `eraseAt()`, `typeText()`, `drawHpBar()`, `drawMenu()`

**화면**: `showTitle()`, `showDifficultySelect()`, `showGameOver()`, `showMercyEnding()`,
`renderSansSprite()`

**전투/턴**: `playerTurn()`, `enemyTurn()`, `fightAction()`, `actAction()`, `itemAction()`,
`mercyAction()`, `checkCollision()`

**공격 패턴**: `attackBones()`, `attackBlaster()`, `attackBlueJump()`, `attackBlueWhiteBones()`

**오디오**: `playBGM()`, `stopBGM()`

---

## 8. 공격 4패턴 (적 턴) 상세

1. **좌우 뼈 탄막** — 박스 좌/우/아래에서 뼈가 `rand()` 간격으로 솟거나 흐름. 영혼은 좌우/상하 회피.
2. **게이스터 블래스터** — 위치 잡고 예고선(점선) 깜빡 → 레이저 발사. `state`: 예고→발사→소멸 타이밍.
3. **파란 영혼 중력 점프** — 영혼 `isBlue=1`, 바닥 중력 적용, 스페이스로 점프해 굴러오는 뼈 넘기(플랫폼).
4. **파란/흰 뼈** — 흰 뼈=닿으면 데미지, 파란 뼈=영혼이 움직이는 중일 때만 데미지(정지 시 통과).
   영혼의 `prevX/prevY` 비교로 "움직임" 판정.

난이도(EASY/NORMAL/HARD)는 각 패턴의 **속도·생성 간격·지속 시간·플레이어 HP**를 스케일링한다.

---

## 9. 오디오

- `PlaySound(TEXT("assets/megalovania.wav"), NULL, SND_FILENAME|SND_ASYNC|SND_LOOP)` — 전투 루프.
- 게임오버/엔딩 시 `PlaySound(NULL, NULL, SND_PURGE)`로 정지 후 해당 트랙(선택).
- **제약**: `PlaySound`는 **WAV만 재생**. 사용자가 OST를 WAV로 변환해 `assets/`에 배치.

---

## 10. 프로젝트 구조

```
UndertaleSans/
├─ main.c                     ← 게임 본체 (소스 확장자 .c 필수)
├─ ImageLayer.h
├─ ImageLayerImpl.h           ← 콘솔 크기 설정(180×48 또는 조정)
├─ ImageLayerInterface.h
├─ ImageFading.h
└─ assets/
    ├─ megalovania.wav        (사용자 준비)
    ├─ title.bmp              (사용자 준비 / 임시 ASCII 대체 가능)
    ├─ sans.bmp / sans_blue.bmp
    └─ gameover.bmp
```

---

## 11. 사용자가 준비할 에셋

- **WAV 음악**(필수, AI가 다운로드 불가): Megalovania 등. mp3 → wav 변환.
- **BMP 이미지**(타이틀 로고, 샌즈 스프라이트, 게임오버): 작은 픽셀아트 BMP.
  없으면 ASCII 임시 버전으로 먼저 구현 후 교체.

---

## 12. 빌드 & 작업 방식

- Visual Studio에서 `main.c` 빌드. `#define _CRT_SECURE_NO_WARNINGS`, `#pragma comment(lib, "winmm.lib")`(PlaySound) 사용.
- ImageLayer는 동일 폴더 헤더 include, VS에서 검증된 라이브러리.
- **핑퐁 진행**: 개발 AI는 Mac이라 직접 실행 불가 → 사용자가 VS에서 빌드→실행→결과(스샷/에러) 공유 → 수정 반복.

---

## 13. 리스크 & 대응

- ImageLayer(전체 렌더) + 실시간 텍스트 혼용 깜빡임 → 정적 이미지 1회 렌더, 동적은 부분 갱신(이전칸 지우고 새칸).
- 콘솔 좌표계 정합 → 박스/메뉴/스프라이트 좌표를 상수로 먼저 고정.
- 실시간 입력 반응성 → `Sleep` 간격 + `_kbhit` 폴링 튜닝.

---

## 14. 범위 밖 (Out of Scope)

- 원작/Bad Time Simulator의 20+개 전체 공격 시퀀스 완전 재현.
- KARMA(KR) 정밀 수치 시스템 — 선택적 연출만 고려.
- 세이브/로드, 마우스 입력, 멀티 엔딩 분기(자비/게임오버 2종만).

---

## 15. 다음 단계

1. 이 스펙 사용자 검토.
2. `writing-plans` 스킬로 단계별 구현 계획 작성(좌표계 고정 → 화면 골격 → 메뉴 → 패턴 1개씩 → 오디오 → 엔딩 → 폴리시).
3. 구현 후 **게임 설명서(40점)** 별도 작성.
