/*
 * hazards.c — VM 게임 명령을 받아 탄막(뼈/사인뼈/블래스터/찌르기/플랫폼)을
 * 생성/이동/충돌/렌더. CombatZone 트윈, Sound/BlackScreen/SansText 라우팅.
 * 좌표 BTS 640x480 1:1, 각도=도(0우/90하/180좌/270상). dt=1/60.
 */
#include "game.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define HAZ_MAX_BONES    512
#define HAZ_MAX_PLATS    32
#define HAZ_MAX_BLASTERS 16
#define HAZ_MAX_STABS    16

static double DVX(double deg) { return cos(deg * M_PI / 180.0); }
static double DVY(double deg) { return sin(deg * M_PI / 180.0); }

typedef struct { float x, y, w, h; float ang; float speed; int color; int active; } HBone;
typedef struct { float x, y, w; float ang; float speed; int reverse; int active; } HPlat;
typedef struct {
    int state; float x, y, ex, ey, ang, endAng; int size;
    float waitTimer, blastTime, baseSize, beamTimer, leaveSpeed, opacity; int active;
} HBlaster;  /* state: 0 ENTER,1 WAIT,2 FIRE,3 LEAVE */
typedef struct {
    int state; float x, y, w, h, ang; float dist, warn, stay, speed, timer, pen; int active;
} HStab;     /* state: 0 WARN,1 IN,2 HOLD,3 OUT.  pen=누적 진입거리 */

static HBone    bones[HAZ_MAX_BONES];
static HPlat    plats[HAZ_MAX_PLATS];
static HBlaster blasters[HAZ_MAX_BLASTERS];
static HStab    stabs[HAZ_MAX_STABS];
static VM*      g_vm = NULL;

/* CombatZone 4변 트윈 */
static float czL, czT, czR, czB;       /* 목표 */
static int   czActive = 0, czResume = 0;
static float czSpeed = 480.0f;

static HBRUSH brWhite = NULL, brBlue = NULL, brOrange = NULL, brCyan = NULL;

static void ensure_brushes(void) {
    if (!brWhite) {
        brWhite  = CreateSolidBrush(RGB(255, 255, 255));
        brBlue   = CreateSolidBrush(RGB(80, 160, 255));
        brOrange = CreateSolidBrush(RGB(255, 160, 0));
        brCyan   = CreateSolidBrush(RGB(0, 220, 255));
    }
}

/* 종료 시 브러시 해제(GDI 객체 누수 방지). 게임 종료 1회 호출. */
void haz_free(void) {
    if (brWhite)  { DeleteObject(brWhite);  brWhite  = NULL; }
    if (brBlue)   { DeleteObject(brBlue);   brBlue   = NULL; }
    if (brOrange) { DeleteObject(brOrange); brOrange = NULL; }
    if (brCyan)   { DeleteObject(brCyan);   brCyan   = NULL; }
}
void haz_set_vm(VM* vm) { g_vm = vm; }
void haz_reset(void) {
    int i;
    for (i = 0; i < HAZ_MAX_BONES; i++)    bones[i].active = 0;
    for (i = 0; i < HAZ_MAX_PLATS; i++)    plats[i].active = 0;
    for (i = 0; i < HAZ_MAX_BLASTERS; i++) blasters[i].active = 0;
    for (i = 0; i < HAZ_MAX_STABS; i++)    stabs[i].active = 0;
    czActive = 0; czResume = 0; czSpeed = 480.0f;
}
int haz_active_count(void) {
    int i, c = 0;
    for (i = 0; i < HAZ_MAX_BONES; i++)    if (bones[i].active) c++;
    for (i = 0; i < HAZ_MAX_BLASTERS; i++) if (blasters[i].active) c++;
    for (i = 0; i < HAZ_MAX_STABS; i++)    if (stabs[i].active) c++;
    return c;
}
void haz_get_heart_pos(void* ctx, double* x, double* y) { (void)ctx; game_get_heart(x, y); }

static double argf(char a[][VM_ARG_LEN], int argc, int i) { return i < argc ? atof(a[i]) : 0.0; }

/* ---- spawn ---- */
static void spawn_bone(double x, double y, double w, double h, double dir, double speed, int color) {
    int i;
    for (i = 0; i < HAZ_MAX_BONES; i++) if (!bones[i].active) {
        bones[i].active = 1; bones[i].ang = (float)(((int)dir & 3) * 90);
        bones[i].speed = (float)speed; bones[i].color = color;
        bones[i].w = (float)w; bones[i].h = (float)h; bones[i].x = (float)x; bones[i].y = (float)y;
        return;
    }
}
static void spawn_plat(double x, double y, double w, double dir, double speed, int reverse) {
    int i;
    for (i = 0; i < HAZ_MAX_PLATS; i++) if (!plats[i].active) {
        plats[i].active = 1; plats[i].x = (float)x; plats[i].y = (float)y; plats[i].w = (float)w;
        plats[i].ang = (float)(((int)dir & 3) * 90); plats[i].speed = (float)speed; plats[i].reverse = reverse;
        return;
    }
}
static void spawn_blaster(double size, double sx, double sy, double ex, double ey, double endAng, double wait, double blast) {
    int i;
    for (i = 0; i < HAZ_MAX_BLASTERS; i++) if (!blasters[i].active) {
        HBlaster* b = &blasters[i];
        b->active = 1; b->state = 0; b->size = (int)size;
        b->x = (float)sx; b->y = (float)sy; b->ex = (float)ex; b->ey = (float)ey;
        b->ang = 90.0f; b->endAng = (float)endAng;
        b->waitTimer = (float)wait; b->blastTime = (float)blast;
        b->baseSize = 0; b->beamTimer = 0; b->leaveSpeed = 0; b->opacity = 100;
        return;
    }
}

/* ---- 명령 디스패치 ---- */
void haz_on_command(void* ctx, const char* cmd, char a[][VM_ARG_LEN], int argc) {
    (void)ctx;
    if (strcmp(cmd, "CombatZoneResizeInstant") == 0) {
        gBox.x = (int)argf(a, argc, 0); gBox.y = (int)argf(a, argc, 1);
        gBox.w = (int)argf(a, argc, 2) - gBox.x; gBox.h = (int)argf(a, argc, 3) - gBox.y;
        czActive = 0;
        return;
    }
    if (strcmp(cmd, "CombatZoneResize") == 0) {
        czL = (float)argf(a, argc, 0); czT = (float)argf(a, argc, 1);
        czR = (float)argf(a, argc, 2); czB = (float)argf(a, argc, 3);
        czActive = 1;
        czResume = (argc >= 5 && strcmp(a[4], "TLResume") == 0);
        return;
    }
    if (strcmp(cmd, "CombatZoneSpeed") == 0) { czSpeed = (float)argf(a, argc, 0); return; }
    if (strcmp(cmd, "HeartTeleport") == 0)  { game_teleport_heart(argf(a, argc, 0), argf(a, argc, 1)); return; }
    if (strcmp(cmd, "HeartMode") == 0)      { game_set_heart_mode((int)argf(a, argc, 0)); return; }
    if (strcmp(cmd, "HeartMaxFallSpeed") == 0) { game_set_max_fall(argf(a, argc, 0)); return; }
    if (strcmp(cmd, "BoneV") == 0) {
        spawn_bone(argf(a, argc, 0), argf(a, argc, 1), 8, argf(a, argc, 2),
                   argf(a, argc, 3), argf(a, argc, 4), (int)argf(a, argc, 5));
        return;
    }
    if (strcmp(cmd, "BoneVRepeat") == 0) {
        double X = argf(a, argc, 0), Y = argf(a, argc, 1), H = argf(a, argc, 2);
        int dir = (int)argf(a, argc, 3) & 3; double speed = argf(a, argc, 4);
        int count = (int)argf(a, argc, 5); double spacing = argf(a, argc, 6); int i;
        for (i = 0; i < count; i++)
            spawn_bone(X - DVX(dir * 90) * spacing * i, Y - DVY(dir * 90) * spacing * i, 8, H, dir, speed, 0);
        return;
    }
    if (strcmp(cmd, "BoneHRepeat") == 0) {
        /* 가로 뼈: Width 가 길이, 두께 8 */
        double X = argf(a, argc, 0), Y = argf(a, argc, 1), W = argf(a, argc, 2);
        int dir = (int)argf(a, argc, 3) & 3; double speed = argf(a, argc, 4);
        int count = (int)argf(a, argc, 5); double spacing = argf(a, argc, 6); int i;
        for (i = 0; i < count; i++)
            spawn_bone(X - DVX(dir * 90) * spacing * i, Y - DVY(dir * 90) * spacing * i, W, 8, dir, speed, 0);
        return;
    }
    if (strcmp(cmd, "SineBones") == 0) {
        /* Count,Spacing(부호=진입측),Speed,Height */
        int count = (int)argf(a, argc, 0); double spacing = argf(a, argc, 1);
        double speed = argf(a, argc, 2), H = argf(a, argc, 3); int i;
        for (i = 0; i < count; i++) {
            double sine = floor(sin((i / 3.0)) * 28.0);  /* i/3 라디안 */
            double X; int dir;
            double topH = H + sine;
            double botY, botH;
            if (spacing > 0) { X = (gBox.x + gBox.w) + spacing * i; dir = 2; }
            else             { X = gBox.x + spacing * i;            dir = 0; }
            spawn_bone(X, gBox.y + 6, 8, topH, dir, speed, 0);          /* 상단 */
            botY = gBox.y + 6 + topH + 39;
            botH = (gBox.y + gBox.h - 5) - botY;
            if (botH > 4) spawn_bone(X, botY, 8, botH, dir, speed, 0);  /* 하단 */
        }
        return;
    }
    if (strcmp(cmd, "GasterBlaster") == 0) {
        spawn_blaster(argf(a, argc, 0), argf(a, argc, 1), argf(a, argc, 2),
                      argf(a, argc, 3), argf(a, argc, 4), argf(a, argc, 5),
                      argf(a, argc, 6), argf(a, argc, 7));
        return;
    }
    if (strcmp(cmd, "BoneStab") == 0) {
        int dir = (int)argf(a, argc, 0) & 3; double dist = argf(a, argc, 1);
        double warn = argf(a, argc, 2), stay = argf(a, argc, 3); int i;
        for (i = 0; i < HAZ_MAX_STABS; i++) if (!stabs[i].active) {
            HStab* s = &stabs[i];
            s->active = 1; s->state = 0; s->ang = (float)(dir * 90);
            s->dist = (float)dist; s->warn = (float)warn; s->stay = (float)stay;
            s->speed = (float)(dist * 10); s->timer = (float)warn;
            /* 가장자리 위치(찌르기 방향 반대편에서 진입) */
            if (dir == 0) { s->w = (float)dist; s->h = (float)gBox.h; s->x = (float)(gBox.x + gBox.w); s->y = (float)gBox.y; }
            else if (dir == 2) { s->w = (float)dist; s->h = (float)gBox.h; s->x = (float)(gBox.x - dist); s->y = (float)gBox.y; }
            else if (dir == 1) { s->w = (float)gBox.w; s->h = (float)dist; s->x = (float)gBox.x; s->y = (float)(gBox.y + gBox.h); }
            else { s->w = (float)gBox.w; s->h = (float)dist; s->x = (float)gBox.x; s->y = (float)(gBox.y - dist); }
            break;
        }
        return;
    }
    if (strcmp(cmd, "Platform") == 0) {
        spawn_plat(argf(a, argc, 0), argf(a, argc, 1), argf(a, argc, 2),
                   argf(a, argc, 3), argf(a, argc, 4), (int)argf(a, argc, 5));
        return;
    }
    if (strcmp(cmd, "PlatformRepeat") == 0) {
        double X = argf(a, argc, 0), Y = argf(a, argc, 1), W = argf(a, argc, 2);
        int dir = (int)argf(a, argc, 3) & 3; double speed = argf(a, argc, 4);
        int count = (int)argf(a, argc, 5); double spacing = argf(a, argc, 6); int i;
        for (i = 0; i < count; i++)
            spawn_plat(X - DVX(dir * 90) * spacing * i, Y - DVY(dir * 90) * spacing * i, W, dir, speed, 0);
        return;
    }
    if (strcmp(cmd, "SansSlam") == 0) { game_set_heart_mode(1); return; } /* BLUE 슬램 물리는 다음 푸시 */
    if (strcmp(cmd, "Sound") == 0)      { game_play_sound(a[0]); return; }
    if (strcmp(cmd, "BlackScreen") == 0){ game_set_blackscreen((int)argf(a, argc, 0)); return; }
    if (strcmp(cmd, "SansText") == 0)   { game_sans_text(a[0]); return; }
    if (strcmp(cmd, "EndAttack") == 0)  { game_end_attack(); return; }
    /* SansBody/SansHead/SansTorso/SansAnimation/SansSweat/SansX/SansRepeat/SansSlamDamage
       등 Sans 비주얼/잔여는 무시(스프라이트 토글은 추후). */
}

/* ---- 충돌 헬퍼 ---- */
static int aabb_hit(double bx, double by, double bw, double bh) {
    double hx, hy; game_get_heart(&hx, &hy);
    return (hx - 8 < bx + bw && hx + 8 > bx && hy - 8 < by + bh && hy + 8 > by);
}

/* ---- update ---- */
void haz_update(float dt) {
    int i;

    /* CombatZone 4변 트윈 */
    if (czActive) {
        float curL = (float)gBox.x, curT = (float)gBox.y;
        float curR = (float)(gBox.x + gBox.w), curB = (float)(gBox.y + gBox.h);
        float mv = czSpeed * dt; int done = 1;
        if (fabs(czL - curL) > 0.5f) { curL += (czL > curL ? 1 : -1) * (fabs(czL - curL) < mv ? fabs(czL - curL) : mv); done = 0; }
        if (fabs(czT - curT) > 0.5f) { curT += (czT > curT ? 1 : -1) * (fabs(czT - curT) < mv ? fabs(czT - curT) : mv); done = 0; }
        if (fabs(czR - curR) > 0.5f) { curR += (czR > curR ? 1 : -1) * (fabs(czR - curR) < mv ? fabs(czR - curR) : mv); done = 0; }
        if (fabs(czB - curB) > 0.5f) { curB += (czB > curB ? 1 : -1) * (fabs(czB - curB) < mv ? fabs(czB - curB) : mv); done = 0; }
        gBox.x = (int)curL; gBox.y = (int)curT; gBox.w = (int)(curR - curL); gBox.h = (int)(curB - curT);
        if (done) { czActive = 0; if (czResume && g_vm) { vm_resume(g_vm); czResume = 0; } }
    }

    /* 뼈 */
    for (i = 0; i < HAZ_MAX_BONES; i++) {
        if (!bones[i].active) continue;
        bones[i].x += (float)(DVX(bones[i].ang) * bones[i].speed * dt);
        bones[i].y += (float)(DVY(bones[i].ang) * bones[i].speed * dt);
        if (bones[i].x + bones[i].w < gBox.x - 80 || bones[i].x > gBox.x + gBox.w + 80 ||
            bones[i].y + bones[i].h < gBox.y - 80 || bones[i].y > gBox.y + gBox.h + 80) { bones[i].active = 0; continue; }
        if (aabb_hit(bones[i].x, bones[i].y, bones[i].w, bones[i].h)) {
            int hit = 1;
            if (bones[i].color == 1) hit = game_heart_moving();
            else if (bones[i].color == 2) hit = !game_heart_moving();
            if (hit) game_hurt(1, 6);
        }
    }

    /* 플랫폼(이동/왕복; 착지물리는 다음 푸시) */
    for (i = 0; i < HAZ_MAX_PLATS; i++) {
        if (!plats[i].active) continue;
        plats[i].x += (float)(DVX(plats[i].ang) * plats[i].speed * dt);
        plats[i].y += (float)(DVY(plats[i].ang) * plats[i].speed * dt);
        if (plats[i].reverse) {
            if (plats[i].x < gBox.x || plats[i].x + plats[i].w > gBox.x + gBox.w ||
                plats[i].y < gBox.y || plats[i].y > gBox.y + gBox.h)
                plats[i].ang = (float)fmod(plats[i].ang + 180, 360);
        } else if (plats[i].x + plats[i].w < gBox.x - 80 || plats[i].x > gBox.x + gBox.w + 80 ||
                   plats[i].y < gBox.y - 80 || plats[i].y > gBox.y + gBox.h + 80) { plats[i].active = 0; }
    }

    /* 블래스터 */
    for (i = 0; i < HAZ_MAX_BLASTERS; i++) {
        HBlaster* b = &blasters[i];
        if (!b->active) continue;
        if (b->state == 0) {            /* ENTER: 위치/각도 지수보간 */
            b->x += (float)((b->ex - b->x) * dt * 10);
            b->y += (float)((b->ey - b->y) * dt * 10);
            b->ang += (float)((b->endAng - b->ang) * dt * 10);
            if (fabs(b->ex - b->x) < 3 && fabs(b->ey - b->y) < 3 && fabs(b->endAng - b->ang) < 3) {
                b->x = b->ex; b->y = b->ey; b->ang = b->endAng; b->state = 1;
            }
        } else if (b->state == 1) {     /* WAIT */
            b->waitTimer -= dt;
            if (b->waitTimer <= 0) { b->state = 2; b->waitTimer = 0.1f; game_play_sound("GasterBlaster"); }
        } else if (b->state == 2) {     /* FIRE 짧은 후 빔 */
            b->waitTimer -= dt;
            if (b->waitTimer <= 0) b->state = 3;
            /* 빔 진행 */
        }
        if (b->state >= 2) {            /* 빔 활성(FIRE/LEAVE 동안) */
            float scale = (b->size == 2 ? 3.0f : 2.0f);
            float maxH = 35.0f * scale;
            b->beamTimer += dt;
            if (b->beamTimer < 4.0f / 30.0f) b->baseSize = maxH * (b->beamTimer / (4.0f / 30.0f));
            else b->baseSize = maxH;
            if (b->beamTimer > b->blastTime + 4.0f / 30.0f)
                b->baseSize *= (float)pow(0.8, dt * 30);
            /* 충돌: OBB (소울을 블래스터 기준 -ang 역회전) */
            if (b->baseSize > 2) {
                double hx, hy, lx, ly, c, s;
                game_get_heart(&hx, &hy);
                c = DVX(-b->ang); s = DVY(-b->ang);
                lx = (hx - b->x) * c - (hy - b->y) * s;
                ly = (hx - b->x) * s + (hy - b->y) * c;
                if (lx >= 0 && lx <= 1000 && fabs(ly) <= b->baseSize * 0.375)
                    game_hurt(1, 10);
            } else if (b->state == 3) { b->active = 0; continue; }
        }
        if (b->state == 3) {            /* LEAVE: 후퇴 */
            b->leaveSpeed += 30 * dt * 30 * dt;  /* +=30/틱 누적 근사 */
            b->leaveSpeed += 0.5f;
            b->x -= (float)(DVX(b->ang) * dt * b->leaveSpeed);
            b->y -= (float)(DVY(b->ang) * dt * b->leaveSpeed);
            if (b->x < -200 || b->x > 840 || b->y < -200 || b->y > 680) b->active = 0;
        }
    }

    /* 찌르기(BoneStab): WARN(예고) → IN(dist만큼 진입) → HOLD(stay) → OUT(후퇴) */
    for (i = 0; i < HAZ_MAX_STABS; i++) {
        HStab* s = &stabs[i];
        int dir;
        if (!s->active) continue;
        dir = ((int)(s->ang / 90)) & 3;
        if (s->state == 0) {            /* WARN */
            s->timer -= dt;
            if (s->timer <= 0) { s->state = 1; s->pen = 0.0f; }
        } else if (s->state == 1) {     /* IN: dist 까지만 진입(오버슈트 방지) */
            float mv = s->speed * dt;
            if (s->pen + mv > s->dist) mv = s->dist - s->pen;
            s->x -= (float)(DVX(dir * 90) * mv); s->y -= (float)(DVY(dir * 90) * mv);
            s->pen += mv;
            if (s->pen >= s->dist - 0.01f) { s->state = 2; s->timer = s->stay; }
        } else if (s->state == 2) {     /* HOLD: 정지 유지 */
            s->timer -= dt;
            if (s->timer <= 0) s->state = 3;
        } else {                        /* OUT: 다시 후퇴 → 화면 밖이면 제거 */
            float mv = s->speed * dt;
            s->x += (float)(DVX(dir * 90) * mv); s->y += (float)(DVY(dir * 90) * mv);
            if (s->x + s->w < -50 || s->x > 700 || s->y + s->h < -50 || s->y > 540) s->active = 0;
        }
        if (s->active && s->state >= 1 && aabb_hit(s->x, s->y, s->w, s->h)) game_hurt(1, 6);
    }
}

/* ---- 회전 빔 렌더(Polygon) ---- */
static void draw_beam(HDC dc, double bx, double by, double ang, double len, double thick, HBRUSH br) {
    POINT p[4];
    double c = DVX(ang), s = DVY(ang);
    double nx = -s, ny = c;               /* 수직 단위 */
    double hx0 = bx, hy0 = by;
    double hx1 = bx + c * len, hy1 = by + s * len;
    HBRUSH ob; HPEN op;
    p[0].x = (LONG)(hx0 + nx * thick / 2); p[0].y = (LONG)(hy0 + ny * thick / 2);
    p[1].x = (LONG)(hx1 + nx * thick / 2); p[1].y = (LONG)(hy1 + ny * thick / 2);
    p[2].x = (LONG)(hx1 - nx * thick / 2); p[2].y = (LONG)(hy1 - ny * thick / 2);
    p[3].x = (LONG)(hx0 - nx * thick / 2); p[3].y = (LONG)(hy0 - ny * thick / 2);
    ob = (HBRUSH)SelectObject(dc, br);
    op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, p, 4);
    SelectObject(dc, op); SelectObject(dc, ob);
}

void haz_render(HDC dc) {
    int i; RECT r;
    ensure_brushes();
    /* 뼈 */
    for (i = 0; i < HAZ_MAX_BONES; i++) {
        if (!bones[i].active) continue;
        r.left = (int)bones[i].x; r.top = (int)bones[i].y;
        r.right = (int)(bones[i].x + bones[i].w); r.bottom = (int)(bones[i].y + bones[i].h);
        FillRect(dc, &r, bones[i].color == 1 ? brBlue : bones[i].color == 2 ? brOrange : brWhite);
    }
    /* 플랫폼 */
    for (i = 0; i < HAZ_MAX_PLATS; i++) {
        if (!plats[i].active) continue;
        r.left = (int)plats[i].x; r.top = (int)plats[i].y;
        r.right = (int)(plats[i].x + plats[i].w); r.bottom = (int)(plats[i].y + 6);
        FillRect(dc, &r, brWhite);
    }
    /* 찌르기(예고=청록 반투명 느낌으로 cyan, 본체=흰) */
    for (i = 0; i < HAZ_MAX_STABS; i++) {
        if (!stabs[i].active) continue;
        r.left = (int)stabs[i].x; r.top = (int)stabs[i].y;
        r.right = (int)(stabs[i].x + stabs[i].w); r.bottom = (int)(stabs[i].y + stabs[i].h);
        FillRect(dc, &r, stabs[i].state == 0 ? brCyan : brWhite);
    }
    /* 블래스터 빔 + 머리 */
    for (i = 0; i < HAZ_MAX_BLASTERS; i++) {
        HBlaster* b = &blasters[i];
        if (!b->active) continue;
        if (b->state >= 2 && b->baseSize > 1) {
            draw_beam(dc, b->x, b->y, b->ang, 1000, b->baseSize, brWhite);
            draw_beam(dc, b->x, b->y, b->ang, 1000, b->baseSize * 0.5, brCyan);
        }
        /* 머리(간단한 흰 사각, 회전 생략) */
        { int hs = (b->size == 2 ? 40 : 28);
          r.left = (int)b->x - hs / 2; r.top = (int)b->y - hs / 2;
          r.right = (int)b->x + hs / 2; r.bottom = (int)b->y + hs / 2;
          FillRect(dc, &r, brWhite); }
    }
}

/* 플랫폼 위 솔리드 판정(BLUE 물리 착지용). 위에서 내려올 때만 밟힘. */
int haz_is_solid(double x, double y, double w, double h, double* outTopY) {
    int i;
    for (i = 0; i < HAZ_MAX_PLATS; i++) {
        if (!plats[i].active) continue;
        if (x < plats[i].x + plats[i].w && x + w > plats[i].x &&
            y + h >= plats[i].y && y + h <= plats[i].y + 12) {
            if (outTopY) *outTopY = plats[i].y;
            return 1;
        }
    }
    return 0;
}

/* 발밑(footX..footX+w, footY) 에 닿은 플랫폼의 수평속도(px/s). 없으면 0.
   이동 플랫폼이 그 위에 선 영혼을 함께 끌고 가도록(착지 물리 정밀화). */
double haz_platform_vx(double footX, double footY, double w) {
    int i;
    for (i = 0; i < HAZ_MAX_PLATS; i++) {
        if (!plats[i].active) continue;
        if (footX < plats[i].x + plats[i].w && footX + w > plats[i].x &&
            footY >= plats[i].y - 10 && footY <= plats[i].y + 9)
            return DVX(plats[i].ang) * plats[i].speed;
    }
    return 0.0;
}
