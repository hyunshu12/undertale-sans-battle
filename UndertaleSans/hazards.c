/*
 * hazards.c — VM 게임 명령(뼈 등)을 받아 탄막을 생성/이동/충돌/렌더.
 * Slice 3 범위: CombatZoneResize / HeartTeleport / HeartMode / BoneV /
 *   BoneVRepeat / BoneHRepeat / EndAttack. 그 외 명령은 무시(Slice 4-5에서 구현).
 */
#include "game.h"
#include <string.h>
#include <stdlib.h>

#define HAZ_MAX_BONES 256

typedef struct {
    float x, y, w, h;   /* 좌상단, 크기(px) */
    int   dir;          /* 0=우,1=하,2=좌,3=상 */
    float speed;        /* px/s */
    int   color;        /* 0=흰,1=파랑(이동중만),2=주황(정지중만) */
    int   active;
} HBone;

static HBone   bones[HAZ_MAX_BONES];
static VM*     g_vm = NULL;
static float   g_resumeTimer = -1.0f;            /* >=0이면 카운트다운 후 vm_resume */
static HBRUSH  brWhite = NULL, brBlue = NULL, brOrange = NULL;

/* 방향 단위벡터 = cos/sin(dir*90도) 의 정수 등가 */
static const int DXi[4] = { 1, 0, -1, 0 };
static const int DYi[4] = { 0, 1, 0, -1 };

static void ensure_brushes(void) {
    if (!brWhite) {
        brWhite  = CreateSolidBrush(RGB(255, 255, 255));
        brBlue   = CreateSolidBrush(RGB(80, 160, 255));
        brOrange = CreateSolidBrush(RGB(255, 160, 0));
    }
}

void haz_set_vm(VM* vm) { g_vm = vm; }
void haz_reset(void) {
    int i; for (i = 0; i < HAZ_MAX_BONES; i++) bones[i].active = 0;
    g_resumeTimer = -1.0f;
}
int  haz_active_count(void) {
    int i, c = 0; for (i = 0; i < HAZ_MAX_BONES; i++) if (bones[i].active) c++; return c;
}
void haz_get_heart_pos(void* ctx, double* x, double* y) { (void)ctx; game_get_heart(x, y); }

static void spawn_bone(double x, double y, double height, int dir, double speed, int color) {
    int i; dir &= 3;
    for (i = 0; i < HAZ_MAX_BONES; i++) if (!bones[i].active) {
        bones[i].active = 1; bones[i].dir = dir; bones[i].speed = (float)speed; bones[i].color = color;
        bones[i].w = 8.0f; bones[i].h = (float)height; bones[i].x = (float)x; bones[i].y = (float)y;
        return;
    }
}

static double argf(char a[][VM_ARG_LEN], int argc, int i) { return i < argc ? atof(a[i]) : 0.0; }

/* VM 게임 명령 디스패치 (args는 이미 $치환됨) */
void haz_on_command(void* ctx, const char* cmd, char a[][VM_ARG_LEN], int argc) {
    (void)ctx;
    if (strcmp(cmd, "CombatZoneResize") == 0 || strcmp(cmd, "CombatZoneResizeInstant") == 0) {
        int l = (int)argf(a, argc, 0), t = (int)argf(a, argc, 1);
        int r = (int)argf(a, argc, 2), b = (int)argf(a, argc, 3);
        gBox.x = l; gBox.y = t; gBox.w = r - l; gBox.h = b - t;
        /* 마지막 인자 'TLResume' → 잠시 후 타임라인 재개 (Slice3: 스냅 + 0.35s 지연) */
        if (argc >= 5 && strcmp(a[4], "TLResume") == 0) g_resumeTimer = 0.35f;
        return;
    }
    if (strcmp(cmd, "HeartTeleport") == 0) { game_teleport_heart(argf(a, argc, 0), argf(a, argc, 1)); return; }
    if (strcmp(cmd, "HeartMode") == 0)     { game_set_heart_mode((int)argf(a, argc, 0)); return; }
    if (strcmp(cmd, "BoneV") == 0) {
        /* X,Y,Height,Direction,Speed,Color */
        spawn_bone(argf(a, argc, 0), argf(a, argc, 1), argf(a, argc, 2),
                   (int)argf(a, argc, 3), argf(a, argc, 4), (int)argf(a, argc, 5));
        return;
    }
    if (strcmp(cmd, "BoneVRepeat") == 0 || strcmp(cmd, "BoneHRepeat") == 0) {
        /* X,Y,Height,Direction,Speed,Count,Spacing → i마다 (X-DX*Spacing*i, Y-DY*Spacing*i) */
        double X = argf(a, argc, 0), Y = argf(a, argc, 1), H = argf(a, argc, 2);
        int    dir = (int)argf(a, argc, 3) & 3;
        double speed = argf(a, argc, 4), spacing = argf(a, argc, 6);
        int    count = (int)argf(a, argc, 5), i;
        for (i = 0; i < count; i++)
            spawn_bone(X - DXi[dir] * spacing * i, Y - DYi[dir] * spacing * i, H, dir, speed, 0);
        return;
    }
    if (strcmp(cmd, "EndAttack") == 0) { game_end_attack(); return; }
    /* 그 외 명령(GasterBlaster, Platform, SansSlam, BoneStab, SineBones, Sound,
       Sans 애니, CombatZoneSpeed, HeartMaxFallSpeed, BlackScreen 등)은
       Slice 4-5에서 구현. 지금은 무시한다. */
}

void haz_update(float dt) {
    int i;
    if (g_resumeTimer >= 0.0f) {
        g_resumeTimer -= dt;
        if (g_resumeTimer < 0.0f && g_vm) vm_resume(g_vm);
    }
    for (i = 0; i < HAZ_MAX_BONES; i++) {
        if (!bones[i].active) continue;
        bones[i].x += DXi[bones[i].dir] * bones[i].speed * dt;
        bones[i].y += DYi[bones[i].dir] * bones[i].speed * dt;
        /* 박스 + 여유 밖이면 제거 */
        if (bones[i].x + bones[i].w < gBox.x - 60 || bones[i].x > gBox.x + gBox.w + 60 ||
            bones[i].y + bones[i].h < gBox.y - 60 || bones[i].y > gBox.y + gBox.h + 60) {
            bones[i].active = 0; continue;
        }
        /* 충돌: 하트 중심 16x16 */
        {
            double hx, hy; double sx, sy;
            game_get_heart(&hx, &hy);
            sx = hx - 8; sy = hy - 8;
            if (sx < bones[i].x + bones[i].w && sx + 16 > bones[i].x &&
                sy < bones[i].y + bones[i].h && sy + 16 > bones[i].y) {
                int hit = 1;
                if (bones[i].color == 1)      hit = game_heart_moving();   /* 파랑: 이동중만 */
                else if (bones[i].color == 2) hit = !game_heart_moving();  /* 주황: 정지중만 */
                if (hit) game_hurt(1, 6);
            }
        }
    }
}

void haz_render(HDC dc) {
    int i; RECT r;
    ensure_brushes();
    for (i = 0; i < HAZ_MAX_BONES; i++) {
        if (!bones[i].active) continue;
        r.left = (int)bones[i].x; r.top = (int)bones[i].y;
        r.right = (int)(bones[i].x + bones[i].w); r.bottom = (int)(bones[i].y + bones[i].h);
        FillRect(dc, &r, bones[i].color == 1 ? brBlue : bones[i].color == 2 ? brOrange : brWhite);
    }
}
