# Slice 3: Timeline VM 코어 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline) 로 task별 진행. 체크박스(`- [ ]`)로 추적.

**Goal:** BTS의 CSV 바이트코드를 해석하는 Timeline VM 인터프리터를 만들어, 실제 `sans_bonegap` CSV가 게임에서 정확한 위치/타이밍에 뼈를 생성하고 메뉴로 복귀하게 한다.

**Architecture:** VM 인터프리터(`vm.c`)는 **순수 C(windows.h 무의존)** 로 작성해 **Mac clang으로 단위테스트(TDD)** 한다. 게임 명령(BoneV 등)·하트좌표는 `VMHost` 콜백으로 분리해, 테스트는 stub으로 기록하고 실게임은 hazards/soul로 라우팅한다. Windows 통합(main.c/hazards.c)만 사용자 빌드 핑퐁으로 검증.

**Tech Stack:** C (MSVC/x64 게임, Mac clang 테스트), Win32 GDI(기존), CSV 데이터(assets/attacks/).

## Global Constraints
- 소스 확장자 `.c` 필수, 외부 라이브러리 금지(Win32 시스템 lib만). 좌표 BTS 640×480 절대좌표 1:1.
- 각도는 도(degree) → C math는 라디안, `sin/cos/atan2`에 `*M_PI/180` 변환.
- CSV 토큰화 `strtok` 금지(연속 콤마=빈 인자 보존), 수동 콤마 스캔.
- VM 실행 순서 원본 일치: `t>=delay` → resolve($치환) → (라벨 아니면)dispatch → `t-=delay` → JMP면 pc직접/아니면 pc++ → loadLine. guard>1000 정지.
- 빌드 절대 안 깨지게: CSV 없거나 RunAttack 실패 시 기존 Slice2 패턴 폴백.

---

## File Structure
- **Create `UndertaleSans/vm.h`** — 순수 C 공개 API: `Instr`, `VM`, `VMHost`, `vm_load/vm_step/vm_resume/vm_get_var/vm_is_running`. (windows.h 무의존)
- **Create `UndertaleSans/vm.c`** — 인터프리터 구현(파서·라벨스캔·$치환·CPU·제어흐름·스텝). 순수 C.
- **Create `UndertaleSans/test_vm.c`** — Mac 단위테스트 하버(assert + stub host). 게임에 미포함, Mac 전용.
- **Create `UndertaleSans/game.h`** — 게임측 공용(Bone/Blaster/Platform struct, gBox, gSoul, 하트모드). windows.h 사용 가능. main.c/hazards.c용.
- **Create `UndertaleSans/hazards.c`** — 게임 명령 구현(BoneV/BoneVRepeat/BoneHRepeat/CombatZoneResize(임시스냅)/HeartTeleport/HeartMode/EndAttack) + 뼈 update/render/충돌.
- **Modify `UndertaleSans/main.c`** — vm.h/game.h include, `VMHost` 구성(on_command→hazards, get_heart_pos→soul), PH_ENEMY에서 `RunAttack("sans_bonegap1")`, 뼈 렌더를 hazards로 위임, 디버그 오버레이.
- **Modify `UndertaleSans/UndertaleSans.vcxproj`** — `<ClCompile Include="vm.c" />`, `<ClCompile Include="hazards.c" />` 추가.

---

## vm.h 공개 인터페이스 (확정)
```c
#ifndef VM_H
#define VM_H
#define VM_MAX_LINES 512
#define VM_MAX_VARS  80
#define VM_MAX_LABELS 48
#define VM_MAX_ARGS  9
#define VM_ARG_LEN   40

typedef struct { float delay; char cmd[24]; char arg[VM_MAX_ARGS][VM_ARG_LEN]; int argc; } Instr;

typedef struct VMHost {
    /* 게임 명령(CPU/제어 외 전부): 이미 $치환된 args 전달 */
    void (*on_command)(void* ctx, const char* cmd, char args[][VM_ARG_LEN], int argc);
    /* GetHeartPos용 하트 좌표 조회 */
    void (*get_heart_pos)(void* ctx, double* x, double* y);
    void* ctx;
} VMHost;

typedef struct {
    Instr code[VM_MAX_LINES]; int n;
    struct { char name[24]; double val; } vars[VM_MAX_VARS]; int nvars;
    struct { char name[24]; int line; } labels[VM_MAX_LABELS]; int nlabels;
    int   pc;        /* 1기반 */
    float t;
    int   running;
    int   finished;
    VMHost host;
} VM;

void   vm_load(VM* vm, const char* csvText, VMHost host); /* 파싱+라벨스캔+pc=1,t=0,running=1 */
void   vm_step(VM* vm, float dt);                          /* 한 프레임 진행 */
void   vm_resume(VM* vm);                                  /* host가 리사이즈 완료 시 호출(running=1) */
double vm_get_var(VM* vm, const char* name);
int    vm_is_running(VM* vm);
#endif
```
**game.h가 hazards/main에 노출하는 것 (Produces):** `extern Box gBox; extern Bone gBones[]; void haz_on_command(...); void haz_update(float dt); void haz_render(HDC); int haz_bone_count();` (Task 6에서 확정).

---

## Task 1: CSV 한 줄 파서 (parseLine) — Mac TDD

**Files:** Create `vm.h`, `vm.c`(parseLine만), `test_vm.c`. 
**Interfaces — Produces:** `int vm__parse_line(const char* line, Instr* out)` (vm.c 내부, 테스트 위해 비static로 선언; 반환=성공). 빈 인자 보존, delay=atof(col0), cmd=col1, arg[]=col2..

- [ ] **Step 1: 실패 테스트 작성** (`test_vm.c`)
```c
#include "vm.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int vm__parse_line(const char* line, Instr* out);  /* 내부 노출 */
static void test_parse(void){
    Instr in;
    /* 연속 콤마(빈 인자) 보존 확인 */
    int ok = vm__parse_line("0.2,BoneVRepeat,128,257,95,0,180,8,120", &in);
    assert(ok);
    assert(in.delay > 0.19f && in.delay < 0.21f);
    assert(strcmp(in.cmd, "BoneVRepeat")==0);
    assert(in.argc == 7);
    assert(strcmp(in.arg[0],"128")==0 && strcmp(in.arg[6],"120")==0);
    /* 빈 인자: 'TLPause,,,,' → argc는 trailing 빈칸 무시 또는 보존? 보존 테스트 */
    Instr e; vm__parse_line("0,HeartTeleport,320,376,,,,,", &e);
    assert(strcmp(e.cmd,"HeartTeleport")==0);
    assert(strcmp(e.arg[0],"320")==0 && strcmp(e.arg[1],"376")==0);
    printf("test_parse OK\n");
}
int main(void){ test_parse(); printf("ALL OK\n"); return 0; }
```
- [ ] **Step 2: 컴파일→실패 확인** `cc vm.c test_vm.c -o /tmp/tvm -lm 2>&1` → link error(미정의) 또는 assert 실패.
- [ ] **Step 3: parseLine 구현** (vm.c): 수동 콤마 스캔, 토큰을 delay/cmd/arg로. argc=마지막 비어있지 않은 arg까지(또는 콤마 개수 기반 — trailing 빈 필드는 무시). `\r`/개행 트림.
- [ ] **Step 4: 컴파일→통과** `cc vm.c test_vm.c -o /tmp/tvm -lm && /tmp/tvm` → `test_parse OK`.
- [ ] **Step 5: 커밋** `feat(vm): CSV 줄 파서(빈인자 보존)`

## Task 2: 로드 + 라벨 사전스캔 (vm_load)
**Produces:** `vm_load` 가 code[]/n 채우고, `:라벨`을 labels[name]=1기반 줄번호로 등록, pc=1,t=0,running=1, vars에 "pi" 초기화.
- [ ] **Step 1: 실패 테스트** — 라벨 든 CSV 문자열 로드 후 `vm.n`, 라벨 줄번호, `vm_get_var("pi")≈3.14159` 검증. (내부 라벨조회용 `int vm__label_line(VM*,const char*)` 노출)
```c
const char* src = "0,SET,x,5\n0,:Loop\n0,ADD,x,$x,1\n0,JMPL,Loop,$x,10\n";
VM vm; VMHost h={0}; vm_load(&vm,&src[0],h);
assert(vm.n==4); assert(vm__label_line(&vm,"Loop")==2);
assert(vm_get_var(&vm,"pi")>3.14 && vm_get_var(&vm,"pi")<3.15);
```
- [ ] **Step 2: 컴파일 실패 확인**
- [ ] **Step 3: 구현** — 줄 분해(\n), 각 줄 vm__parse_line, code[i] 저장. cmd[0]==':'면 labels[cmd+1]=i+1. vars_set("pi", M_PI). running=1.
- [ ] **Step 4: 통과 확인**
- [ ] **Step 5: 커밋** `feat(vm): 로더+라벨 사전스캔`

## Task 3: 변수 + $치환 (vars_get/set, resolveArgs)
**Produces:** `vm_get_var/vm__set_var`, 그리고 스텝 시 각 arg가 `$name`이면 vars 값(숫자문자열)로 치환.
- [ ] **Step 1: 실패 테스트** — vars_set/get 왕복, `$x` 치환 동작(작은 헬퍼 `vm__resolve(VM*,const char* in,char* out)` 노출).
- [ ] **Step 2: 실패 확인**
- [ ] **Step 3: 구현** — vars 선형배열(이름→double). set은 기존 있으면 덮어쓰기. resolve: in[0]=='$'면 snprintf(out,"%.6g",vars_get(in+1)) else strcpy.
- [ ] **Step 4: 통과** **Step 5: 커밋** `feat(vm): 변수 딕셔너리 + $치환`

## Task 4: CPU 산술 명령 (도단위 trig 포함)
**Produces:** dispatch 내부에서 SET/ADD/SUB/MUL/DIV/MOD/FLOOR/SIN/COS/DEG/RAD/ANGLE/RND/GetHeartPos 처리.
- [ ] **Step 1: 실패 테스트** — 미니 CSV로 각 명령 후 변수값 검증. 특히 **SIN 90 → 1.0(도)**, ANGLE(0,0,1,0)→0도, RND<n, GetHeartPos(stub host가 좌표 반환).
```c
const char* s="0,SET,a,90\n0,SIN,b,$a\n0,ANGLE,c,0,0,10,0\n0,GetHeartPos,hx,hy\n";
/* host.get_heart_pos는 (320,376) 반환 stub */
VM vm; vm_load(&vm,s,host); for(int i=0;i<10;i++) vm_step(&vm,0.001f);
assert(fabs(vm_get_var(&vm,"b")-1.0)<1e-6);     /* sin(90도)=1 */
assert(fabs(vm_get_var(&vm,"c")-0.0)<1e-6);
assert(fabs(vm_get_var(&vm,"hx")-320)<1e-6 && fabs(vm_get_var(&vm,"hy")-376)<1e-6);
```
- [ ] **Step 2~4: 실패→구현(`*M_PI/180` 변환 주의)→통과** **Step 5: 커밋** `feat(vm): CPU 산술/도단위 trig/GetHeartPos`

## Task 5: 제어흐름 (JMP 8종 + 절대/상대) — 골든 케이스
**Produces:** dispatch 내 JMPABS/JMPREL/JMPZ/JMPNZ/JMPE/JMPNE/JMPL/JMPNL/JMPG/JMPNG. JMP시 pc 직접세팅 + 그 줄에선 pc++ 스킵.
- [ ] **Step 1: 실패 테스트** — 루프 CSV가 정확히 N회 도는지(`x`가 10이 되는지), JMPREL 점프테이블이 올바른 분기 도달.
```c
const char* loop="0,SET,x,0\n0,:L\n0,ADD,x,$x,1\n0,JMPL,L,$x,5\n";
/* x<5인 동안 L로 점프 → 최종 x==5 */
VM vm; vm_load(&vm,loop,host0); for(int i=0;i<50;i++) vm_step(&vm,0.001f);
assert(fabs(vm_get_var(&vm,"x")-5.0)<1e-6);
```
- [ ] **Step 2~4: 실패→구현(`-1` 보정·pc++스킵·C2 enum: JMPNL=≥, JMPNG=≤)→통과**
- [ ] **Step 6(추가): 실제 골든** `sans_multi1.csv`(JMPREL 점프테이블)·`sans_randomblaster1.csv`(JMPNL/JMPNG)를 fopen 로드해 vm_step 다수 실행, on_command 호출 cmd 시퀀스를 stub이 기록 → 무한루프(guard) 없이 EndAttack 도달 확인.
- [ ] **Step 5: 커밋** `feat(vm): 제어흐름 점프 8종 + 골든 CSV 검증`

## Task 6: 스텝 엔진 + 시간/딜레이 + 게임명령 디스패치
**Produces:** `vm_step` 완성(while 멀티라인, t누적, delay차감, guard1000). CPU/제어 외 명령은 host->on_command. TLPause→running=0(내부), vm_resume→running=1.
- [ ] **Step 1: 실패 테스트** — `sans_bonegap1.csv` 로드, stub host가 명령 기록. dt=1/60로 N프레임 진행시 (a)CombatZoneResize/HeartTeleport/HeartMode/TLPause가 0초에 즉시, (b)BoneVRepeat가 약 0.2초 뒤(12프레임±) 첫 호출, (c)guard 안 걸림.
- [ ] **Step 2~4: 실패→구현→통과**
- [ ] **Step 5: 커밋** `feat(vm): 스텝엔진 시간모델 + 게임명령 디스패치(TLPause/resume)`

## Task 7: hazards.c — 게임 명령 + 뼈 (Windows, 핑퐁)
**Files:** Create `game.h`,`hazards.c`. **Produces:** `void haz_on_command(void*,const char*,char[][VM_ARG_LEN],int); void haz_get_heart_pos(void*,double*,double*); void haz_update(float); void haz_render(HDC); void haz_reset();` + `extern Bone gBones[MAX]; extern Box gBox;`
- BoneVRepeat/BoneV/BoneHRepeat 인자맵은 **Battle.xml의 해당 함수 + CSV 대조로 확정**(구현 착수 시 `/tmp/bts_src/Battle.xml` grep). CombatZoneResize=즉시 gBox 스냅 + 마지막인자 TLResume이면 host가 vm_resume 호출. HeartTeleport=gSoul 이동. HeartMode=gSoul.mode 플래그(BLUE 물리는 Slice4). EndAttack=attack_active=0+메뉴 enable. 미지원 명령은 무시(디버그 로그).
- [ ] Step: 명령 핸들러 작성 → 뼈 구조체 운용 → update(이동/박스밖 제거)/render(흰 사각) → 충돌(기존 무적 재사용).
- [ ] Step: 커밋 `feat(haz): 게임명령+뼈 update/render`

## Task 8: main.c 통합 + vcxproj — (Windows, 핑퐁)
- [ ] vm.h/game.h include. `VMHost gHost={haz_on_command, haz_get_heart_pos, NULL};` 전역 `VM gVM;`
- [ ] `RunAttack(const char* name)`: assetPath("attacks/<name>.csv") fopen→텍스트 읽기→`vm_load(&gVM,text,gHost)`. 실패시 기존 패턴 폴백 플래그.
- [ ] PH_ENEMY 진입 시 `RunAttack("sans_bonegap1")`. PH_ENEMY update: `if(vm_using) { vm_step(&gVM,dt); haz_update(dt); if(!vm_is_running(&gVM)&&gVM.finished) →PH_MENU } else 기존 패턴`. render: `haz_render(gMemDC)` (박스 gBox 사용).
- [ ] 기존 하드코딩 spawnBone/updateEnemyPhase의 뼈 부분을 vm 경로와 분기.
- [ ] vcxproj에 vm.c/hazards.c ClCompile 추가.
- [ ] 커밋 `feat: VM을 전투에 통합(sans_bonegap 구동) + vcxproj`

## Task 9: 디버그 오버레이 (핑퐁 효율)
- [ ] F1 토글: 현재 CSV명 / `pc` / `t` / 주요 vars / 뼈 수 / running 을 좌상단에 작은 글씨로. 사용자 스샷 1장으로 VM 상태 진단.
- [ ] 커밋 `feat: VM 디버그 오버레이(F1)`

---

## Self-Review 체크
- 스펙 커버리지: VM 실행모델(§2)→Task1-6, 게임명령→Task7, 통합→Task8, 디버그오버레이(리스크3 대응)→Task9. BLUE물리/CombatZone트윈/GasterBlaster는 **Slice4-5**(이 plan 범위 밖, 명시).
- Placeholder: BoneVRepeat 인자맵만 "구현 착수 시 확정"으로 남김(Battle.xml 근거 필요, Task7에서 grep). 그 외 없음.
- 타입 일관성: `Instr/VM/VMHost/Box/Bone` 이름 plan 전반 일치. `char args[][VM_ARG_LEN]` 콜백 시그니처 통일.

## 테스트 실행 (Mac)
```bash
cc UndertaleSans/vm.c UndertaleSans/test_vm.c -o /tmp/tvm -lm && /tmp/tvm
```
Task 1-6은 위 명령으로 Mac에서 녹색 확인 후 진행. Task 7-9는 사용자 윈도우 빌드 핑퐁.
