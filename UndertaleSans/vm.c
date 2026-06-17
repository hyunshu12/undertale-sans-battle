/*
 * vm.c — BTS 타임라인 바이트코드 인터프리터 (순수 C).
 * 실행 모델은 BTS Timeline.xml 과 일치:
 *  - CSV 줄 = "delay,cmd,args". delay는 직전 줄 실행 후 상대 지연(초).
 *  - 라벨(:name)은 로드 시 1패스 사전스캔(1기반 줄번호).
 *  - 매 프레임 t+=dt; t가 도래한 줄을 연쇄 실행(delay=0 줄은 같은 틱에 즉시).
 *  - JMP는 pc를 target-1로 사전보정하고 매 줄 pc++ 가 항상 실행돼 target에 안착.
 *  - guard>1000 이면 무한루프로 간주해 정지.
 */
#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------- 변수 딕셔너리 ---------------- */
static int vm__var_index(VM* vm, const char* name) {
    int i;
    for (i = 0; i < vm->nvars; i++)
        if (strcmp(vm->vars[i].name, name) == 0) return i;
    return -1;
}
static void vm__set(VM* vm, const char* name, double v) {
    int i = vm__var_index(vm, name);
    if (i < 0) {
        if (vm->nvars >= VM_MAX_VARS) return;
        i = vm->nvars++;
        strncpy(vm->vars[i].name, name, 23);
        vm->vars[i].name[23] = 0;
    }
    vm->vars[i].val = v;
}
static double vm__get(VM* vm, const char* name) {
    int i = vm__var_index(vm, name);
    return i < 0 ? 0.0 : vm->vars[i].val;
}
double vm_get_var(VM* vm, const char* name) { return vm__get(vm, name); }

int vm__label_line(VM* vm, const char* name) {
    int i;
    for (i = 0; i < vm->nlabels; i++)
        if (strcmp(vm->labels[i].name, name) == 0) return vm->labels[i].line;
    return -1;
}

/* ---------------- 줄 파서 ---------------- */
/* 수동 콤마 스캔(빈 인자 보존, trailing 빈칸 제거). 성공 1 / 빈줄 0. */
int vm__parse_line(const char* line, Instr* out) {
    char fields[2 + VM_MAX_ARGS][VM_ARG_LEN];
    int nf = 0, fi = 0, i = 0;
    char c;
    memset(out, 0, sizeof(*out));
    /* 줄을 콤마로 분해(개행/CR에서 종료) */
    for (;;) {
        c = line[i++];
        if (c == '\n' || c == '\r' || c == '\0' || c == ',') {
            if (nf < 2 + VM_MAX_ARGS) { fields[nf][fi] = 0; nf++; }
            fi = 0;
            if (c == '\n' || c == '\r' || c == '\0') break;
        } else if (nf < 2 + VM_MAX_ARGS && fi < VM_ARG_LEN - 1) {
            fields[nf][fi++] = c;
        }
    }
    if (nf == 0) return 0;
    if (nf == 1 && fields[0][0] == 0) return 0;  /* 완전 빈줄 */
    out->delay = (float)atof(fields[0]);
    if (nf >= 2) { strncpy(out->cmd, fields[1], 23); out->cmd[23] = 0; }
    out->argc = 0;
    for (i = 2; i < nf; i++) {
        strncpy(out->arg[out->argc], fields[i], VM_ARG_LEN - 1);
        out->arg[out->argc][VM_ARG_LEN - 1] = 0;
        out->argc++;
    }
    while (out->argc > 0 && out->arg[out->argc - 1][0] == 0) out->argc--;  /* trailing 빈칸 */
    return 1;
}

/* ---------------- 로드 ---------------- */
void vm_load(VM* vm, const char* csv, VMHost host) {
    const char* p = csv;
    vm->n = 0; vm->nvars = 0; vm->nlabels = 0;
    vm->pc = 1; vm->t = 0; vm->running = 1; vm->finished = 0;
    vm->host = host;
    vm__set(vm, "pi", M_PI);
    while (*p && vm->n < VM_MAX_LINES) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        {
            char line[600];
            int L = (int)(e - p); if (L > 599) L = 599;
            memcpy(line, p, L); line[L] = 0;
            Instr ins;
            if (!vm__parse_line(line, &ins)) memset(&ins, 0, sizeof(ins)); /* 빈줄=no-op(줄번호 보존) */
            vm->code[vm->n] = ins;
            if (ins.cmd[0] == ':' && vm->nlabels < VM_MAX_LABELS) {
                strncpy(vm->labels[vm->nlabels].name, ins.cmd + 1, 23);
                vm->labels[vm->nlabels].name[23] = 0;
                vm->labels[vm->nlabels].line = vm->n + 1;  /* 1기반 */
                vm->nlabels++;
            }
            vm->n++;
        }
        if (*e == '\n') p = e + 1; else break;
    }
}

/* ---------------- $치환 ---------------- */
static void vm__resolve(VM* vm, const char* in, char* out) {
    if (in[0] == '$') {
        snprintf(out, VM_ARG_LEN, "%.10g", vm__get(vm, in + 1));
    } else {
        strncpy(out, in, VM_ARG_LEN - 1); out[VM_ARG_LEN - 1] = 0;
    }
}

/* ---------------- 점프 ---------------- */
static int vm__all_digits(const char* s) {
    if (!s[0]) return 0;
    for (; *s; s++) if (*s < '0' || *s > '9') return 0;
    return 1;
}
/* target-1로 사전보정(이후 매 줄 pc++ 가 target에 안착) */
static void vm__jmpabs(VM* vm, const char* target) {
    if (vm__all_digits(target)) { vm->pc = atoi(target) - 1; return; }
    {
        int ln = vm__label_line(vm, target);
        if (ln > 0) vm->pc = ln - 1;
        else vm->running = 0;  /* 라벨 없음 → panic */
    }
}

/* ---------------- 디스패치 ---------------- */
#define NUM(s) (atof(s))
static int eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

/* CPU/제어면 1 반환(host로 보내지 않음), 게임명령이면 0 */
static int vm__dispatch(VM* vm, const char* cmd, char r[][VM_ARG_LEN], int argc) {
    (void)argc;
    /* 산술 */
    if (eq(cmd, "SET"))   { vm__set(vm, r[0], NUM(r[1])); return 1; }
    if (eq(cmd, "ADD"))   { vm__set(vm, r[0], NUM(r[1]) + NUM(r[2])); return 1; }
    if (eq(cmd, "SUB"))   { vm__set(vm, r[0], NUM(r[1]) - NUM(r[2])); return 1; }
    if (eq(cmd, "MUL"))   { vm__set(vm, r[0], NUM(r[1]) * NUM(r[2])); return 1; }
    if (eq(cmd, "DIV"))   { double d = NUM(r[2]); vm__set(vm, r[0], d != 0 ? NUM(r[1]) / d : 0); return 1; }
    if (eq(cmd, "MOD"))   { double d = NUM(r[2]); vm__set(vm, r[0], d != 0 ? fmod(NUM(r[1]), d) : 0); return 1; }
    if (eq(cmd, "FLOOR")) { vm__set(vm, r[0], floor(NUM(r[1]))); return 1; }
    if (eq(cmd, "SIN"))   { vm__set(vm, r[0], sin(NUM(r[1]) * M_PI / 180.0)); return 1; }  /* 도 입력 */
    if (eq(cmd, "COS"))   { vm__set(vm, r[0], cos(NUM(r[1]) * M_PI / 180.0)); return 1; }
    if (eq(cmd, "DEG"))   { vm__set(vm, r[0], NUM(r[1]) * 180.0 / M_PI); return 1; }
    if (eq(cmd, "RAD"))   { vm__set(vm, r[0], NUM(r[1]) * M_PI / 180.0); return 1; }
    if (eq(cmd, "ANGLE")) { /* dst x1 y1 x2 y2 → (x1,y1)->(x2,y2) 각(도) */
        double a = atan2(NUM(r[4]) - NUM(r[2]), NUM(r[3]) - NUM(r[1])) * 180.0 / M_PI;
        if (a < 0) a += 360.0;
        vm__set(vm, r[0], a); return 1;
    }
    if (eq(cmd, "RND"))   { int n = (int)NUM(r[1]); vm__set(vm, r[0], n > 0 ? (double)(rand() % n) : 0); return 1; }
    if (eq(cmd, "GetHeartPos")) {
        double x = 0, y = 0;
        if (vm->host.get_heart_pos) vm->host.get_heart_pos(vm->host.ctx, &x, &y);
        vm__set(vm, r[0], x); vm__set(vm, r[1], y); return 1;
    }
    /* 제어흐름 (조건 참이면 JMPABS(target)). r[0]=target, r[1]=a, r[2]=b */
    if (eq(cmd, "JMPABS")) { vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPREL")) { vm->pc += atoi(r[0]) - 1; return 1; }
    if (eq(cmd, "JMPZ"))   { if (NUM(r[1]) == 0)             vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPNZ"))  { if (NUM(r[1]) != 0)             vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPE"))   { if (NUM(r[1]) == NUM(r[2]))     vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPNE"))  { if (NUM(r[1]) != NUM(r[2]))     vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPL"))   { if (NUM(r[1]) <  NUM(r[2]))     vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPNL"))  { if (NUM(r[1]) >= NUM(r[2]))     vm__jmpabs(vm, r[0]); return 1; }  /* C2: NotLess=>= */
    if (eq(cmd, "JMPG"))   { if (NUM(r[1]) >  NUM(r[2]))     vm__jmpabs(vm, r[0]); return 1; }
    if (eq(cmd, "JMPNG"))  { if (NUM(r[1]) <= NUM(r[2]))     vm__jmpabs(vm, r[0]); return 1; }  /* C2: NotGreater=<= */
    /* 타임라인 제어 */
    if (eq(cmd, "TLPause")) { vm->running = 0; return 1; }
    /* 그 외 = 게임 명령 → host */
    return 0;
}

/* ---------------- 스텝 ---------------- */
void vm_step(VM* vm, float dt) {
    int guard = 0;
    if (vm->running) vm->t += dt;
    while (vm->running && vm->pc >= 1 && vm->pc <= vm->n && guard++ < 1000) {
        Instr* ins = &vm->code[vm->pc - 1];
        char r[VM_MAX_ARGS][VM_ARG_LEN];
        int i;
        if (vm->t < ins->delay) break;        /* 아직 이 줄 시간 안 됨 */
        for (i = 0; i < ins->argc; i++) vm__resolve(vm, ins->arg[i], r[i]);
        if (ins->cmd[0] != 0 && ins->cmd[0] != ':') {
            if (!vm__dispatch(vm, ins->cmd, r, ins->argc)) {
                /* 게임 명령 */
                if (vm->host.on_command) vm->host.on_command(vm->host.ctx, ins->cmd, r, ins->argc);
            }
        }
        vm->t -= ins->delay;
        vm->pc++;
    }
    if (vm->running && vm->pc > vm->n) { vm->finished = 1; vm->running = 0; }
    if (guard >= 1000) vm->running = 0;       /* 무한루프 방지 */
}

void vm_resume(VM* vm) { if (!vm->finished) vm->running = 1; }
int  vm_is_running(VM* vm) { return vm->running; }
