/*
 * UNDERTALE - Sans Battle  (Win32 GDI 버전)  /  Slice 2
 * ------------------------------------------------------------------
 * - Windows 시스템 라이브러리만 사용(gdi32/user32/winmm/msimg32). 외부 라이브러리 없음.
 * - 우리 소유의 Win32 창 + 더블버퍼 GDI 렌더(깜빡임 없는 60fps).
 * - 실제 Bad Time Simulator 스프라이트(BMP) + Megalovania(WAV) 사용.
 *   에셋이 없거나 못 읽어도 GDI 도형으로 폴백 → 항상 실행됨.
 *
 * [조작]
 *   전투(회피): 화살표/WASD 이동
 *   메뉴: 좌우(또는 A/D)로 선택, Z/Enter 확정
 *   대사 진행: Z/Enter      종료: ESC
 *
 * [Slice 2] 타이틀 -> 전투(턴: 대사 -> 적턴 회피 -> 메뉴) -> 게임오버/자비 엔딩
 *   적턴 패턴: 짝수턴=뼈 탄막, 홀수턴=게이스터 블래스터(빔)
 *   4턴 생존 후 MERCY 선택 시 자비 엔딩
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>   /* PlaySound, SND_*, timeBeginPeriod/timeEndPeriod */
#include <stdlib.h>   /* rand, srand, RAND_MAX */
#include <string.h>   /* strlen, strrchr */
#include <stdio.h>    /* fopen (에셋 경로 확인) */
#include <time.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msimg32.lib")   /* TransparentBlt */

/* 진입점/서브시스템을 코드에서 못박음(프로젝트 설정 의존 X). 콘솔 창 안 뜸. */
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

/* ---------------- 상수 ---------------- */
#define CLIENT_W   640
#define CLIENT_H   480

#define BOX_X      170
#define BOX_Y      158
#define BOX_W      300
#define BOX_H      168

#define SOUL_SIZE  16
#define MAX_BONES  48
#define MAX_BLASTERS 6
#define HIT_DAMAGE 6
#define MAX_HP     92
#define MERCY_TURNS 4          /* 이 횟수 생존 후 MERCY로 승리 */
#define ENEMY_DURATION 8.0f    /* 적턴 길이(초) */

#define SANS_W 128
#define SANS_H 120
#define SANS_X ((CLIENT_W - SANS_W) / 2)
#define SANS_Y 6

/* 메뉴 버튼 배치 */
#define BTN_W 110
#define BTN_H 42
#define BTN_Y 402
#define BTN_STEP 124
#define BTN_X0 79

/* ---------------- 구조체 ([구현조건: 구조체]) ---------------- */
typedef enum { ST_TITLE, ST_BATTLE, ST_GAMEOVER, ST_WIN } GameState;
typedef enum { PH_DIALOGUE, PH_ENEMY, PH_MENU, PH_ACTION } Phase;

typedef struct { float x, y; int maxHp, hp; float invuln; } Soul;
typedef struct { float x, y, vx; int w, h; int active; } Bone;
typedef struct {
    int   active, state;   /* state: 0 charge, 1 fire, 2 fade */
    float bx, by, timer;   /* 블래스터 스프라이트 위치, 단계 타이머 */
    int   beamY, beamH;    /* 가로 빔(박스 전체 폭) */
} Blaster;

/* 스프라이트(BMP). ok=0이면 폴백 렌더 */
typedef struct { HBITMAP bmp; HDC dc; HBITMAP oldbmp; int w, h, ok; } Sprite;

/* ---------------- 전역 상태 ---------------- */
static HWND     gHwnd;
static HDC      gMemDC;
static HBITMAP  gMemBmp, gOldBmp;
static HBRUSH   gBlack, gWhite, gRed, gYellow, gBlue, gDkRed, gCyan;
static HFONT    gFontBig, gFontSmall, gFontTiny;
static int      gRunning = 1;

static Sprite   gSprHead, gSprHeadBlue, gSprHeart, gSprBlaster, gSprBlasterFire;
static Sprite   gSprFight, gSprAct, gSprItem, gSprMercy;

static GameState gState = ST_TITLE;
static Phase     gPhase = PH_DIALOGUE;
static Soul      gSoul;
static Bone      gBones[MAX_BONES];        /* [구현조건: 배열] */
static Blaster   gBlasters[MAX_BLASTERS];  /* [구현조건: 배열] */
static int       gTurn = 0;
static int       gMenuIndex = 0;
static int       gItemsLeft = 3;
static float     gSpawnTimer = 0.0f;
static float     gBlasterTimer = 0.0f;
static float     gEnemyTime = 0.0f;
static float     gTypePos = 0.0f;          /* 타이핑 진행 글자수 */
static char      gMessage[160] = "";       /* 액션 메시지 */
static float     gMenacePulse = 0.0f;      /* 샌즈 파란 눈 토글 */

/* 키 엣지 검출용 이전 상태 */
static int gPrevZ = 0, gPrevLeft = 0, gPrevRight = 0;

/* 대사 ([구현조건: 배열]) */
static const char* gDialogues[] = {
    "* it's a beautiful day outside.",
    "* birds are singing, flowers are blooming...",
    "* on days like these, kids like you...",
    "* should be burning in hell.",
    "* heh. you're still standing?",
    "* ...maybe you should just give me a break."
};
#define DIALOGUE_COUNT 6

/* ---------------- 전방 선언 ---------------- */
static void startBattle(void);
static void startTurn(void);
static void startEnemyPhase(void);
static void advanceTurn(void);

/* ---------------- 유틸 ([구현조건: 사용자정의함수]) ---------------- */
static int   keyDown(int vk) {                                                             /* [구현조건: 키보드입력] */
    if (GetForegroundWindow() != gHwnd) return 0;   /* 창 비활성(Alt+Tab) 시 전역 입력 무시 */
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
static float frand(float a, float b) { return a + (b - a) * ((float)rand() / (float)RAND_MAX); } /* [구현조건: 랜덤함수] */

static int rectsOverlap(float ax, float ay, float aw, float ah,
                        float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
static void fillRect(HDC dc, int x, int y, int w, int h, HBRUSH br) {
    RECT r; r.left = x; r.top = y; r.right = x + w; r.bottom = y + h; FillRect(dc, &r, br);
}
static void drawText(HDC dc, int x, int y, const char* s, COLORREF col, HFONT f) {
    HFONT old = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, col);
    TextOutA(dc, x, y, s, (int)strlen(s));
    SelectObject(dc, old);
}
/* 사각형 영역 안에서 자동 줄바꿈(긴 대사/메시지가 박스 밖으로 새지 않게) */
static void drawTextWrapped(int x, int y, int w, int h, const char* s, COLORREF col, HFONT f) {
    RECT r; HFONT old;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    old = (HFONT)SelectObject(gMemDC, f);
    SetBkMode(gMemDC, TRANSPARENT); SetTextColor(gMemDC, col);
    DrawTextA(gMemDC, s, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(gMemDC, old);
}

/* 에셋 경로: exe폴더\assets, exe폴더\..\..\assets(VS x64\Debug 기준), cwd\assets 순으로 탐색 */
static const char* assetPath(const char* name) {
    static char buf[MAX_PATH];
    char dir[MAX_PATH];
    char* slash;
    FILE* f;
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    slash = strrchr(dir, '\\'); if (slash) *slash = '\0';

    wsprintfA(buf, "%s\\assets\\%s", dir, name);
    f = fopen(buf, "rb"); if (f) { fclose(f); return buf; }

    wsprintfA(buf, "%s\\..\\..\\assets\\%s", dir, name);
    f = fopen(buf, "rb"); if (f) { fclose(f); return buf; }

    wsprintfA(buf, "assets\\%s", name);
    return buf;
}

static Sprite loadSprite(const char* file) {
    Sprite s; HDC wdc; HBITMAP b; BITMAP bm;
    s.bmp = NULL; s.dc = NULL; s.oldbmp = NULL; s.w = s.h = 0; s.ok = 0;
    b = (HBITMAP)LoadImageA(NULL, assetPath(file), IMAGE_BITMAP, 0, 0,
                            LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!b) return s;
    GetObject(b, sizeof(bm), &bm);
    s.bmp = b; s.w = bm.bmWidth; s.h = bm.bmHeight;
    wdc = GetDC(gHwnd);
    s.dc = CreateCompatibleDC(wdc);
    s.oldbmp = (HBITMAP)SelectObject(s.dc, b);
    ReleaseDC(gHwnd, wdc);
    s.ok = 1;
    return s;
}
/* 검은색을 투명으로 처리해 블릿(스프라이트 배경이 검정으로 합성돼 있음) */
static void drawSprite(Sprite* s, int x, int y) {
    if (!s->ok) return;
    TransparentBlt(gMemDC, x, y, s->w, s->h, s->dc, 0, 0, s->w, s->h, RGB(0, 0, 0));
}
static void freeSprite(Sprite* s) {
    if (!s->ok) return;
    SelectObject(s->dc, s->oldbmp);
    DeleteDC(s->dc);
    DeleteObject(s->bmp);
    s->ok = 0;
}

/* ---------------- 오디오 ([구현조건: 음악재생]) ---------------- */
static void playBGM(void) {
    const char* p = assetPath("megalovania.wav");
    FILE* f = fopen(p, "rb");
    if (!f) return;                 /* 파일 없으면 재생 시도 안 함(시스템 경고음 방지) */
    fclose(f);
    PlaySoundA(p, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
}
static void stopBGM(void) { PlaySoundA(NULL, NULL, 0); }

/* ---------------- 게임 로직 ---------------- */
static void clearHazards(void) {
    int i;
    for (i = 0; i < MAX_BONES; i++) gBones[i].active = 0;
    for (i = 0; i < MAX_BLASTERS; i++) gBlasters[i].active = 0;
}
static void centerSoul(void) {
    gSoul.x = BOX_X + BOX_W / 2.0f - SOUL_SIZE / 2.0f;
    gSoul.y = BOX_Y + BOX_H / 2.0f - SOUL_SIZE / 2.0f;
}
static void startBattle(void) {
    gSoul.maxHp = MAX_HP; gSoul.hp = MAX_HP; gSoul.invuln = 0.0f;
    gTurn = 0; gMenuIndex = 0; gItemsLeft = 3;
    clearHazards(); centerSoul();
    gState = ST_BATTLE;
    playBGM();
    startTurn();
}
static void startTurn(void) {
    gPhase = PH_DIALOGUE;
    gTypePos = 0.0f;
}
static void startEnemyPhase(void) {
    gPhase = PH_ENEMY;
    clearHazards(); centerSoul();
    gSpawnTimer = 0.0f; gBlasterTimer = 0.6f;
    gEnemyTime = ENEMY_DURATION;
}
static void advanceTurn(void) {
    gTurn++;
    startTurn();
}

/* 뼈 한 개 생성: 오른쪽에서 무작위 높이로 등장 */
static void spawnBone(void) {
    int i;
    for (i = 0; i < MAX_BONES; i++) {
        if (!gBones[i].active) {
            gBones[i].active = 1;
            gBones[i].w = 10;
            gBones[i].h = (int)frand(40.0f, 95.0f);
            gBones[i].x = (float)(BOX_X + BOX_W + 4);
            gBones[i].y = frand((float)(BOX_Y + 2), (float)(BOX_Y + BOX_H - 2 - gBones[i].h));
            gBones[i].vx = -frand(150.0f, 235.0f) - gTurn * 8.0f;  /* 턴 진행시 약간 빨라짐 */
            return;
        }
    }
}
/* 블래스터 한 개 생성: 왼쪽 가장자리 무작위 행 -> 가로 빔 */
static void spawnBlaster(void) {
    int i;
    for (i = 0; i < MAX_BLASTERS; i++) {
        if (!gBlasters[i].active) {
            int ry = (int)frand((float)(BOX_Y + 24), (float)(BOX_Y + BOX_H - 24));
            gBlasters[i].active = 1;
            gBlasters[i].state = 0;            /* charge */
            gBlasters[i].timer = 0.7f;
            gBlasters[i].beamH = 28;
            gBlasters[i].beamY = ry - 14;
            gBlasters[i].bx = (float)(BOX_X - 70);
            gBlasters[i].by = (float)(ry - 44);
            return;
        }
    }
}

static void hurtSoul(void) {
    if (gSoul.invuln > 0.0f) return;
    gSoul.hp -= HIT_DAMAGE;
    gSoul.invuln = 1.0f;
    if (gSoul.hp <= 0) { gSoul.hp = 0; gState = ST_GAMEOVER; stopBGM(); }
}

static void updateEnemyPhase(float dt) {
    float dx = 0.0f, dy = 0.0f;
    const float speed = 160.0f;
    int i;

    /* 영혼 이동 [구현조건: 키보드입력] */
    if (keyDown(VK_LEFT)  || keyDown('A')) dx -= 1.0f;
    if (keyDown(VK_RIGHT) || keyDown('D')) dx += 1.0f;
    if (keyDown(VK_UP)    || keyDown('W')) dy -= 1.0f;
    if (keyDown(VK_DOWN)  || keyDown('S')) dy += 1.0f;
    gSoul.x += dx * speed * dt;
    gSoul.y += dy * speed * dt;
    if (gSoul.x < BOX_X + 2)                    gSoul.x = (float)(BOX_X + 2);
    if (gSoul.y < BOX_Y + 2)                    gSoul.y = (float)(BOX_Y + 2);
    if (gSoul.x > BOX_X + BOX_W - 2 - SOUL_SIZE) gSoul.x = (float)(BOX_X + BOX_W - 2 - SOUL_SIZE);
    if (gSoul.y > BOX_Y + BOX_H - 2 - SOUL_SIZE) gSoul.y = (float)(BOX_Y + BOX_H - 2 - SOUL_SIZE);

    if (gSoul.invuln > 0.0f) gSoul.invuln -= dt;

    if (gTurn % 2 == 0) {
        /* 짝수 턴: 뼈 탄막 */
        gSpawnTimer -= dt;
        if (gSpawnTimer <= 0.0f) { spawnBone(); gSpawnTimer = frand(0.32f, 0.62f); }
        for (i = 0; i < MAX_BONES; i++) {
            if (!gBones[i].active) continue;
            gBones[i].x += gBones[i].vx * dt;
            if (gBones[i].x + gBones[i].w < BOX_X - 4) { gBones[i].active = 0; continue; }
            if (rectsOverlap(gSoul.x, gSoul.y, SOUL_SIZE, SOUL_SIZE,
                             gBones[i].x, gBones[i].y, (float)gBones[i].w, (float)gBones[i].h))
                hurtSoul();
        }
    } else {
        /* 홀수 턴: 게이스터 블래스터 */
        gBlasterTimer -= dt;
        if (gBlasterTimer <= 0.0f) { spawnBlaster(); gBlasterTimer = frand(1.0f, 1.5f); }
        for (i = 0; i < MAX_BLASTERS; i++) {
            if (!gBlasters[i].active) continue;
            gBlasters[i].timer -= dt;
            if (gBlasters[i].state == 0) {            /* 충전 */
                if (gBlasters[i].timer <= 0.0f) { gBlasters[i].state = 1; gBlasters[i].timer = 0.55f; }
            } else if (gBlasters[i].state == 1) {     /* 발사: 가로 빔 */
                if (rectsOverlap(gSoul.x, gSoul.y, SOUL_SIZE, SOUL_SIZE,
                                 (float)BOX_X, (float)gBlasters[i].beamY, (float)BOX_W, (float)gBlasters[i].beamH))
                    hurtSoul();
                if (gBlasters[i].timer <= 0.0f) { gBlasters[i].state = 2; gBlasters[i].timer = 0.2f; }
            } else {                                  /* 소멸 */
                if (gBlasters[i].timer <= 0.0f) gBlasters[i].active = 0;
            }
        }
    }

    gEnemyTime -= dt;
    if (gEnemyTime <= 0.0f && gState == ST_BATTLE) {
        clearHazards();
        gPhase = PH_MENU;
        gMenuIndex = 0;
    }
}

/* 메뉴 액션 수행 */
static void doAction(int idx) {
    switch (idx) {
    case 0: /* FIGHT */
        lstrcpyA(gMessage, "* You attack! ...but sans dodges. MISS!");
        break;
    case 1: /* ACT */
        lstrcpyA(gMessage, "* Check.  SANS - ATK 1 DEF 1.\n* the easiest enemy. can only deal 1 damage.");
        break;
    case 2: /* ITEM */
        if (gItemsLeft > 0) {
            gItemsLeft--;
            gSoul.hp += 20; if (gSoul.hp > gSoul.maxHp) gSoul.hp = gSoul.maxHp;
            wsprintfA(gMessage, "* You eat a Monster Candy. (+20 HP)\n* (%d left)", gItemsLeft);
        } else {
            lstrcpyA(gMessage, "* You're out of items.");
        }
        break;
    default: /* MERCY */
        if (gTurn + 1 >= MERCY_TURNS) { gState = ST_WIN; stopBGM(); return; }
        lstrcpyA(gMessage, "* Sans isn't ready to give up yet.");
        break;
    }
    gPhase = PH_ACTION;
    gTypePos = 0.0f;
}

static void updateMenuPhase(int leftPressed, int rightPressed, int zPressed) {
    if (leftPressed  && gMenuIndex > 0) gMenuIndex--;
    if (rightPressed && gMenuIndex < 3) gMenuIndex++;
    if (zPressed) doAction(gMenuIndex);
}

static void update(float dt) {
    int z = keyDown('Z') || keyDown(VK_RETURN);
    int l = keyDown(VK_LEFT) || keyDown('A');
    int r = keyDown(VK_RIGHT) || keyDown('D');
    int zPressed = z && !gPrevZ;
    int lPressed = l && !gPrevLeft;
    int rPressed = r && !gPrevRight;
    gPrevZ = z; gPrevLeft = l; gPrevRight = r;

    gMenacePulse += dt;

    switch (gState) {
    case ST_TITLE:
        if (zPressed) startBattle();
        break;
    case ST_BATTLE:
        if (gPhase == PH_DIALOGUE) {
            const char* line = gDialogues[gTurn < DIALOGUE_COUNT ? gTurn : DIALOGUE_COUNT - 1];
            int len = (int)strlen(line);
            gTypePos += dt * 28.0f;                 /* 타이핑 속도 */
            if (zPressed) {
                if (gTypePos < len) gTypePos = (float)len;  /* 1회 누르면 즉시 완성 */
                else startEnemyPhase();                     /* 완성 후 누르면 적턴 */
            }
        } else if (gPhase == PH_ENEMY) {
            updateEnemyPhase(dt);
        } else if (gPhase == PH_MENU) {
            updateMenuPhase(lPressed, rPressed, zPressed);
        } else { /* PH_ACTION */
            int len = (int)strlen(gMessage);
            gTypePos += dt * 32.0f;
            if (zPressed) {
                if (gTypePos < len) gTypePos = (float)len;
                else advanceTurn();
            }
        }
        break;
    case ST_GAMEOVER:
    case ST_WIN:
        if (zPressed) { gState = ST_TITLE; }
        break;
    }
}

/* ---------------- 렌더 ---------------- */
static void drawSansHead(void) {
    /* 일정 주기로 파란 눈 연출 */
    int menace = ((int)(gMenacePulse * 1.5f) % 4 == 0);
    Sprite* s = (menace && gSprHeadBlue.ok) ? &gSprHeadBlue : &gSprHead;
    if (s->ok) { drawSprite(s, SANS_X, SANS_Y); return; }
    /* 폴백: GDI 해골 */
    fillRect(gMemDC, SANS_X + 24, SANS_Y + 10, 80, 78, gWhite);
    fillRect(gMemDC, SANS_X + 40, SANS_Y + 32, 14, 16, gBlack);
    fillRect(gMemDC, SANS_X + 74, SANS_Y + 32, 14, 16, menace ? gCyan : gBlack);
    fillRect(gMemDC, SANS_X + 40, SANS_Y + 60, 48, 6, gBlack);
}
static void drawMenuButton(Sprite* s, const char* label, int idx) {
    int x = BTN_X0 + idx * BTN_STEP;
    if (s->ok) drawSprite(s, x, BTN_Y);
    else { fillRect(gMemDC, x, BTN_Y, BTN_W, BTN_H, gDkRed); drawText(gMemDC, x + 14, BTN_Y + 10, label, RGB(255, 160, 0), gFontSmall); }
    if (gPhase == PH_MENU && gMenuIndex == idx) {
        /* 선택 하이라이트 + 하트 커서 */
        HBRUSH ob = (HBRUSH)SelectObject(gMemDC, GetStockObject(NULL_BRUSH));
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
        HPEN op = (HPEN)SelectObject(gMemDC, pen);
        Rectangle(gMemDC, x - 4, BTN_Y - 4, x + BTN_W + 4, BTN_Y + BTN_H + 4);
        SelectObject(gMemDC, op); SelectObject(gMemDC, ob); DeleteObject(pen);
        if (gSprHeart.ok) drawSprite(&gSprHeart, x - 26, BTN_Y + 13);
        else fillRect(gMemDC, x - 26, BTN_Y + 13, SOUL_SIZE, SOUL_SIZE, gRed);
    }
}
static void drawHpBar(void) {
    int hpx = 250, hpy = 350, hpw = 120, hph = 20;
    int cur = (int)(hpw * (gSoul.hp / (float)gSoul.maxHp));
    char buf[48];
    drawText(gMemDC, 120, hpy - 1, "CHARA   LV 19", RGB(255, 255, 255), gFontSmall);
    fillRect(gMemDC, hpx, hpy, hpw, hph, gDkRed);
    fillRect(gMemDC, hpx, hpy, cur, hph, gYellow);
    wsprintfA(buf, "HP %d / %d", gSoul.hp, gSoul.maxHp);
    drawText(gMemDC, hpx + hpw + 12, hpy - 1, buf, RGB(255, 255, 255), gFontSmall);
}
static void drawSoul(void) {
    int blink = (gSoul.invuln > 0.0f && ((int)(gSoul.invuln * 16.0f) % 2));
    if (blink) return;
    if (gSprHeart.ok) drawSprite(&gSprHeart, (int)gSoul.x, (int)gSoul.y);
    else fillRect(gMemDC, (int)gSoul.x, (int)gSoul.y, SOUL_SIZE, SOUL_SIZE, gRed);
}

static void render(void) {
    int i;
    fillRect(gMemDC, 0, 0, CLIENT_W, CLIENT_H, gBlack);

    if (gState == ST_TITLE) {
        drawText(gMemDC, CLIENT_W / 2 - 145, 120, "UNDERTALE", RGB(255, 255, 255), gFontBig);
        drawText(gMemDC, CLIENT_W / 2 - 95, 230, "* Sans Battle (Slice 2)", RGB(255, 255, 255), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 110, 285, "Press Z or Enter to start", RGB(255, 255, 0), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 130, 320, "Move: Arrows/WASD   Menu: <- ->  Confirm: Z", RGB(150, 150, 150), gFontTiny);
        drawText(gMemDC, CLIENT_W / 2 - 30, 345, "Quit: ESC", RGB(150, 150, 150), gFontTiny);
        return;
    }
    if (gState == ST_GAMEOVER) {
        drawText(gMemDC, CLIENT_W / 2 - 115, 175, "GAME OVER", RGB(255, 0, 0), gFontBig);
        drawText(gMemDC, CLIENT_W / 2 - 90, 280, "* Stay determined...", RGB(255, 255, 255), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 120, 320, "Press Z to return to title", RGB(255, 255, 0), gFontSmall);
        return;
    }
    if (gState == ST_WIN) {
        drawSansHead();
        drawText(gMemDC, CLIENT_W / 2 - 130, 200, "* welp. i'm going to grillby's.", RGB(255, 255, 255), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 130, 240, "YOU WON.  (Mercy)", RGB(255, 255, 0), gFontBig);
        drawText(gMemDC, CLIENT_W / 2 - 120, 310, "Press Z to return to title", RGB(255, 255, 0), gFontSmall);
        return;
    }

    /* ---- 전투 ---- */
    drawSansHead();

    /* 전투 박스 */
    fillRect(gMemDC, BOX_X - 3, BOX_Y - 3, BOX_W + 6, BOX_H + 6, gWhite);
    fillRect(gMemDC, BOX_X, BOX_Y, BOX_W, BOX_H, gBlack);

    if (gPhase == PH_DIALOGUE) {
        const char* line = gDialogues[gTurn < DIALOGUE_COUNT ? gTurn : DIALOGUE_COUNT - 1];
        int n = (int)gTypePos; int len = (int)strlen(line);
        char buf[160];
        if (n > len) n = len;
        memcpy(buf, line, n); buf[n] = '\0';
        drawTextWrapped(BOX_X + 12, BOX_Y + 14, BOX_W - 24, BOX_H - 30, buf, RGB(255, 255, 255), gFontSmall);
        if (n >= len) drawText(gMemDC, BOX_X + BOX_W - 70, BOX_Y + BOX_H - 24, "[Z]", RGB(255, 255, 0), gFontTiny);
    } else if (gPhase == PH_ENEMY) {
        if (gTurn % 2 == 0) {
            for (i = 0; i < MAX_BONES; i++)
                if (gBones[i].active)
                    fillRect(gMemDC, (int)gBones[i].x, (int)gBones[i].y, gBones[i].w, gBones[i].h, gWhite);
        } else {
            for (i = 0; i < MAX_BLASTERS; i++) {
                if (!gBlasters[i].active) continue;
                if (gBlasters[i].state == 0) {
                    /* 충전: 빔 예고선 */
                    fillRect(gMemDC, BOX_X, gBlasters[i].beamY + gBlasters[i].beamH / 2 - 1, BOX_W, 2, gDkRed);
                    drawSprite(&gSprBlaster, (int)gBlasters[i].bx, (int)gBlasters[i].by);
                } else if (gBlasters[i].state == 1) {
                    /* 발사: 빔 */
                    fillRect(gMemDC, BOX_X, gBlasters[i].beamY, BOX_W, gBlasters[i].beamH, gWhite);
                    fillRect(gMemDC, BOX_X, gBlasters[i].beamY + gBlasters[i].beamH / 2 - 3, BOX_W, 6, gCyan);
                    drawSprite(&gSprBlasterFire, (int)gBlasters[i].bx, (int)gBlasters[i].by);
                }
            }
        }
        drawSoul();
    } else if (gPhase == PH_ACTION) {
        int n = (int)gTypePos; int len = (int)strlen(gMessage);
        char buf[160];
        if (n > len) n = len;
        memcpy(buf, gMessage, n); buf[n] = '\0';
        drawTextWrapped(BOX_X + 12, BOX_Y + 14, BOX_W - 24, BOX_H - 30, buf, RGB(255, 255, 255), gFontSmall);
        if (n >= len) drawText(gMemDC, BOX_X + BOX_W - 70, BOX_Y + BOX_H - 24, "[Z]", RGB(255, 255, 0), gFontTiny);
    } else { /* PH_MENU */
        drawText(gMemDC, BOX_X + 12, BOX_Y + 16, "* SANS is sparing you.", RGB(255, 255, 255), gFontSmall);
        if (gTurn + 1 >= MERCY_TURNS)
            drawText(gMemDC, BOX_X + 12, BOX_Y + 44, "* (MERCY available!)", RGB(255, 255, 0), gFontTiny);
    }

    drawHpBar();
    drawMenuButton(&gSprFight, "FIGHT", 0);
    drawMenuButton(&gSprAct,   "ACT",   1);
    drawMenuButton(&gSprItem,  "ITEM",  2);
    drawMenuButton(&gSprMercy, "MERCY", 3);
}

/* ---------------- Win32 ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) DestroyWindow(h); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        if (gMemDC) BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gMemDC, 0, 0, SRCCOPY);
        EndPaint(h, &ps); return 0;
    }
    }
    return DefWindowProcA(h, m, w, l);
}

static void initResources(void) {
    HDC wdc = GetDC(gHwnd);
    gMemDC  = CreateCompatibleDC(wdc);
    gMemBmp = CreateCompatibleBitmap(wdc, CLIENT_W, CLIENT_H);
    gOldBmp = (HBITMAP)SelectObject(gMemDC, gMemBmp);
    ReleaseDC(gHwnd, wdc);

    gBlack  = CreateSolidBrush(RGB(0, 0, 0));
    gWhite  = CreateSolidBrush(RGB(255, 255, 255));
    gRed    = CreateSolidBrush(RGB(255, 0, 0));
    gYellow = CreateSolidBrush(RGB(255, 255, 0));
    gBlue   = CreateSolidBrush(RGB(60, 120, 255));
    gDkRed  = CreateSolidBrush(RGB(90, 0, 0));
    gCyan   = CreateSolidBrush(RGB(0, 220, 255));

    gFontBig   = CreateFontA(48, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Consolas");
    gFontSmall = CreateFontA(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Consolas");
    gFontTiny  = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Consolas");

    /* 스프라이트 로드(실패해도 폴백) */
    gSprHead        = loadSprite("sans_head.bmp");
    gSprHeadBlue    = loadSprite("sans_head_blue.bmp");
    gSprHeart       = loadSprite("heart.bmp");
    gSprBlaster     = loadSprite("blaster.bmp");
    gSprBlasterFire = loadSprite("blaster_fire.bmp");
    gSprFight       = loadSprite("ui_fight.bmp");
    gSprAct         = loadSprite("ui_act.bmp");
    gSprItem        = loadSprite("ui_item.bmp");
    gSprMercy       = loadSprite("ui_mercy.bmp");
}

static void freeResources(void) {
    freeSprite(&gSprHead); freeSprite(&gSprHeadBlue); freeSprite(&gSprHeart);
    freeSprite(&gSprBlaster); freeSprite(&gSprBlasterFire);
    freeSprite(&gSprFight); freeSprite(&gSprAct); freeSprite(&gSprItem); freeSprite(&gSprMercy);
    if (gMemDC) { SelectObject(gMemDC, gOldBmp); DeleteDC(gMemDC); }
    if (gMemBmp) DeleteObject(gMemBmp);
    DeleteObject(gBlack); DeleteObject(gWhite); DeleteObject(gRed); DeleteObject(gYellow);
    DeleteObject(gBlue); DeleteObject(gDkRed); DeleteObject(gCyan);
    DeleteObject(gFontBig); DeleteObject(gFontSmall); DeleteObject(gFontTiny);
}

int main(void) {
    HINSTANCE hInst = GetModuleHandleA(NULL);
    int show = SW_SHOW;
    WNDCLASSEXA wc;
    DWORD style;
    RECT rc;
    LARGE_INTEGER freq, prev, now;
    double acc = 0.0;
    const double FIXED_DT = 1.0 / 60.0;
    MSG msg;

    { HWND con = GetConsoleWindow(); if (con) ShowWindow(con, SW_HIDE); }

    /* 고DPI 모니터에서 OS 비트맵 스케일링(화면 흐림) 방지 — 창 생성 전에 호출 */
    {
        HMODULE u32 = GetModuleHandleA("user32");
        typedef BOOL (WINAPI *PSetCtx)(void*);
        PSetCtx setCtx = u32 ? (PSetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : NULL;
        if (!setCtx || !setCtx((void*)(LONG_PTR)-4))   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
            SetProcessDPIAware();                       /* 구버전 Windows 폴백 */
    }

    srand((unsigned)time(NULL));

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "UndertaleSansWnd";
    RegisterClassExA(&wc);

    style = (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    rc.left = 0; rc.top = 0; rc.right = CLIENT_W; rc.bottom = CLIENT_H;
    AdjustWindowRect(&rc, style, FALSE);
    gHwnd = CreateWindowExA(0, wc.lpszClassName, "UNDERTALE - Sans Battle",
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInst, NULL);
    if (!gHwnd) return 0;

    /* 작업영역(작업표시줄 제외) 중앙에 창 배치 */
    {
        RECT wa, wr; int ww, wh, px, py;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        GetWindowRect(gHwnd, &wr);
        ww = wr.right - wr.left; wh = wr.bottom - wr.top;
        px = wa.left + ((wa.right - wa.left) - ww) / 2;
        py = wa.top  + ((wa.bottom - wa.top) - wh) / 2;
        if (px < wa.left) px = wa.left;
        if (py < wa.top)  py = wa.top;
        SetWindowPos(gHwnd, NULL, px, py, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    initResources();
    ShowWindow(gHwnd, show);
    UpdateWindow(gHwnd);

    timeBeginPeriod(1);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    while (gRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { gRunning = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!gRunning) break;

        QueryPerformanceCounter(&now);
        {
            int didUpdate = 0;
            double frame = (double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart;
            prev = now;
            if (frame > 0.25) frame = 0.25;
            acc += frame;
            while (acc >= FIXED_DT) { update((float)FIXED_DT); acc -= FIXED_DT; didUpdate = 1; }
            if (didUpdate) {            /* 갱신된 프레임만 그려 ~60fps로 캡(CPU 점유 절감) */
                render();
                {
                    HDC dc = GetDC(gHwnd);
                    BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gMemDC, 0, 0, SRCCOPY);
                    ReleaseDC(gHwnd, dc);
                }
            }
        }
        Sleep(1);
    }

    timeEndPeriod(1);
    stopBGM();
    freeResources();
    return 0;
}
