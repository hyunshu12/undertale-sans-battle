# Bad Time Simulator 풀 이식 설계 — Win32 GDI VM 포팅

- **작성일**: 2026-06-17 · **마감**: 2026-06-26(금) 17:00
- **결정**: BTS(Jcw87/c2-sans-fight) **전체를 "풀 VM 이식"** — 24개 공격 CSV를 그대로 해석/재생, 전 대사 한국어
- **기반**: 현재 `UndertaleSans/main.c` (Win32 GDI, 더블버퍼, 고정 1/60, 한글 유니코드 + 네오둥근모)
- **근거 분석**: BTS 소스(AttackLoader/Timeline/Battle/InputManagement XML + 24 CSV) 6축 리버스엔지니어링

---

## 0. 핵심 결론

- BTS 공격은 전부 **24개 CSV(약 970줄, ~45 opcode)** 로 외재화돼 있고, **좌표계가 이미 640×480 절대좌표로 우리와 1:1 일치**(변환식 0). 따라서 신규 작업의 90%는 4개 모듈에 집중:
  1. **Timeline VM 인터프리터** (`vm.c`)
  2. **게임 spawn 명령 ~12개** (`hazards.c`: BoneV/Repeat/GasterBlaster/Platform/BoneStab/SineBones…)
  3. **BLUE 영혼 중력/점프 물리** (`main.c`)
  4. **동적 CombatZone 리사이즈** (`main.c`)
- **중요 정정**: `AttackLoader.xml`은 로더일 뿐. 실제 인터프리터는 **`Timeline.xml`**(`Timeline`/`TimelineCPU` 그룹).
- 최대 리스크: GasterBlaster 회전빔 OBB 충돌, JMP `-1` 보정/`$`치환 타이밍, BLUE 물리. 그리고 **개발자 Mac → 윈도우 빌드 핑퐁**이 일정의 생명선.

---

## 1. 아키텍처

### 1.1 파일 분리 (단일 main.c → 4파일)
```
UndertaleSans/
├─ game.h     ← 공용 구조체/전역 선언 (gBox, gVM, gBones[], gBlasters[], gPlatforms[], gSoul …)
├─ vm.c       ← Timeline 인터프리터 + CPU 명령 + 제어흐름
├─ hazards.c  ← 게임 명령(뼈/블래스터/플랫폼/샌즈) + 매프레임 탄막 update/render/충돌
├─ main.c     ← 창·게임루프·상태머신·CombatZone·HeartMode 물리·대사 채널·렌더
└─ assets/attacks/  ← 24개 sans_*.csv (런타임 로드)
```
- `#pragma comment(lib/linker)`는 `main.c`에만 유지. **vcxproj에 `vm.c`/`hazards.c` ClCompile 2개 추가**.
- CSV는 **빌드 임베드 아님 — 런타임 `fopen/fread`** 로 `assets/attacks/`에서 로드(스프라이트/폰트와 동일 `assetPath`). CSV 수정 시 재컴파일 불필요.

### 1.2 VM 자료구조
```c
typedef struct { float delay; char cmd[24]; char arg[9][32]; int argc; } Instr;
typedef struct {
    Instr code[256];      int n;
    struct { char name[24]; double val; } vars[64];  int nvars;   // 초기 {"pi", M_PI}
    struct { char name[24]; int line; }   labels[32]; int nlabels; // 1기반 줄번호
    int   pc;             // 1기반 program counter
    float t;              // 누적 시간 카운터
    int   running;        // 0/1
    int   guard;          // 무한루프 방지(>1000 → running=0)
} VM;
```

### 1.3 좌표계
- BTS 640×480 절대좌표를 **픽셀 그대로** 사용(변환 없음). 기존 고정 `BOX_*` 매크로 → **`gBox{x,y,w,h}` 전역화**.
- 각도는 전부 **도(degree)** → C `sin/cos/atan2`는 라디안 → 반드시 `*M_PI/180` 변환. 방향 0~3은 룩업 `{{+1,0},{0,+1},{-1,0},{0,-1}}`.

---

## 2. VM 실행 모델 (정확 명세 — 원본 글자 그대로 일치 필수)

### 2.1 로드(TLPlay)
1. CSV 텍스트를 줄 단위로 `code[]`에 파싱. **토큰화는 `strtok` 금지**(연속 콤마=빈 인자 보존) → 수동 콤마 스캔. `delay=atof(컬럼0)`, `cmd=컬럼1`, `arg[0..]=컬럼2..`.
2. **라벨 1패스 사전스캔**: `cmd[0]==':'` 이면 `labels[cmd+1] = (줄번호 i+1, 1기반)`. 라벨 줄도 `code[]`에 남되 실행 시 디스패치만 스킵.
3. `pc=1, t=0, running=1`.

### 2.2 매 프레임 스텝
```
if (running) t += dt;
guard = 0;
while (running && pc>=1 && pc<=n && t >= code[pc-1].delay && guard++ < 1000) {
    resolveArgs(pc);                 // 각 arg가 '$'면 vars_get(arg+1)로 치환
    if (code[pc-1].cmd[0] != ':')    // 라벨 줄은 디스패치 건너뜀
        dispatch(cmd, resolvedArgs);
    t -= code[pc-1].delay;           // 이 줄 지연만큼 차감
    if (!jumpedThisLine) pc++;       // JMP가 pc를 세팅했으면 pc++ 스킵
    loadLine(pc);                    // 다음 줄 $치환 준비
}
```
- **시간 컬럼 = 직전 줄 실행 후 이 줄까지의 상대 지연(초)**. `delay=0` 줄은 같은 틱에 연쇄 실행(한 틱에 다수 뼈 동시 생성).
- `guard>1000` → `running=0` + panic(무한루프 방지).
- `EndAttack`는 VM 특수명령 아님 — 일반 게임함수(메뉴 enable, 하트 정리). 타임라인은 `pc>n` 되면 자연 종료.

### 2.3 CPU 명령 (vm.c)
| 명령 | 동작 | 주의 |
|---|---|---|
| `SET dst v` | vars[dst]=v | |
| `ADD/SUB/MUL/DIV/MOD dst a b` | vars[dst]=a op b | 3-주소. `ADD Jump $Jump 1`=증가 |
| `FLOOR dst x` | floor(x) | |
| `SIN/COS dst x` | sin/cos(x) | **x는 도** → `sin(x*π/180)` |
| `DEG/RAD dst x` | x*180/π / x*π/180 | |
| `ANGLE dst x1 y1 x2 y2` | (x1,y1)→(x2,y2) 방향각(도) | `atan2(y2-y1,x2-x1)*180/π`, C2 부호기준 확인 |
| `RND dst n` | floor(random(n)) = 0~n-1 정수 | `rand()%(int)n`, 0/음수 가드 |
| `GetHeartPos dx dy` | vars[dx]=heart.x, vars[dy]=heart.y | 2변수 동시 |

### 2.4 제어흐름 (vm.c)
- `JMPABS t` : `t`가 숫자면 `pc = atoi(t)-1`(다음 pc++로 보정), 라벨이면 `pc = labels[t]-1`, 없으면 `running=0`+panic.
- `JMPREL off` : `pc += atoi(off)-1`.
- 조건점프 `JMPxx target a [b]` → 조건 참이면 `JMPABS(target)`:
  `JMPZ a==0` · `JMPNZ a!=0` · `JMPE a==b` · `JMPNE a!=b` · `JMPL a<b` · **`JMPNL a>=b`** · `JMPG a>b` · **`JMPNG a<=b`** (C2 Compare enum 매핑 주의).
- **골든 테스트 케이스**: `sans_randomblaster1.csv`(JMPNL/JMPNG 다수), `sans_multi1.csv`(JMPREL 점프테이블) — 단계별 vars 로그로 검증.

---

## 3. 서브시스템 명세 (요약 — 구현 시 로컬 소스/CSV 재확인)

### 3.1 영혼 물리 (HeartMode)
- `HeartMode 0=RED`(자유 이동) / `1=BLUE`(중력+점프). `HeartTeleport x y` 순간이동. `HeartMaxFallSpeed v`.
- BLUE: **가변중력 구간(DownSpeed 540/180/450/180)** + MaxFall 클램프, **점프 임펄스 180 + 가변높이컷 30**, 입력 엣지(Last키) 추적.
- 경계 클램프는 단순 min/max가 아니라 **축별 px 서브스텝**(솔리드 시 정지, 터널링 방지).
- 무적: `LastDamageTime + 0.033` 시간게이트.

### 3.2 색 판정 + KARMA
- 탄막 Color: `0=흰`(항상 피해) / `1=파랑`(이동 중에만) / `2=주황`(정지 중에만). **'이동중' = 실제 속도>0**.
- **KR(카르마)**: 피격 시 즉시 데미지 일부 + KR 누적, 5단계 비선형 드레인(0.033/0.066/0.166/0.5/1.0). HP바에 흰=HP, 보라=KR 잔량.

### 3.3 CombatZone(박스)
- `CombatZoneResize l t r b [TLResume]` → `gBox = {l, t, r-l, b-t}` 로 **4변 독립 트윈**(speed 기본 480/30). 마지막 인자 `TLResume`이면 도달 시 `running=1` 콜백.
- `CombatZoneResizeInstant` 즉시 스냅. `CombatZoneSpeed` 속도 변경. 리사이즈 중 영혼 클램프.

### 3.4 탄막 (hazards.c)
- `BoneV/BoneVRepeat/BoneHRepeat` 세로/가로 뼈(+반복 생성), `SineBones` sine offset, `BoneStab`+`BoneStabWarn` 4단계(WARN→STAB_IN(speed=Dist*10)→HOLD→STAB_OUT), `Platform/PlatformRepeat`(BLUE 착지 발판, 데미지X), `SansSlam`(BLUE 강제 + angle*90 슬램).
- **GasterBlaster**: 상태머신 ENTER(지수보간 k=dt*10)→WAIT(Timer)→FIRE(0.1s 빔)→LEAVE(가속 후퇴). Size 0/1/2 스케일. **회전빔 = 입에서 angle방향 1000px 직선 vs 16×16 하트(점-선분 거리), karma=10**. GDI 회전 사각형 렌더.

### 3.5 전투 흐름
- `StartAttack` 페이즈 디스패처 + **인트로 테이블 14 / 멀티 테이블 9**(중복 포함) 정확 복제. `HitAttempts` 13/22 경계로 페이즈 전환. FIGHT→회피 MISS→`HitAttempts++` 턴 루프.
- 자비 트릭(`sans_spare`) → 마지막 공격(`sans_final`) → 지침(huff/puff) → 승리.

---

## 4. 단계별 구현 계획 (각 단계 = 동작하는 게임 증분)

### Slice 3 — Timeline VM 코어 + 기존 탄막을 CSV로 구동
- `game.h`/`vm.c`/`hazards.c` 분리, vcxproj ClCompile 2개 추가, 하드코딩 `spawnBone/Blaster/updateEnemyPhase` 대체 경로.
- `Instr` 사전파서(수동 콤마 스캔), `TLPlay`(줄 빌드 + 라벨 1패스), 실행 while루프($치환·t누적·delay차감·pc++·guard1000·delay0 연쇄).
- CPU 명령 전부 + 제어흐름(JMP 8종, 도단위 변환). 최소 게임명령 `BoneV/BoneVRepeat/BoneHRepeat + EndAttack + TLPause/TLResume`.
- `assets/attacks/`에 24 CSV 복사 + 런타임 로더. 전투 진입 시 `sans_bonegap1` 구동.
- **완성 상태**: 실제 `sans_bonegap` CSV가 VM으로 해석돼 뼈가 정확한 위치/타이밍에 생성, EndAttack로 메뉴 복귀. "하드코딩 → CSV 데이터 구동" 완전 대체.

### Slice 4 — 동적 박스 + BLUE 물리 + 색판정 + 플랫폼
- `CombatZoneResize/Instant/Speed` 트윈(4변 보간, TLResume 콜백). 고정 BOX 매크로 제거.
- `HeartTeleport/HeartMode/HeartMaxFallSpeed`, BLUE 가변중력+점프, 축별 px 서브스텝 클램프.
- Color 판정(흰/파랑/주황) + 무적 시간게이트 + KR 카르마.
- `Platform/PlatformRepeat`, `SansSlam`, `SineBones`.
- `StartAttack` 페이즈 디스패처 + 인트로/멀티 테이블 + HitAttempts 턴루프.
- **완성 상태**: bluebone/platforms/multi 등 BLUE·플랫폼·색뼈 공격 정상, 13턴 자비트릭·페이즈 전환 동작 → 사실상 완주 가능.

### Slice 5 — GasterBlaster 회전빔 + BoneStab + 한국어 대사 + 엔딩
- GasterBlaster 상태머신 + 회전빔 OBB 충돌(점-선분 거리) + GDI 회전 사각형 렌더.
- `BoneStab`+Warn 4단계.
- **대사 3채널 한국어화**: SansText(말풍선), CombatZone.InfoText(턴별 플레이버), DamageFont('빗나감!'). 채널별 폰트/위치 분리.
- `sans_final` 종료 → huff/puff/win 한국어 엔딩. KR HP바.
- `Sound`(Flash/Ding/GasterBlaster/Slam)→PlaySound, `BlackScreen`.
- 24 CSV 전체 통과 회귀 핑퐁으로 타이밍/충돌 미세조정.
- **완성 상태**: 인트로→멀티→sans_final 24개 공격 전부 원작 타이밍, 회전빔·찌르기 정상, 전 대사 한국어, 승리 엔딩까지 완주하는 **풀 샌즈전**.

---

## 5. 대사 한국어화 (추출본 — Slice 5에서 전수 적용)
샌즈 말풍선/플레이버 주요 추출:
- `ready?` → "준비됐어?" · `here we go.` → "자, 간다." · `huff... puff...` → "헉... 헉..." · `alright, i guess you win.` → "그래, 네가 이긴 걸로 하지."
- `* You felt your sins crawling on your back.` → "* 등 뒤로 죄가 기어오르는 것이 느껴진다." · `* KARMA coursing through your veins.` → "* 카르마가 혈관을 타고 흐른다." · `* You feel like you're going to have a bad time.` → "* 끔찍한 시간이 될 것 같은 예감이 든다."
- `* Sans is starting to look really tired.` → "* 샌즈가 꽤 지쳐 보이기 시작한다." · `* Sans is getting ready to use his special attack.` → "* 샌즈가 특수 공격을 준비하고 있다."
- `* SANS 1 ATK 1 DEF` / `* The easiest enemy.` / `* Can only deal 1 damage.` → "* 샌즈  공격 1  방어 1" / "* 가장 약한 적." / "* 1의 피해만 줄 수 있다." · `MISS` → "빗나감!"
> Slice 5에서 Battle.xml/RPGText.xml를 전수 grep해 누락 대사까지 완역. (WebGL 폴백 안내문 등 불필요분은 제외.)

---

## 6. 최우선 리스크 & 대응
1. **GasterBlaster 회전빔**(임의각 1000px OBB + 지수보간 + Size + 페이드) — 가장 크고 현재 가로빔과 완전 다름, 8개 CSV 의존. → Slice5 격리, 1차는 '점-선분 거리<빔두께/2' + 위치/각도 즉시 스냅으로 동작부터, 핑퐁 후 보간/페이드 점증.
2. **JMP `-1` 보정/`$`치환 타이밍/C2 비교 enum**(JMPNL=≥, JMPNG=≤) — 한 군데 틀리면 동적 루프 통째로 깨짐. → 'delay차감→pc++→loadLine' 순서를 원본 글자 그대로, JMP시 pc++ 스킵 명시 분기. 골든 CSV 변수 로그 검증.
3. **Mac→윈도우 핑퐁 지연** — 9일 일정 최대 변수. → 각 슬라이스를 항상 컴파일·실행되는 증분으로, **VM 디버그 오버레이(현재 CSV명/pc/주요 vars/탄막수)** 토글로 스샷 1장 디버깅. CSV 없으면 Slice2 패턴 폴백 → 빌드 절대 안 깨지게.
4. **BLUE 물리**(가변중력·서브스텝 클램프·입력 엣지) — platforms/bluebone/multi 전제. → Slice4 독립 구현·핑퐁 후 색판정 부착. 시간 부족 시 단일 중력값으로 단순화(서브스텝은 유지).

## 7. Cut 우선순위 (마감 압박 시 위→아래로)
1. GasterBlaster ENTER/LEAVE 지수보간·SineSize 진동·페이드 (즉시 스냅 + 단순 빔)
2. sans_final의 SansBody 손동작·SansSweat·Tired 애니 (head 스프라이트 토글 대체)
3. KR 5단계 드레인 정밀 임계 (단일/2단계로 단순화)
4. Platform 그림자·BlackScreen 플래시 등 순수 장식
5. SineBones·BoneHRepeat·boneslide 등 후반/중복 공격 일부 (가까운 동작으로 폴백)
6. 14턴+ 랜덤 choose의 fast/hard 변형 (고정 시퀀스만 유지)
7. CombatZone 4변 독립 보간 미세보정 (단순 트윈/Instant 스냅)

> **절대 사수**: 빌드/실행 무오류, VM이 최소 수 개 CSV 정확 구동, BLUE 점프 1패턴, 메뉴/턴/엔딩, 전 대사 한국어, 디버그 오버레이.

## 8. 채점 영향
구현 40(화면·6조건)·테스트 10·주석 10은 현 Slice2로 이미 충족. 본 이식은 **완성도/임팩트**를 끌어올림. 게임 설명서(40)는 별도 — 이식 진행과 병렬로 스샷 축적.

---

## 9. 다음 단계
1. 사용자 스펙 검토.
2. `writing-plans`로 Slice 3 상세 구현계획 → 첫 코드(파일 분리 + VM 코어 + sans_bonegap 구동).
3. 로컬 BTS 소스(`/tmp/bts_src` — 휘발성)는 구현 착수 시 재다운로드, 24 CSV는 repo `assets/attacks/`에 영구 동봉.
