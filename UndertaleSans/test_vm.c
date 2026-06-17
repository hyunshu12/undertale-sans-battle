/* test_vm.c — vm.c 순수 C 단위테스트 (Mac clang).  게임 빌드에는 미포함.
 * 실행: cc UndertaleSans/vm.c UndertaleSans/test_vm.c -o /tmp/tvm -lm && /tmp/tvm
 */
#include "vm.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static char   g_cmdlog[512][32];
static int    g_ncmd = 0;
static double g_hx = 320, g_hy = 376;

static void stub_cmd(void* ctx, const char* cmd, char args[][VM_ARG_LEN], int argc) {
    (void)ctx; (void)args; (void)argc;
    if (g_ncmd < 512) { strncpy(g_cmdlog[g_ncmd], cmd, 31); g_cmdlog[g_ncmd][31] = 0; g_ncmd++; }
}
static void stub_heart(void* ctx, double* x, double* y) { (void)ctx; *x = g_hx; *y = g_hy; }
static VMHost host(void) { VMHost h; h.on_command = stub_cmd; h.get_heart_pos = stub_heart; h.ctx = NULL; return h; }

static void run_until_done(VM* vm, int maxframes) {
    int f;
    for (f = 0; f < maxframes; f++) {
        vm_step(vm, 1.0f / 60.0f);
        if (vm->finished) break;
        if (!vm_is_running(vm) && !vm->finished) vm_resume(vm); /* TLPause 해제 시뮬(실게임=박스 리사이즈 콜백) */
    }
}

static void test_parse(void) {
    Instr in;
    assert(vm__parse_line("0.2,BoneVRepeat,128,257,95,0,180,8,120", &in));
    assert(in.delay > 0.19f && in.delay < 0.21f);
    assert(strcmp(in.cmd, "BoneVRepeat") == 0);
    assert(in.argc == 7);
    assert(strcmp(in.arg[0], "128") == 0 && strcmp(in.arg[6], "120") == 0);
    vm__parse_line("0,HeartTeleport,320,376,,,,,", &in);
    assert(strcmp(in.cmd, "HeartTeleport") == 0 && in.argc == 2);
    assert(strcmp(in.arg[0], "320") == 0 && strcmp(in.arg[1], "376") == 0);
    printf("  test_parse OK\n");
}
static void test_load_labels(void) {
    const char* src = "0,SET,x,5\n0,:Loop\n0,ADD,x,$x,1\n0,JMPL,Loop,$x,10\n";
    VM vm; vm_load(&vm, src, host());
    assert(vm.n == 4);
    assert(vm__label_line(&vm, "Loop") == 2);
    assert(vm_get_var(&vm, "pi") > 3.14 && vm_get_var(&vm, "pi") < 3.15);
    printf("  test_load_labels OK\n");
}
static void test_cpu(void) {
    const char* s = "0,SET,a,90\n0,SIN,b,$a\n0,ANGLE,c,0,0,10,0\n0,GetHeartPos,hx,hy\n0,COS,d,0\n";
    VM vm; vm_load(&vm, s, host());
    run_until_done(&vm, 100);
    assert(fabs(vm_get_var(&vm, "b") - 1.0) < 1e-6);  /* sin(90도)=1 */
    assert(fabs(vm_get_var(&vm, "c") - 0.0) < 1e-6);  /* 우향 각=0 */
    assert(fabs(vm_get_var(&vm, "d") - 1.0) < 1e-6);  /* cos(0)=1 */
    assert(fabs(vm_get_var(&vm, "hx") - 320) < 1e-6 && fabs(vm_get_var(&vm, "hy") - 376) < 1e-6);
    printf("  test_cpu OK\n");
}
static void test_control(void) {
    const char* loop = "0,SET,x,0\n0,:L\n0,ADD,x,$x,1\n0,JMPL,L,$x,5\n";
    VM vm; vm_load(&vm, loop, host());
    run_until_done(&vm, 200);
    assert(fabs(vm_get_var(&vm, "x") - 5.0) < 1e-6);
    assert(vm.finished);
    printf("  test_control OK (loop=5)\n");
}
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc(n + 1); if (!b) { fclose(f); return NULL; }
    fread(b, 1, n, f); b[n] = 0; fclose(f); return b;
}
static void test_golden(const char* path) {
    char* csv = read_file(path);
    if (!csv) { printf("  (golden skip: %s)\n", path); return; }
    g_ncmd = 0;
    VM vm; vm_load(&vm, csv, host());
    assert(vm.n > 0);
    run_until_done(&vm, 60 * 90);  /* 최대 90초 */
    printf("  golden %-44s lines=%-4d cmds=%-4d finished=%d first3=%s,%s,%s\n",
           path, vm.n, g_ncmd, vm.finished,
           g_ncmd > 0 ? g_cmdlog[0] : "-", g_ncmd > 1 ? g_cmdlog[1] : "-", g_ncmd > 2 ? g_cmdlog[2] : "-");
    assert(g_ncmd > 0);  /* 명령이 하나도 안 나오면 VM이 안 돈 것 */
    free(csv);
}
int main(void) {
    printf("VM tests:\n");
    test_parse();
    test_load_labels();
    test_cpu();
    test_control();
    test_golden("UndertaleSans/assets/attacks/sans_bonegap1.csv");
    test_golden("UndertaleSans/assets/attacks/sans_multi1.csv");
    test_golden("UndertaleSans/assets/attacks/sans_randomblaster1.csv");
    test_golden("UndertaleSans/assets/attacks/sans_intro.csv");
    test_golden("UndertaleSans/assets/attacks/sans_final.csv");
    printf("ALL OK\n");
    return 0;
}
