/*
 * vm.h — BTS 공격 CSV(타임라인 바이트코드) 인터프리터 공개 API.
 * 순수 C (windows.h 무의존) → Mac/Windows 양쪽 컴파일·단위테스트 가능.
 * 게임 명령(BoneV 등)·하트 좌표는 VMHost 콜백으로 분리.
 */
#ifndef VM_H
#define VM_H

#define VM_MAX_LINES  512
#define VM_MAX_VARS   96
#define VM_MAX_LABELS 64
#define VM_MAX_ARGS   9
#define VM_ARG_LEN    40

/* CSV 한 줄: "delay,cmd,arg0,...,argN" 을 파싱한 결과 */
typedef struct {
    float delay;                       /* 컬럼0: 직전 줄 실행 후 이 줄까지의 상대 지연(초) */
    char  cmd[24];                     /* 컬럼1: 명령명(또는 ":라벨") */
    char  arg[VM_MAX_ARGS][VM_ARG_LEN];/* 컬럼2..: 인자(빈 인자 보존, trailing 빈칸 제거) */
    int   argc;
} Instr;

/* 게임/엔진 측 콜백 */
typedef struct VMHost {
    /* CPU/제어 외 모든 명령. args는 이미 $치환된 문자열. */
    void (*on_command)(void* ctx, const char* cmd, char args[][VM_ARG_LEN], int argc);
    /* GetHeartPos용 현재 하트 좌표 */
    void (*get_heart_pos)(void* ctx, double* x, double* y);
    void* ctx;
} VMHost;

typedef struct {
    Instr code[VM_MAX_LINES]; int n;
    struct { char name[24]; double val; } vars[VM_MAX_VARS]; int nvars;
    struct { char name[24]; int line; } labels[VM_MAX_LABELS]; int nlabels;
    int   pc;        /* 1기반 program counter */
    float t;         /* 누적 시간 카운터 */
    int   running;   /* 1=실행중, 0=정지(TLPause/완료) */
    int   finished;  /* 타임라인 자연 종료(pc>n) */
    VMHost host;
} VM;

/* CSV 텍스트를 로드(파싱+라벨 사전스캔+pc=1,t=0,running=1, vars["pi"]=π) */
void   vm_load(VM* vm, const char* csvText, VMHost host);
/* 한 프레임 진행(dt초). 시간이 도래한 줄들을 연쇄 실행. */
void   vm_step(VM* vm, float dt);
/* host가 CombatZoneResize(TLResume) 등 완료 콜백에서 호출 → running=1 */
void   vm_resume(VM* vm);
double vm_get_var(VM* vm, const char* name);
int    vm_is_running(VM* vm);

/* --- 테스트 노출용 내부 함수 --- */
int    vm__parse_line(const char* line, Instr* out);
int    vm__label_line(VM* vm, const char* name);

#endif
