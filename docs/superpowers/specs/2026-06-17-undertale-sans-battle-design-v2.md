# UNDERTALE — Sans Battle (콘솔 C) 설계 문서 **v2 (적대적 검증 반영)**

- **작성일**: 2026-06-17 · **마감**: 2026-06-26(금) 17:00 · **환경**: Windows + Visual Studio (MSVC), C
- **레퍼런스**: Bad Time Simulator (jcw87/c2-sans-fight)
- **이전 버전**: [v1](./2026-06-17-undertale-sans-battle-design.md) (검증 전)
- **검증**: 6차원 적대적 비평 → 2렌즈(Windows현실/채점의도) 반박검증 → 확정 결함 34개 반영

---

## 0. 무엇이 바뀌었나 (v1 → v2 핵심 변경 6가지)

| # | v1의 문제 | v2의 결정 |
|---|---|---|
| 1 | **전투 중 "상단 샌즈 BMP + 하단 실시간 텍스트 동시 렌더"** | ❌ 구조적 불가(GDI 휘발성). → **전투는 100% 텍스트. 샌즈도 ASCII 스프라이트.** ImageLayer/BMP는 *정적 입력대기 화면(타이틀·난이도·게임오버·엔딩)에만* 선택적 사용 |
| 2 | 화살표 입력을 선생님식 `_getch` 1회로 | → **`int`로 받고 `0/224` 프리픽스 명시 처리 + `while(_kbhit())` 버퍼 소진**. WASD+Space를 1순위 키맵 |
| 3 | 인코딩 전략 없음 (Mac UTF-8 → CP949 깨짐) | → **main 첫 줄 `SetConsoleOutputCP(65001)` + `/utf-8` 플래그 + UTF-8(BOM) 저장**, 특수문자는 1차 핑퐁에서 실제 렌더 검증, ASCII 폴백 보유 |
| 4 | 수치(박스/HP/데미지/난이도/턴) 전무 | → **상수 블록 + 난이도 표 확정**(§5) |
| 5 | 수평 레이어 순서, 수직 슬라이스 없음 | → **Day1 렌더 스모크테스트 + 끝-끝 수직 슬라이스 먼저 + cut list**(§9) |
| 6 | 에셋 상대경로, 깜빡임, 설명서 후순위 | → **exe기준 경로/더블버퍼/설명서 병렬·제출 체크리스트**(§6·§7·§10) |

> **그래픽 패러다임 원칙 (가장 중요):** *한 화면은 "정적=이미지" 또는 "동적=텍스트" 중 하나만.* 둘을 같은 화면에서 겹쳐 실시간으로 돌리지 않는다. 전투(동적)=텍스트, 타이틀/엔딩(정적·입력대기)=이미지(옵션).

---

## 1. 컨셉 & 채점 매핑 (변경 없음)

언더테일 샌즈전(Bad Time Simulator 느낌). 메뉴 턴제(FIGHT/ACT/ITEM/MERCY) + 적턴 탄막회피.
화면 4개: 타이틀 → 난이도선택 → 전투 → 엔딩/게임오버.

| 채점 항목 | 배점 | 충족 |
|---|---|---|
| 화면 3개+ | 20 | 4개 화면 (텍스트만으로도 충족) |
| 음악재생 | 5 | `PlaySound` BGM 루프 |
| 구조체 | 5 | `Soul/Bone/Blaster/Sans/Item/DiffConfig` |
| 랜덤함수 | 5 | 뼈·블래스터 `rand()` (시드 1회) |
| 사용자정의함수 5개+ | 5 | 20개+ |
| 키보드입력 | 5 | `_kbhit/_getch` (견고화) |
| 배열 | 5 | `bones[]/blasters[]/items[]/dialogues[]` |
| 게임 테스트 | 10 | 오류 없이 동작 |
| 주석 | 10 | §7 주석 컨벤션으로 6조건 태깅 |
| 게임 설명서 | 40 | §10 병렬 작성 |

> **안전마진:** 선택조건은 택4(20점)면 만점. **구조체·랜덤·함수5+·배열은 코드 작성만으로 자동 충족**되어 ImageLayer/음악을 빼도 4개는 확보됨 → 그래픽/오디오를 잘라도 화면구현 점수는 안전.

---

## 2. 그래픽/렌더링 전략 (대수술)

### 2.1 패러다임 분리
- **전투 화면 = 순수 텍스트 셀**(선생님 game.c 패러다임): `gotoxy`+출력, `_kbhit/_getch`, 고정 타임스텝 루프. 샌즈 상반신도 **여러 줄 ASCII 아트**로 그리고, "눈 번쩍"은 BMP 교체가 아니라 **`SetConsoleTextAttribute`로 셀 색 토글**(파란 눈) 또는 문자 토글로 표현 → 같은 셀 레이어 안이라 깜빡임 없음.
- **정적 화면(타이틀/난이도/게임오버/엔딩) = ImageLayer BMP (선택)**: `renderAll` 1회 후 `_getch` 대기. 프레임 루프가 없어 GDI가 안 지워짐(샘플 CodeTheCompany가 검증한 사용처). **이미지 위에 콘솔 텍스트를 겹치지 않음**(이미지 전용 화면).

### 2.2 인코딩 (치명 리스크 차단)
- `main()` 진입 즉시 `SetConsoleOutputCP(65001);` 호출. 소스는 **UTF-8 (BOM 포함)** 저장. VS 프로젝트 속성 → C/C++ → 명령줄에 **`/utf-8`** 추가(C4819 경고 제거).
- **특수문자 정책:** 폭/폰트 리스크가 있는 글자(♥ U+2665, 박스드로잉 ┌─┐)는 **1차 핑퐁 스모크테스트에서 실제 콘솔 스샷으로 렌더 확인 후** 채택. 안 보이면 **ASCII 폴백**:
  - 영혼 ♥ → `'v'` 또는 단일셀 안전글자 (확정 전까지 `SOUL_CH` 매크로로 한 곳에서 교체)
  - 박스 테두리 → `+ - |`
  - 뼈/빔 → `|`(세로뼈) `=`(가로빔) — **전부 단일셀 반각**으로 폭 문제 원천 제거(이동단위 1칸)

### 2.3 깜빡임 방지
- 프레임당 `system("cls")` **금지**.
- 기본: **prev/cur 문자 그리드 차분 갱신**(달라진 셀만 `gotoxy`+출력). 탄막 수가 많아지면 **`WriteConsoleOutput` 더블버퍼**로 승급(off-screen `CHAR_INFO[]` 작성 후 1회 전송).
- 커서 숨김(`clearCursor`), 콘솔 크기 **시작 시 1회만** 설정 후 변경 금지(§6.3).

---

## 3. 화면 흐름

```
[타이틀] (텍스트 로고 기본 / BMP 옵션) — 아무키 → [난이도 선택] EASY·NORMAL·HARD (♥ 커서)
   → [전투] 4턴 ── HP0 ──▶ [게임오버] 부서진 영혼 + "Stay determined..." → RETRY(난이도선택)/종료
                  └ 4턴 생존 → MERCY 활성 → 봐주기 → [자비 엔딩]
```

전투 화면 레이아웃(전부 텍스트 셀, 120×40 콘솔 예시):
```
            ( 샌즈 ASCII 상반신, 눈은 색 토글 )
   * heh heh heh... you're gonna have a bad time.      ← 대사 타이핑('* ' 후 한 글자씩)
                 +--------------------------------------+
                 |                  v                   |  ← 전투박스(테두리 ASCII), 영혼 회피
                 +--------------------------------------+
   CHARA   LV 19   HP [|||||||||||||......]  92/92         ← 노란 HP바(색), 텍스트
   [ FIGHT ]  [ ACT ]  [ ITEM ]  [ MERCY ]                 ← 선택 버튼 색 하이라이트
```

---

## 4. 전투 루프 (고정 타임스텝)

```c
// FRAME_MS = 16 (≈60fps). 모든 타이밍은 tick(프레임) 단위.
for (turn = 0; turn < 4; turn++) {
    flushInput();                 // 페이즈 전환마다 입력버퍼 비우기
    typeDialogue(turn);           // 대사 타이핑(스킵=_kbhit 1회만 허용)
    flushInput();
    enemyTurn(turn, diff);        // 적턴: 해당 패턴 탄막, hp<=0면 즉시 GAMEOVER
    if (player.hp <= 0) break;
    flushInput();
    playerTurn(&state);           // 내턴: FIGHT/ACT/ITEM/MERCY 메뉴
    survivedTurns++;
}
if (player.hp <= 0)      showGameOver();        // RETRY → 난이도선택
else if (mercyChosen)    showMercyEnding();     // 4턴 생존 후 MERCY
```

- **승리조건 단일 정의:** *샌즈 HP는 격파 불가(항상 1, FIGHT는 연출만 1뎀+회피)*. **유일한 승리 = 4턴 생존 후 MERCY 선택.** (HP격파 엔딩 없음 → 분기 혼선 제거)
- **적턴 종료조건(패턴별 명시):** 좌우뼈/파란흰뼈 = `turnTicks` 경과 / 블래스터 = 발사 N회(예 4발) 완료 / 파란점프 = 뼈 웨이브 M개 통과.
- **피격:** 충돌 시 `hp -= diff.hitDamage` **1회** 후 `invuln = 20`틱 부여(다중차감 방지, 영혼 깜빡임). `invuln>0`이면 데미지 무시.

---

## 5. 상수 블록 & 난이도 표 (확정 수치)

```c
#define FRAME_MS      16
#define CONSOLE_W     120
#define CONSOLE_H     40
#define BOX_X         40           // 전투박스 좌상단(셀)
#define BOX_Y         14
#define BOX_W         40
#define BOX_H         16
// 영혼 이동범위: x ∈ [BOX_X+1, BOX_X+BOX_W-2], y ∈ [BOX_Y+1, BOX_Y+BOX_H-2]
#define SOUL_CH       'v'          // ♥ 검증 후 교체 가능(한 곳에서)
#define INVULN_TICKS  20
#define MOVE_COOLDOWN 8            // 파란뼈 '이동중' 판정 윈도우(틱)
#define GRAVITY_T     1            // 파란점프: 틱당 vy += 1 (1/10셀 단위 권장)
#define JUMP_VY      -4
#define MERCY_TURNS   4            // 이 턴 수 생존 시 MERCY 활성

typedef struct { int maxHp, spawnInterval, bulletEveryTicks, turnTicks, hitDamage; } DiffConfig;
// EASY / NORMAL / HARD
DiffConfig DIFF[3] = {
    {120, 18, 3, 560, 3},   // EASY  (turnTicks*16ms ≈ 9.0s)
    { 92, 12, 2, 680, 5},   // NORMAL(≈10.9s)
    { 70,  8, 1, 800, 8},   // HARD  (≈12.8s)
};
```
- 전각문자(♥/■)를 쓰기로 확정되면 이동단위 `dx=2`, 충돌 `|bone.x-soul.x|<2`, 지움은 2칸 공백. **기본은 단일셀 반각이라 dx=1.**

---

## 6. 입력 · 타이밍 · 경로 (견고화)

### 6.1 입력 모델 (`readInput()` 한 곳에 고정)
```c
// _getch는 int 반환. 확장키(화살표)는 0 또는 224 프리픽스 + 스캔코드 2바이트.
while (_kbhit()) {                 // 한 프레임에 쌓인 입력 전부 소진
    int c = _getch();
    if (c == 0 || c == 224) {      // 확장키
        int sc = _getch();
        switch (sc){ case 72:up; case 80:down; case 75:left; case 77:right; }
    } else {                       // 단일바이트
        switch (tolower(c)){ case 'w':up; case 's':down; case 'a':left; case 'd':right; case ' ':jump; }
    }
}
```
- **WASD+Space를 1순위**(단일바이트라 타이밍 안전). 화살표는 보조.
- `flushInput()`: `while(_kbhit()) _getch();` — 페이즈 전환마다 호출(메뉴 키 누수/타이핑 폭주 방지).
- **파란뼈 '이동중' 판정:** 위치변화(prevX/Y)가 아니라 **`GetAsyncKeyState`로 키 홀드 직접 폴링**(가장 견고) 또는 방향입력 시 `moveCooldown=MOVE_COOLDOWN`로 세팅 후 매틱 감소, `moveCooldown>0`이면 이동중. (user32는 시스템 라이브러리 → 제약 위반 아님)

### 6.2 파란 영혼 중력 점프 (텍스트 그리드 물리)
- 위치/속도를 **float**(`soulYf`, `vyf`)로 내부 보관, 렌더 시 `(int)round`로 셀에 찍음.
- 매틱: `vyf += GRAVITY; soulYf += vyf;` → `BOX_TOP/BOX_BOTTOM`에서 clamp. `onGround && Space`일 때만 `vyf = JUMP_VY`.
- 콘솔 셀 세로:가로 ≈2:1 → 수직값을 수평의 1/2로 튜닝(체공감 보정). 바닥 = `BOX_Y+BOX_H-2`.

### 6.3 콘솔 크기 소유권
- 시작 즉시 콘솔 크기 **1회** 설정(`mode con` 또는 `SetConsoleScreenBufferSize`+`SetConsoleWindowInfo`로 `CONSOLE_W×CONSOLE_H`) → 이후 **절대 리사이즈 금지**(난이도/화면 전환에서도).
- ImageLayer를 쓸 경우: `ImageLayerImpl.h`의 `CONSOLE_WIDTH/HEIGHT`를 **같은 값으로** 맞추고, **콘솔 크기 확정 후에** ImageLayer 초기화 1회. (초기화 후 리사이즈하면 캐싱된 `WINDOW_WIDTH`와 어긋나 이미지 깨짐)

### 6.4 에셋 경로 (무음/미표시 버그 차단)
- **권장:** `GetModuleFileName(NULL,...)`로 exe 폴더를 구해 **그 폴더 기준 절대경로**를 조립해 `PlaySound`/`LoadImage`에 전달 → F5 디버그/exe 더블클릭/채점자 실행 모두 동작.
- VS 프로젝트 속성 → Debugging → Working Directory = `$(ProjectDir)`로 명시.
- `PlaySound`는 실패해도 **조용히 무음**이므로, 로드 직후 반환값을 1차 핑퐁에서 검증.
- `srand((unsigned)time(NULL))`는 **`main` 시작 시 딱 1회**. 패턴/턴 함수 내 재시드 금지(같은 초 반복 → 탄막 고정 버그).

---

## 7. 데이터 구조 · 함수 · 주석 컨벤션

### 7.1 구조체 / 배열
```c
typedef struct { float xf, yf, vyf; int x, y, isBlue, onGround, invuln, moveCooldown; } Soul;
typedef struct { int x, y, dir, active, isBlue; } Bone;
typedef struct { int x, y, dir, state, timer; } Blaster;     // state: 예고→발사→소멸
typedef struct { int hp; int turn; } Sans;
typedef struct { char name[20]; int heal; } Item;
typedef enum { EASY, NORMAL, HARD } Difficulty;
Bone bones[MAX_BONES]; Blaster blasters[MAX_BLASTERS]; Item items[ITEM_COUNT];
const char* dialogues[4];
```

### 7.2 함수 (20개+) — 신규 강조
시스템/렌더: `initConsole` `gotoxy` `hideCursor` `setColor` `drawBox` `drawSoul` `renderFrame`(차분/더블버퍼) `typeDialogue` `drawHpBar` `drawMenu`
입력/물리: **`readInput`** **`flushInput`** **`physicsTick`** `checkCollision`
화면: `showTitle` `showDifficulty` `showGameOver` `showMercyEnding` `renderSansAscii`
턴/액션: `enemyTurn` `playerTurn` `fightAction` `actAction` `itemAction` `mercyAction`
패턴: `attackBones` `attackBlaster` `attackBlueJump` `attackBlueWhiteBones`
오디오: `playBGM` `stopBGM` · 경로: `assetPath`

### 7.3 주석 컨벤션 (10점 만점 안전화)
- 파일 상단에 **"구현조건 충족 목차"** 주석 블록(조건 → 대표 함수/라인 매핑).
- 각 충족 지점 위에 머리표 주석:
  `// [구현조건: 구조체] Soul/Bone/... 사용자 정의 타입`
  `// [구현조건: 랜덤함수] rand()로 뼈 생성 간격`
  `// [구현조건: 배열] bones[] 운용`
  `// [구현조건: 키보드입력] _kbhit/_getch (확장키 0/224 프리픽스 처리)`
  `// [구현조건: 음악재생] PlaySound 루프`
  `// [구현조건: 사용자정의함수] (20개 중 하나)`
- **6개 모두 태깅** → "5개 만점" 기준을 안전 초과.

---

## 8. 공격 4패턴 (구체 규칙)

1. **좌우 뼈** — `spawnInterval`틱마다 박스 좌/우/아래에서 `|`뼈 생성(`rand()` 위치), `bulletEveryTicks`마다 1칸 이동. 종료 = `turnTicks`.
2. **게이스터 블래스터** — 위치 잡고 `state:예고`(점선 깜빡 N틱) → `발사`(`=`빔 라인) → `소멸`. **4발** 완료 시 종료. 예고 동안 비키면 안전.
3. **파란 영혼 중력 점프**(§6.2) — 영혼 `isBlue=1`, 중력 적용, `Space`로 굴러오는 뼈 넘기. 뼈 웨이브 M개 통과 시 종료. *구현난도 최고 → cut list 2순위.*
4. **파란/흰 뼈** — 흰뼈=닿으면 피해, 파란뼈=**이동중(§6.1)일 때만** 피해. 종료 = `turnTicks`.

> 난이도는 `DIFF[]` 표의 `spawnInterval/bulletEveryTicks/turnTicks/hitDamage`로 스케일(프레임수가 아닌 상수 기반).

---

## 9. 구현 순서 — 수직 슬라이스 우선 + 핑퐁 스모크테스트

- **Day 0 (오늘): 렌더 스모크테스트** (`smoke.c`, 최소): ① `SetConsoleOutputCP(65001)` 후 한글 대사 1줄 + `SOUL_CH`/박스문자 출력 → **인코딩/글리프 검증**. ② (ImageLayer 쓸 경우) sans.bmp 1회 렌더 → 표시 확인. → 사용자 VS 빌드/스샷 1회. **특수문자·이미지 가부를 여기서 확정.**
- **Day 1–2: 수직 슬라이스 1줄** — 텍스트 타이틀 → 난이도 1개 고정 → 전투 진입 → **좌우뼈 1패턴 회피**(영혼 이동+충돌+HP감소+i-frame) → HP0 게임오버 → 종료. *오디오·BMP·대사타이핑·나머지 3패턴·MERCY 전부 제외.* 이게 **무오류로 돌면 채점 바닥(테스트10+화면다수+입력/충돌) 확보.**
- **Day 3–5: 가지치기** — 블래스터 패턴 → 메뉴(FIGHT/ITEM 먼저) → 대사 타이핑 → BGM → 난이도 3종 → 게임오버/엔딩 연출.
- **Day 6: 파란점프·파란/흰뼈 + ACT/MERCY** (난도 높은 것 후반).
- **Day 7 (6/24): 폴리시 + ImageLayer 정적화면(옵션) + 클린빌드 리허설**(압축 풀어 더블클릭 빌드→실행).
- **Day 8 (6/25): 설명서 마무리 + 제출 리허설.** 6/26 17시 마감 전 버퍼.

### Cut list (일정 압박 시 위→아래로 버림)
1. **ImageLayer/BMP 전부** → ASCII 로고/샌즈로 대체(화면 20점 유지)
2. 공격 패턴 3·4번(파란점프·파란흰뼈) → 좌우뼈+블래스터 2패턴만 확실히
3. MERCY/ACT 분기 연출 → FIGHT/ITEM만
4. 난이도 3종 → 1종 또는 HP만 바뀌는 형식 분기

**절대 사수:** 화면 3개+, 텍스트 탄막 1~2패턴 **무오류**, 선택조건 4개(구조체·rand·함수5+·키보드·배열), 주석, 설명서.

---

## 10. 게임 설명서(40점) & 제출 (병렬 진행)

- **지금 목차 확정, 핑퐁 스샷을 즉시 적재**: ①게임 소개 ②조작키 표(WASD/화살표/Space/Enter) ③화면별 설명+스샷 ④승패조건 ⑤난이도 차이 ⑥**구현조건 6개가 코드 어디서 충족되는지 표**.
- **제출 체크리스트:** `main.c` + 사용한 모든 `.h`(ImageLayer 4종은 사용 시) + `assets/`(BGM wav, 사용 BMP 전부)가 압축에 포함 / `.DS_Store`·`.git`·`.vs` 제외 / **클린 환경에서 압축 풀어 빌드→실행 리허설(6/24)** / WAV가 실제로 소리나는지 확인.

---

## 11. 사용자 확인/준비 사항 (열린 결정)

1. **그래픽 전략(핵심 결정):** (A) **ASCII 우선 + 정적화면에만 ImageLayer 이미지 폴리시**(추천: 이미지 살리되 견고) / (B) **전부 ASCII**(가장 안전·빠름) / (C) 전투에도 BMP(비추천).
2. **ImageLayer 허용 재확인:** 사용자가 "선생님이 이미지 띄우기는 허용"이라 했음 → **`Msimg32.lib`(TransparentBlt) 링크까지 포함해 외부라이브러리 금지에 안 걸리는지** 한 번 더 확인 권장(걸려도 cut list 1번으로 무손실 대체 가능).
3. **음악:** BGM `.wav` 준비 가능? (`PlaySound` WAV 루프. mp3는 `mciSendString`로도 가능하나 루프 처리 복잡) — 없으면 무음으로 골격 먼저.
4. **특수문자:** ♥/■ 쓸지 ASCII(`v`,`|`)로 갈지는 Day0 스모크테스트 스샷으로 확정.

---

## 12. 적대적 검증 요약 (34개 확정 결함 출처)

6차원(렌더/입력/루브릭/일정/게임플레이/빌드) × 적대적 비평 → 2렌즈 반박검증.
양 렌즈 confirmed(최우선): `gdi-text-coexist-wipe`, `arrow-224-prefix-single-getch`, `input-buffer-residue`, `hybrid-gdi-realtime-conflict`, `vertical-slice-first`, `cut-list-priority`, `blue-white-bone-moving-judgement`, `bmp-format-loadimage`. 전체 목록은 워크플로 결과(`tasks/wx3y315s2.output`)에 보존.
