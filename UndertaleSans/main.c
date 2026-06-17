/*
 * UNDERTALE - Sans Battle  (Win32 GDI 버전)  /  Vertical Slice 1
 * ------------------------------------------------------------------
 * - 단일 파일, 외부 라이브러리 없음(Windows 시스템 라이브러리 gdi32/user32/winmm만 사용).
 * - 에셋 파일(BMP/WAV) 불필요. Visual Studio에서 UndertaleSans.sln 열고 F5 → 바로 실행.
 * - 우리 소유의 Win32 창 + 더블버퍼 GDI 렌더로 깜빡임 없는 60fps.
 *
 * [조작]  이동: 화살표 / WASD    시작·재시작: Z 또는 Enter    종료: ESC
 *
 * [슬라이스 1 범위]  타이틀 → 전투(빨간 영혼으로 좌우 뼈 회피, HP/무적) → 게임오버 → 재시작
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>   /* rand, srand, RAND_MAX */
#include <string.h>
#include <time.h>

/* 시스템 라이브러리 링크 (프로젝트 설정 없이 코드에서 직접 링크 → "세팅 없이 F5") */
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")

/* ---------------- 상수 ---------------- */
#define CLIENT_W   640
#define CLIENT_H   480

#define BOX_X      170          /* 전투 박스 좌상단/크기 */
#define BOX_Y      150
#define BOX_W      300
#define BOX_H      220

#define SOUL_SIZE  16
#define MAX_BONES  64
#define HIT_DAMAGE 5
#define MAX_HP     92

/* ---------------- 구조체 ([구현조건: 구조체]) ---------------- */
typedef enum { ST_TITLE, ST_BATTLE, ST_GAMEOVER } GameState;

typedef struct {
    float x, y;        /* 영혼(하트) 좌상단 좌표(px) */
    int   maxHp, hp;
    float invuln;      /* 피격 후 무적 잔여시간(초) */
} Soul;

typedef struct {
    float x, y;        /* 뼈 좌상단 */
    float vx;          /* 수평 속도(px/s) */
    int   w, h;
    int   active;
} Bone;

/* ---------------- 전역 상태 ---------------- */
static HWND     gHwnd;
static HDC      gMemDC;          /* 백버퍼 DC */
static HBITMAP  gMemBmp, gOldBmp;
static HBRUSH   gBlack, gWhite, gRed, gYellow, gBlue, gDkRed;
static HFONT    gFontBig, gFontSmall;
static int      gRunning = 1;

static GameState gState = ST_TITLE;
static Soul      gSoul;
static Bone      gBones[MAX_BONES];   /* [구현조건: 배열] 뼈 배열 */
static float     gSpawnTimer = 0.0f;
static int       gPrevConfirm = 0;    /* Z/Enter 엣지 검출 */

/* ---------------- 유틸 함수 ([구현조건: 사용자정의함수]) ---------------- */
/* 키가 눌려 있는지 (홀드 폴링) — [구현조건: 키보드입력] */
static int keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

/* [a,b) 실수 난수 — [구현조건: 랜덤함수] */
static float frand(float a, float b) {
    return a + (b - a) * ((float)rand() / (float)RAND_MAX);
}

/* 두 사각형이 겹치는가 (AABB 충돌) */
static int rectsOverlap(float ax, float ay, float aw, float ah,
                        float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void fillRect(HDC dc, int x, int y, int w, int h, HBRUSH br) {
    RECT r; r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(dc, &r, br);
}

static void drawText(HDC dc, int x, int y, const char* s, COLORREF col, HFONT f) {
    HFONT old = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    TextOutA(dc, x, y, s, (int)strlen(s));
    SelectObject(dc, old);
}

/* ---------------- 게임 로직 ---------------- */
static void resetBattle(void) {
    gSoul.x = BOX_X + BOX_W / 2.0f - SOUL_SIZE / 2.0f;
    gSoul.y = BOX_Y + BOX_H / 2.0f - SOUL_SIZE / 2.0f;
    gSoul.maxHp = MAX_HP;
    gSoul.hp = MAX_HP;
    gSoul.invuln = 0.0f;
    for (int i = 0; i < MAX_BONES; i++) gBones[i].active = 0;
    gSpawnTimer = 0.0f;
}

/* 비활성 슬롯에 뼈 하나 생성: 박스 오른쪽에서 무작위 높이로 등장 */
static void spawnBone(void) {
    for (int i = 0; i < MAX_BONES; i++) {
        if (!gBones[i].active) {
            gBones[i].active = 1;
            gBones[i].w = 10;
            gBones[i].h = (int)frand(40.0f, 90.0f);
            gBones[i].x = (float)(BOX_X + BOX_W + 4);
            gBones[i].y = frand((float)(BOX_Y + 2),
                                (float)(BOX_Y + BOX_H - 2 - gBones[i].h));
            gBones[i].vx = -frand(150.0f, 230.0f);
            return;
        }
    }
}

static void updateBattle(float dt) {
    /* 영혼 이동 입력 — [구현조건: 키보드입력] */
    const float speed = 155.0f;
    float dx = 0.0f, dy = 0.0f;
    if (keyDown(VK_LEFT)  || keyDown('A')) dx -= 1.0f;
    if (keyDown(VK_RIGHT) || keyDown('D')) dx += 1.0f;
    if (keyDown(VK_UP)    || keyDown('W')) dy -= 1.0f;
    if (keyDown(VK_DOWN)  || keyDown('S')) dy += 1.0f;
    gSoul.x += dx * speed * dt;
    gSoul.y += dy * speed * dt;

    /* 전투 박스 안으로 제한 */
    if (gSoul.x < BOX_X + 2)                       gSoul.x = (float)(BOX_X + 2);
    if (gSoul.y < BOX_Y + 2)                       gSoul.y = (float)(BOX_Y + 2);
    if (gSoul.x > BOX_X + BOX_W - 2 - SOUL_SIZE)    gSoul.x = (float)(BOX_X + BOX_W - 2 - SOUL_SIZE);
    if (gSoul.y > BOX_Y + BOX_H - 2 - SOUL_SIZE)    gSoul.y = (float)(BOX_Y + BOX_H - 2 - SOUL_SIZE);

    /* 뼈 스폰 타이머 */
    gSpawnTimer -= dt;
    if (gSpawnTimer <= 0.0f) {
        spawnBone();
        gSpawnTimer = frand(0.35f, 0.7f);
    }

    /* 무적 시간 감소 */
    if (gSoul.invuln > 0.0f) gSoul.invuln -= dt;

    /* 뼈 이동 + 충돌 판정 */
    for (int i = 0; i < MAX_BONES; i++) {
        if (!gBones[i].active) continue;
        gBones[i].x += gBones[i].vx * dt;
        if (gBones[i].x + gBones[i].w < BOX_X - 4) { gBones[i].active = 0; continue; }

        if (gSoul.invuln <= 0.0f &&
            rectsOverlap(gSoul.x, gSoul.y, SOUL_SIZE, SOUL_SIZE,
                         gBones[i].x, gBones[i].y, (float)gBones[i].w, (float)gBones[i].h)) {
            gSoul.hp -= HIT_DAMAGE;
            gSoul.invuln = 1.0f;                /* 1초 무적 → 다중 차감 방지 */
            if (gSoul.hp <= 0) { gSoul.hp = 0; gState = ST_GAMEOVER; }
        }
    }
}

static void update(float dt) {
    int confirm = keyDown('Z') || keyDown(VK_RETURN);
    int confirmPressed = confirm && !gPrevConfirm;   /* 한 번 누름만 검출 */
    gPrevConfirm = confirm;

    switch (gState) {
    case ST_TITLE:    if (confirmPressed) { resetBattle(); gState = ST_BATTLE; } break;
    case ST_BATTLE:   updateBattle(dt); break;
    case ST_GAMEOVER: if (confirmPressed) { gState = ST_TITLE; } break;
    }
}

/* ---------------- 렌더 (백버퍼에 그린 뒤 한 번에 전송) ---------------- */
static void render(void) {
    fillRect(gMemDC, 0, 0, CLIENT_W, CLIENT_H, gBlack);  /* 배경 */

    if (gState == ST_TITLE) {
        drawText(gMemDC, CLIENT_W / 2 - 145, 130, "UNDERTALE", RGB(255, 255, 255), gFontBig);
        drawText(gMemDC, CLIENT_W / 2 - 95,  235, "* Sans Battle (Slice 1)", RGB(255, 255, 255), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 110, 290, "Press Z or Enter to start", RGB(255, 255, 0), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 110, 320, "Move: Arrow keys / WASD", RGB(160, 160, 160), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 110, 345, "Quit: ESC", RGB(160, 160, 160), gFontSmall);
    }
    else if (gState == ST_BATTLE) {
        /* 전투 박스: 흰 테두리 + 검은 안쪽 */
        fillRect(gMemDC, BOX_X - 3, BOX_Y - 3, BOX_W + 6, BOX_H + 6, gWhite);
        fillRect(gMemDC, BOX_X,     BOX_Y,     BOX_W,     BOX_H,     gBlack);

        /* 뼈 */
        for (int i = 0; i < MAX_BONES; i++)
            if (gBones[i].active)
                fillRect(gMemDC, (int)gBones[i].x, (int)gBones[i].y,
                         gBones[i].w, gBones[i].h, gWhite);

        /* 영혼(무적 중에는 깜빡임) */
        if (!(gSoul.invuln > 0.0f && ((int)(gSoul.invuln * 16.0f) % 2)))
            fillRect(gMemDC, (int)gSoul.x, (int)gSoul.y, SOUL_SIZE, SOUL_SIZE, gRed);

        /* HP 바 */
        int hpx = CLIENT_W / 2 - 100, hpy = CLIENT_H - 45, hpw = 200, hph = 18;
        int cur = (int)(hpw * (gSoul.hp / (float)gSoul.maxHp));
        fillRect(gMemDC, hpx, hpy, hpw, hph, gDkRed);     /* 소진분 */
        fillRect(gMemDC, hpx, hpy, cur, hph, gYellow);    /* 현재 HP */
        {
            char buf[64];
            wsprintfA(buf, "HP  %d / %d", gSoul.hp, gSoul.maxHp);
            drawText(gMemDC, hpx + hpw + 14, hpy - 3, buf, RGB(255, 255, 255), gFontSmall);
            drawText(gMemDC, BOX_X, BOX_Y - 34, "* heh heh heh... you're gonna have a bad time.",
                     RGB(255, 255, 255), gFontSmall);
        }
    }
    else { /* ST_GAMEOVER */
        drawText(gMemDC, CLIENT_W / 2 - 115, 175, "GAME OVER", RGB(255, 0, 0), gFontBig);
        drawText(gMemDC, CLIENT_W / 2 - 90,  280, "* Stay determined...", RGB(255, 255, 255), gFontSmall);
        drawText(gMemDC, CLIENT_W / 2 - 120, 320, "Press Z to return to title", RGB(255, 255, 0), gFontSmall);
    }

    /* 백버퍼 → 화면 전송 */
    HDC dc = GetDC(gHwnd);
    BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gMemDC, 0, 0, SRCCOPY);
    ReleaseDC(gHwnd, dc);
}

/* ---------------- Win32 윈도우 ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (gMemDC) BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gMemDC, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
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
    gDkRed  = CreateSolidBrush(RGB(110, 0, 0));

    gFontBig   = CreateFontA(48, 0, 0, 0, FW_BOLD,   0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, 0, 0, "Consolas");
    gFontSmall = CreateFontA(18, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, 0, 0, "Consolas");
}

static void freeResources(void) {
    if (gMemDC) { SelectObject(gMemDC, gOldBmp); DeleteDC(gMemDC); }
    if (gMemBmp) DeleteObject(gMemBmp);
    DeleteObject(gBlack); DeleteObject(gWhite); DeleteObject(gRed);
    DeleteObject(gYellow); DeleteObject(gBlue); DeleteObject(gDkRed);
    DeleteObject(gFontBig); DeleteObject(gFontSmall);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd;
    srand((unsigned)time(NULL));   /* 시드 1회 */

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "UndertaleSansWnd";
    RegisterClassExA(&wc);

    /* 클라이언트 영역이 정확히 640x480이 되도록 창 크기 계산 (크기 고정) */
    DWORD style = (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    RECT rc; rc.left = 0; rc.top = 0; rc.right = CLIENT_W; rc.bottom = CLIENT_H;
    AdjustWindowRect(&rc, style, FALSE);

    gHwnd = CreateWindowExA(0, wc.lpszClassName, "UNDERTALE - Sans Battle",
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rc.right - rc.left, rc.bottom - rc.top,
                            NULL, NULL, hInst, NULL);
    if (!gHwnd) return 0;

    initResources();
    ShowWindow(gHwnd, show);
    UpdateWindow(gHwnd);

    /* 고정 타임스텝 게임 루프 (60Hz) */
    timeBeginPeriod(1);
    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);
    const double FIXED_DT = 1.0 / 60.0;
    double acc = 0.0;

    MSG msg;
    while (gRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { gRunning = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!gRunning) break;

        QueryPerformanceCounter(&now);
        double frame = (double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart;
        prev = now;
        if (frame > 0.25) frame = 0.25;          /* 디버거 정지 등 대비 */
        acc += frame;
        while (acc >= FIXED_DT) { update((float)FIXED_DT); acc -= FIXED_DT; }

        render();
        Sleep(1);
    }

    timeEndPeriod(1);
    freeResources();
    return 0;
}
