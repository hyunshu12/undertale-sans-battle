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
#include <wchar.h>    /* wcslen, wmemcpy (한글 유니코드 출력) */
#include <time.h>
#include <math.h>     /* cos (샌즈 회피 애니) */
#include "game.h"     /* BTS VM/탄막 통합 (Box gBox, VM, hazards API) */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
#define MERCY_TURNS 4          /* (구) 폴백 패턴용 */
#define MERCY_HA 8             /* 이 횟수 이상 공격 후(샌즈가 지침) MERCY로 승리 가능 */
#define ENEMY_DURATION 8.0f    /* 적턴 길이(초) */

#define SANS_W 128
#define SANS_H 120
#define SANS_X ((CLIENT_W - SANS_W) / 2)
#define SANS_Y 6

/* 메뉴 버튼 배치 */
#define BTN_W 110
#define BTN_H 42
#define BTN_Y 428
#define BTN_STEP 124
#define BTN_X0 79

/* ---------------- 구조체 ([구현조건: 구조체]) ---------------- */
typedef enum { ST_TITLE, ST_BATTLE, ST_GAMEOVER, ST_WIN } GameState;
typedef enum { PH_DIALOGUE, PH_ENEMY, PH_MENU, PH_ACTION, PH_FIGHT } Phase;

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
static HBRUSH   gBlack, gWhite, gRed, gYellow, gBlue, gDkRed, gCyan, gKarmaBr;
static HFONT    gFontBig, gFontSmall, gFontTiny;
static int      gPixelFontOk = 0;   /* 네오둥근모 픽셀 폰트 로드 성공 여부 */
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
static wchar_t   gMessage[160] = L"";      /* 액션 메시지 */
static float     gMenacePulse = 0.0f;      /* 샌즈 파란 눈 토글 */

/* --- BTS VM 통합 전역 --- */
Box           gBox = { BOX_X, BOX_Y, BOX_W, BOX_H };  /* 전투 박스(CombatZoneResize로 변경) */
static VM     gVM;
static VMHost gHost;
static int    gUseVM = 0;       /* 1=VM 공격 구동중, 0=Slice2 패턴 폴백 */
static int    gSoulMode = 0;    /* 0=빨강,1=파랑(BLUE 물리는 Slice4) */
static int    gAttackEnded = 0;
static float  gPrevSx = 0.0f, gPrevSy = 0.0f;
static int    gDebug = 0;       /* F1 디버그 오버레이 */
static char   gAtkName[32] = "";
static int    gAtkIndex = 0;    /* 공격 시퀀스 인덱스 */
static double gMaxFall = 750.0; /* HeartMaxFallSpeed (BLUE 물리는 다음 푸시) */
static int    gBlackScreen = 0;
static float  gShakeI = 0.0f, gShakeT = 0.0f;
static int    gShakeDx = 0, gShakeDy = 0;
static wchar_t gBubble[256] = L"";   /* 샌즈 말풍선(한국어) */
static int    gBubbleLen = 0;
static float  gBubbleType = 0.0f, gBubbleTimer = 0.0f;
static float  gVx = 0.0f, gVy = 0.0f;   /* 파란 영혼 속도(px/s) */
static int    gPrevUp = 0;              /* 점프키 엣지 검출 */
static int    gKR = 0;                  /* KARMA(누적 카르마 데미지) */
static float  gKR_t = 0.0f;

/* --- 전투 흐름(HitAttempts) / 샌즈 회피 / 대사 큐 --- */
static int    gHitAttempts = 0;          /* FIGHT(샌즈 회피) 성공 횟수 → 공격 선택 */
static char   gCurAtk[32] = "";          /* 진행중 공격 이름(엔딩 트리거 판정) */
static float  gSansDodgeT = 0.0f;        /* 회피 애니 타이머 */
static float  gSansOffsetX = 0.0f;       /* 샌즈 머리 가로 오프셋(회피) */
static const wchar_t* gDlgQueue[10];     /* 인트로/엔딩 공용 대사 큐 */
static int    gDlgN = 0, gDlgIdx = 0, gDlgAfter = 0;  /* after: 0=적턴, 1=승리 */

/* 키 엣지 검출용 이전 상태 */
static int gPrevZ = 0, gPrevLeft = 0, gPrevRight = 0;

/* 대사 ([구현조건: 배열]) */
static const wchar_t* gDialogues[] = {
    L"* 오늘은 밖이 참 아름다운 날이지.",
    L"* 새들은 지저귀고, 꽃들은 피어나고...",
    L"* 이런 날엔 말이야, 너 같은 녀석들은...",
    L"* 지옥에서 불타고 있어야 하는 거야.",
    L"* 흥. 아직도 멀쩡히 서 있네?",
    L"* ...이쯤에서 그만 좀 봐주지 그래."
};
#define DIALOGUE_COUNT 6

/* 공격 테이블 (BTS HitAttempts 순서). 모든 CSV는 assets/attacks/ 에 존재. */
static const char* gIntroTable[] = {
    "sans_intro", "sans_bonegap1", "sans_bluebone", "sans_bonegap2",
    "sans_platforms1", "sans_platforms2", "sans_platforms3", "sans_platforms4",
    "sans_platformblaster", "sans_platforms4hard", "sans_bonegap1fast",
    "sans_boneslideh", "sans_platformblasterfast"
};
#define INTRO_N 13   /* HA 0..12 */
static const char* gMultiTable[] = {
    "sans_multi1", "sans_randomblaster1", "sans_multi2", "sans_bonestab1",
    "sans_bonestab2", "sans_randomblaster2", "sans_boneslidev", "sans_multi3"
};
#define MULTI_N 8     /* HA 14..21 */

/* ---------------- 전방 선언 ---------------- */
static void startBattle(void);
static void startTurn(void);
static void startEnemyPhase(void);
static void advanceTurn(void);
static int  RunAttack(const char* name);
static void startDialogue(const wchar_t** lines, int n, int after);
static void startEnding(int viaFinal);
static const char* chooseAttack(void);

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
static void drawText(HDC dc, int x, int y, const wchar_t* s, COLORREF col, HFONT f) {
    HFONT old = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, col);
    TextOutW(dc, x, y, s, (int)wcslen(s));   /* 유니코드 출력(한글 지원) */
    SelectObject(dc, old);
}
/* 사각형 영역 안에서 자동 줄바꿈(긴 대사/메시지가 박스 밖으로 새지 않게) */
static void drawTextWrapped(int x, int y, int w, int h, const wchar_t* s, COLORREF col, HFONT f) {
    RECT r; HFONT old;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    old = (HFONT)SelectObject(gMemDC, f);
    SetBkMode(gMemDC, TRANSPARENT); SetTextColor(gMemDC, col);
    DrawTextW(gMemDC, s, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(gMemDC, old);
}
/* cx를 기준으로 가로 중앙 정렬해 출력(한/영 폭 차이에도 정렬 유지) */
static void drawTextCentered(int cx, int y, const wchar_t* s, COLORREF col, HFONT f) {
    SIZE sz; HFONT old = (HFONT)SelectObject(gMemDC, f);
    GetTextExtentPoint32W(gMemDC, s, (int)wcslen(s), &sz);
    SetBkMode(gMemDC, TRANSPARENT); SetTextColor(gMemDC, col);
    TextOutW(gMemDC, cx - sz.cx / 2, y, s, (int)wcslen(s));
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

/* 샌즈 음성("으으으"). BGM(PlaySound)과 동시재생을 위해 MCI 별도 채널 사용. */
static int   gVoiceOpen = 0;
static float gVoiceTimer = 0.0f;
static void openVoice(void) {
    char cmd[320];
    if (gVoiceOpen) return;
    wsprintfA(cmd, "open \"%s\" type waveaudio alias spk", assetPath("sans_speak.wav"));
    if (mciSendStringA(cmd, NULL, 0, NULL) == 0) gVoiceOpen = 1;
}
static void playVoice(void) {
    if (!gVoiceOpen) openVoice();
    if (gVoiceOpen) mciSendStringA("play spk from 0", NULL, 0, NULL);
}

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

/* 대사 큐 시작(인트로/엔딩 공용). after: 0=대사 끝나면 적턴, 1=승리 */
static void startDialogue(const wchar_t** lines, int n, int after) {
    int i;
    if (n > 10) n = 10;
    for (i = 0; i < n; i++) gDlgQueue[i] = lines[i];
    gDlgN = n; gDlgIdx = 0; gDlgAfter = after;
    gPhase = PH_DIALOGUE; gTypePos = 0.0f;
}
/* HitAttempts 로 다음 공격 선택: <13=INTRO, ==13=SPARE, 14~21=MULTI, >=22=FINAL */
static const char* chooseAttack(void) {
    int ha = gHitAttempts;
    if (ha < INTRO_N) return gIntroTable[ha];
    if (ha == 13)     return "sans_spare";
    if (ha < 22)      { int i = ha - 14; return (i < MULTI_N) ? gMultiTable[i] : gMultiTable[rand() % MULTI_N]; }
    return "sans_final";
}
/* 엔딩 시퀀스: 탄막 정리 후 마무리 대사 → 승리 */
static void startEnding(int viaFinal) {
    static const wchar_t* fin[] = {
        L"* ...",
        L"* 헉... 헉...",
        L"* 너 정말... 끈질긴 녀석이구나.",
        L"* 좋아. 그래... 네가 이긴 걸로 하지.",
        L"* 난 그릴비네 가게나 가야겠다. 잘 있어라."
    };
    static const wchar_t* spare[] = {
        L"* ...",
        L"* 정말로 날 봐주겠다는 거야?",
        L"* ...훗. 별난 녀석이네.",
        L"* 좋아. 오늘은 이쯤에서 끝내자.",
        L"* 그릴비네 가게나 가야겠다. 잘 있어라."
    };
    clearHazards(); haz_reset();
    gSoulMode = 0; gBlackScreen = 0; gBubbleLen = 0; gSansOffsetX = 0.0f;
    gKR = 0; gKR_t = 0.0f;   /* 승리 확정 — 잔여 KARMA로 엔딩 중 사망하지 않게 */
    startDialogue(viaFinal ? fin : spare, 5, 1);
}

static void startBattle(void) {
    gSoul.maxHp = MAX_HP; gSoul.hp = MAX_HP; gSoul.invuln = 0.0f;
    gKR = 0; gKR_t = 0.0f; gAtkIndex = 0; gHitAttempts = 0;
    gTurn = 0; gMenuIndex = 0; gItemsLeft = 3;
    gCurAtk[0] = 0; gSansOffsetX = 0.0f; gBubbleLen = 0; gBlackScreen = 0;
    clearHazards(); centerSoul();
    gState = ST_BATTLE;
    playBGM();
    startDialogue(gDialogues, DIALOGUE_COUNT, 0);   /* 인트로 독백 → 첫 공격 */
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
    gBox.x = BOX_X; gBox.y = BOX_Y; gBox.w = BOX_W; gBox.h = BOX_H;  /* 박스 기본값 리셋 */
    gAttackEnded = 0;
    gSoulMode = 0; gVx = 0.0f; gVy = 0.0f; gPrevUp = 0;             /* 영혼 물리 리셋 */
    {   /* HitAttempts 기반 공격 선택(INTRO/SPARE/MULTI/FINAL) */
        const char* atk = chooseAttack();
        strncpy(gCurAtk, atk, 31); gCurAtk[31] = 0;
        gUseVM = RunAttack(atk);   /* 실패(파일 없음) 시 Slice2 패턴 폴백 */
    }
    if (gUseVM) gEnemyTime = 90.0f;   /* VM 안전장치: 보통 EndAttack로 먼저 끝남(멈춤 방지) */
}
/* 적턴 종료 전환(VM·폴백 공용). 최종 공격이면 엔딩, 아니면 메뉴. */
static void endEnemyPhase(void) {
    clearHazards(); haz_reset();
    if (strcmp(gCurAtk, "sans_final") == 0) startEnding(1);
    else { gPhase = PH_MENU; gMenuIndex = 0; }
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

/* --- hazards/VM 가 호출하는 game 후크 (game.h 선언) --- */
void game_get_heart(double* cx, double* cy) { *cx = gSoul.x + SOUL_SIZE / 2.0; *cy = gSoul.y + SOUL_SIZE / 2.0; }
int  game_heart_moving(void) {
    if (gSoulMode == 1) return keyDown(VK_LEFT) || keyDown('A') || keyDown(VK_RIGHT) || keyDown('D');
    return (gSoul.x != gPrevSx) || (gSoul.y != gPrevSy);
}
void game_hurt(int dmg, int karma) {
    if (gSoul.invuln > 0.0f) return;
    gSoul.hp -= dmg;
    game_play_sound("PlayerDamaged");
    gKR += karma; if (gKR > 40) gKR = 40;
    if (gKR >= gSoul.hp) gKR = gSoul.hp > 1 ? gSoul.hp - 1 : 0;   /* 즉사 방지 */
    gSoul.invuln = 0.4f;
    if (gSoul.hp <= 0) { gSoul.hp = 0; gKR = 0; gState = ST_GAMEOVER; stopBGM(); }
}
void game_set_heart_mode(int blue) { gSoulMode = blue; }
void game_teleport_heart(double x, double y) {
    gSoul.x = (float)(x - SOUL_SIZE / 2.0); gSoul.y = (float)(y - SOUL_SIZE / 2.0);
}
void game_end_attack(void) { gAttackEnded = 1; }
void game_set_max_fall(double v) { gMaxFall = v; }
double game_get_max_fall(void) { return gMaxFall; }
/* 효과음: 이미 연 MCI alias는 재사용(빠른 재생). BGM(PlaySound)·음성(spk)과 별도 채널이라 동시재생. */
static char gSfxOpen[16][40]; static int gSfxN = 0;
void game_play_sound(const char* name) {
    char alias[40], rel[64], cmd[400], play[64]; int i, found = 0;
    if (!name || !name[0]) return;
    wsprintfA(alias, "sfx_%s", name);
    for (i = 0; i < gSfxN; i++) if (strcmp(gSfxOpen[i], alias) == 0) { found = 1; break; }
    if (!found) {
        wsprintfA(rel, "sfx_%s.wav", name);
        wsprintfA(cmd, "open \"%s\" type waveaudio alias %s", assetPath(rel), alias);
        if (mciSendStringA(cmd, NULL, 0, NULL) != 0) return;   /* 파일 없음/실패 → 무음 */
        if (gSfxN < 16) { strncpy(gSfxOpen[gSfxN], alias, 39); gSfxOpen[gSfxN][39] = 0; gSfxN++; }
    }
    wsprintfA(play, "play %s from 0", alias);
    mciSendStringA(play, NULL, 0, NULL);
}
void game_shake(double intensity) { if ((float)intensity > gShakeI) gShakeI = (float)intensity; }
void game_set_blackscreen(int on) { gBlackScreen = on; }
/* 알려진 샌즈 대사 한국어 매핑(없으면 영어 그대로 와이드 변환) */
static const wchar_t* sansTextKor(const char* en) {
    static wchar_t buf[256]; int i;
    if (strcmp(en, "ready?") == 0) return L"준비됐어?";
    if (strcmp(en, "here we go.") == 0) return L"자, 간다.";
    if (strcmp(en, "huff... puff...") == 0) return L"헉... 헉...";
    if (strcmp(en, "alright, i guess you win.") == 0) return L"그래... 네가 이긴 걸로 하지.";
    for (i = 0; en[i] && i < 255; i++) buf[i] = (wchar_t)(unsigned char)en[i];
    buf[i] = 0; return buf;
}
void game_sans_text(const char* text) {
    lstrcpynW(gBubble, sansTextKor(text), 255);
    gBubbleLen = (int)wcslen(gBubble);
    gBubbleType = 0.0f; gBubbleTimer = 3.0f;
}

/* attacks/<name>.csv 를 읽어 VM 로드. 성공 1, 실패(파일 없음) 0. */
static int RunAttack(const char* name) {
    char rel[260]; const char* path; FILE* f; long n; char* text;
    wsprintfA(rel, "attacks\\%s.csv", name);
    path = assetPath(rel);
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }
    text = (char*)malloc((size_t)n + 1);
    if (!text) { fclose(f); return 0; }
    fread(text, 1, (size_t)n, f); text[n] = 0; fclose(f);
    vm_load(&gVM, text, gHost);   /* code[]로 복사하므로 text 해제 가능 */
    free(text);
    haz_reset(); haz_set_vm(&gVM);
    strncpy(gAtkName, name, 31); gAtkName[31] = 0;
    return 1;
}

static void updateEnemyPhase(float dt) {
    float dx = 0.0f, dy = 0.0f;
    const float speed = 160.0f;
    int i;

    gPrevSx = gSoul.x; gPrevSy = gSoul.y;   /* 이동 판정용 직전 위치 */

    /* 영혼 이동 [구현조건: 키보드입력] */
    if (keyDown(VK_LEFT)  || keyDown('A')) dx -= 1.0f;
    if (keyDown(VK_RIGHT) || keyDown('D')) dx += 1.0f;
    if (keyDown(VK_UP)    || keyDown('W')) dy -= 1.0f;
    if (keyDown(VK_DOWN)  || keyDown('S')) dy += 1.0f;
    if (gSoul.invuln > 0.0f) gSoul.invuln -= dt;

    if (gUseVM) {
        if (gSoulMode == 1) {
            /* === 파란 영혼: 중력+점프 (각도 90=아래) === */
            float floorY = (float)(gBox.y + gBox.h - 2 - SOUL_SIZE);
            int   up = keyDown(VK_UP) || keyDown('W');
            int   grounded;
            double ptopY = 0;
            float lat = 0.0f, g;
            if (keyDown(VK_LEFT)  || keyDown('A')) lat -= 1.0f;
            if (keyDown(VK_RIGHT) || keyDown('D')) lat += 1.0f;
            gVx = lat * 150.0f;
            grounded = (gSoul.y >= floorY - 1.0f) ||
                       (gVy >= 0 && haz_is_solid(gSoul.x, gSoul.y + SOUL_SIZE, SOUL_SIZE, 3, &ptopY) &&
                        gSoul.y + SOUL_SIZE <= ptopY + 8);
            if (!grounded) {                                  /* 가변 중력 밴드 */
                if (gVy > 240.0f) g = 540.0f; else if (gVy > 15.0f) g = 180.0f;
                else if (gVy > -30.0f) g = 450.0f; else g = 180.0f;
                gVy += g * dt;
            }
            if (gVy >  (float)gMaxFall) gVy =  (float)gMaxFall;
            if (gVy < -(float)gMaxFall) gVy = -(float)gMaxFall;
            if (up && !gPrevUp && grounded) gVy = -180.0f;    /* 점프 임펄스 */
            if (!up && gVy < -30.0f) gVy = -30.0f;            /* 가변 점프컷(짧은 점프) */
            gPrevUp = up;
            gSoul.x += gVx * dt;
            gSoul.y += gVy * dt;
            if (gSoul.x < gBox.x + 2) gSoul.x = (float)(gBox.x + 2);
            if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
            if (gVy >= 0 && haz_is_solid(gSoul.x, gSoul.y + SOUL_SIZE, SOUL_SIZE, 4, &ptopY) &&
                gSoul.y + SOUL_SIZE <= ptopY + 10) { gSoul.y = (float)(ptopY - SOUL_SIZE); gVy = 0.0f; }
            if (gSoul.y >= floorY) { gSoul.y = floorY; gVy = 0.0f; }
            if (gSoul.y < gBox.y + 2) { gSoul.y = (float)(gBox.y + 2); if (gVy < 0.0f) gVy = 0.0f; }
            if (gVy == 0.0f) {   /* 이동 플랫폼 위에 서면 함께 끌려감(착지 물리) */
                double pvx = haz_platform_vx(gSoul.x, gSoul.y + SOUL_SIZE, SOUL_SIZE);
                if (pvx != 0.0) {
                    gSoul.x += (float)(pvx * dt);
                    if (gSoul.x < gBox.x + 2) gSoul.x = (float)(gBox.x + 2);
                    if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
                }
            }
        } else {
            /* === 빨간 영혼: 자유 8방향 === */
            gSoul.x += dx * speed * dt;
            gSoul.y += dy * speed * dt;
            if (gSoul.x < gBox.x + 2)                      gSoul.x = (float)(gBox.x + 2);
            if (gSoul.y < gBox.y + 2)                      gSoul.y = (float)(gBox.y + 2);
            if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
            if (gSoul.y > gBox.y + gBox.h - 2 - SOUL_SIZE) gSoul.y = (float)(gBox.y + gBox.h - 2 - SOUL_SIZE);
        }
        vm_step(&gVM, dt);
        haz_update(dt);
        if (gState != ST_BATTLE) return;            /* 피격으로 게임오버됐을 수 있음 */
        gEnemyTime -= dt;                            /* 안전장치 카운트다운 */
        if (gAttackEnded || (gVM.finished && !vm_is_running(&gVM)) || gEnemyTime <= 0.0f)
            endEnemyPhase();                        /* 최종이면 엔딩, 아니면 메뉴 */
        return;
    }

    /* --- 기존 Slice2 패턴 (VM 로드 실패 시 폴백) --- */
    gSoul.x += dx * speed * dt;
    gSoul.y += dy * speed * dt;
    if (gSoul.x < BOX_X + 2)                    gSoul.x = (float)(BOX_X + 2);
    if (gSoul.y < BOX_Y + 2)                    gSoul.y = (float)(BOX_Y + 2);
    if (gSoul.x > BOX_X + BOX_W - 2 - SOUL_SIZE) gSoul.x = (float)(BOX_X + BOX_W - 2 - SOUL_SIZE);
    if (gSoul.y > BOX_Y + BOX_H - 2 - SOUL_SIZE) gSoul.y = (float)(BOX_Y + BOX_H - 2 - SOUL_SIZE);
    if (gTurn % 2 == 0) {
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
        gBlasterTimer -= dt;
        if (gBlasterTimer <= 0.0f) { spawnBlaster(); gBlasterTimer = frand(1.0f, 1.5f); }
        for (i = 0; i < MAX_BLASTERS; i++) {
            if (!gBlasters[i].active) continue;
            gBlasters[i].timer -= dt;
            if (gBlasters[i].state == 0) {
                if (gBlasters[i].timer <= 0.0f) { gBlasters[i].state = 1; gBlasters[i].timer = 0.55f; }
            } else if (gBlasters[i].state == 1) {
                if (rectsOverlap(gSoul.x, gSoul.y, SOUL_SIZE, SOUL_SIZE,
                                 (float)BOX_X, (float)gBlasters[i].beamY, (float)BOX_W, (float)gBlasters[i].beamH))
                    hurtSoul();
                if (gBlasters[i].timer <= 0.0f) { gBlasters[i].state = 2; gBlasters[i].timer = 0.2f; }
            } else {
                if (gBlasters[i].timer <= 0.0f) gBlasters[i].active = 0;
            }
        }
    }
    gEnemyTime -= dt;
    if (gEnemyTime <= 0.0f && gState == ST_BATTLE) endEnemyPhase();
}

/* 메뉴 액션 수행 */
static void doAction(int idx) {
    switch (idx) {
    case 0: /* FIGHT → 샌즈 회피 애니(끝나면 HitAttempts++ → 다음 공격) */
        gPhase = PH_FIGHT; gSansDodgeT = 0.0f; gSansOffsetX = 0.0f;
        game_play_sound("Slam");   /* 헛스윙(있으면) */
        return;
    case 1: /* ACT */
        lstrcpyW(gMessage, L"* 관찰.  샌즈 - 공격 1 방어 1.\n* 끈질기게 버티는 수밖에 없다.");
        break;
    case 2: /* ITEM */
        if (gItemsLeft > 0) {
            gItemsLeft--;
            gSoul.hp += 20; if (gSoul.hp > gSoul.maxHp) gSoul.hp = gSoul.maxHp;
            wsprintfW(gMessage, L"* 몬스터 캔디를 먹었다. (+20 HP)\n* (%d개 남음)", gItemsLeft);
        } else {
            lstrcpyW(gMessage, L"* 아이템이 다 떨어졌다.");
        }
        break;
    default: /* MERCY */
        if (gHitAttempts >= MERCY_HA) { startEnding(0); return; }   /* 샌즈가 지침 → 자비 승리 */
        lstrcpyW(gMessage, L"* 샌즈는 아직 포기할 생각이 없어 보인다.");
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

    /* 화면 흔들림 감쇠 */
    if (gShakeI > 0.0f) {
        gShakeT -= dt;
        if (gShakeT <= 0.0f) {
            gShakeDx = (int)frand(-gShakeI, gShakeI);
            gShakeDy = (int)frand(-gShakeI, gShakeI);
            gShakeT = 1.0f / 30.0f;
            gShakeI -= dt * 60.0f;
            if (gShakeI < 0.0f) gShakeI = 0.0f;
        }
    } else { gShakeDx = 0; gShakeDy = 0; }

    switch (gState) {
    case ST_TITLE:
        if (zPressed) startBattle();
        break;
    case ST_BATTLE:
        if (gBubbleLen > 0) {           /* 샌즈 말풍선 타이핑/표시 */
            if (gBubbleType < gBubbleLen) {
                gBubbleType += dt * 24.0f;
                gVoiceTimer -= dt; if (gVoiceTimer <= 0.0f) { playVoice(); gVoiceTimer = 0.09f; }
            } else { gBubbleTimer -= dt; if (gBubbleTimer <= 0.0f) gBubbleLen = 0; }
        }
        if (gKR > 0 && gSoul.hp > 1) {  /* KARMA 드레인 */
            float iv = gKR >= 40 ? 0.033f : gKR >= 30 ? 0.066f : gKR >= 20 ? 0.166f : gKR >= 10 ? 0.5f : 1.0f;
            gKR_t += dt;
            if (gKR_t >= iv) {
                gKR--; gSoul.hp--; gKR_t = 0.0f;
                if (gSoul.hp <= 0) { gSoul.hp = 0; gKR = 0; gState = ST_GAMEOVER; stopBGM(); }
            }
        }
        if (gPhase == PH_DIALOGUE) {
            const wchar_t* line = gDlgQueue[gDlgIdx];
            int len = (int)wcslen(line);
            gTypePos += dt * 28.0f;                 /* 타이핑 속도 */
            if ((int)gTypePos < len) { gVoiceTimer -= dt; if (gVoiceTimer <= 0.0f) { playVoice(); gVoiceTimer = 0.09f; } }
            if (zPressed) {
                if (gTypePos < len) { gTypePos = (float)len; }   /* 1회 누르면 즉시 완성 */
                else {                                            /* 다음 대사 / 큐 끝 처리 */
                    gDlgIdx++;
                    if (gDlgIdx >= gDlgN) {
                        if (gDlgAfter == 1) { gState = ST_WIN; stopBGM(); }
                        else startEnemyPhase();
                    } else { gTypePos = 0.0f; }
                }
            }
        } else if (gPhase == PH_ENEMY) {
            updateEnemyPhase(dt);
        } else if (gPhase == PH_MENU) {
            updateMenuPhase(lPressed, rPressed, zPressed);
        } else if (gPhase == PH_FIGHT) {
            /* 샌즈 회피: 옆으로 휙 → 0.45s 후 제자리. 끝나면 HitAttempts++ → 다음 공격 */
            gSansDodgeT += dt;
            gSansOffsetX = -(float)cos((double)gSansDodgeT * 225.0 * M_PI / 180.0) * 100.0f;
            if (gSansDodgeT > 0.45f) {
                gSansOffsetX = 0.0f;
                gHitAttempts++;
                startEnemyPhase();
            }
        } else { /* PH_ACTION (ACT/ITEM/MERCY 결과) → 메뉴로 복귀 */
            int len = (int)wcslen(gMessage);
            gTypePos += dt * 32.0f;
            if ((int)gTypePos < len) { gVoiceTimer -= dt; if (gVoiceTimer <= 0.0f) { playVoice(); gVoiceTimer = 0.09f; } }
            if (zPressed) {
                if (gTypePos < len) gTypePos = (float)len;
                else { gPhase = PH_MENU; gMenuIndex = 0; }
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
    /* 일정 주기로 파란 눈 연출. gSansOffsetX 만큼 좌우(회피 애니) */
    int sx = SANS_X + (int)gSansOffsetX;
    int menace = ((int)(gMenacePulse * 1.5f) % 4 == 0);
    Sprite* s = (menace && gSprHeadBlue.ok) ? &gSprHeadBlue : &gSprHead;
    if (s->ok) { drawSprite(s, sx, SANS_Y); return; }
    /* 폴백: GDI 해골 */
    fillRect(gMemDC, sx + 24, SANS_Y + 10, 80, 78, gWhite);
    fillRect(gMemDC, sx + 40, SANS_Y + 32, 14, 16, gBlack);
    fillRect(gMemDC, sx + 74, SANS_Y + 32, 14, 16, menace ? gCyan : gBlack);
    fillRect(gMemDC, sx + 40, SANS_Y + 60, 48, 6, gBlack);
}
static void drawMenuButton(Sprite* s, const wchar_t* label, int idx) {
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
    int hpx = 250, hpy = 398, hpw = 120, hph = 20;
    int cur = (int)(hpw * (gSoul.hp / (float)gSoul.maxHp));
    wchar_t buf[48];
    drawText(gMemDC, 120, hpy - 1, L"CHARA   LV 19", RGB(255, 255, 255), gFontSmall);
    fillRect(gMemDC, hpx, hpy, hpw, hph, gDkRed);
    fillRect(gMemDC, hpx, hpy, cur, hph, gYellow);
    if (gKR > 0) {   /* KARMA(보라) — 현재 HP의 오른쪽 끝 일부가 깎일 예정 */
        int krw;
        if (!gKarmaBr) gKarmaBr = CreateSolidBrush(RGB(150, 30, 160));
        krw = (int)(hpw * (gKR / (float)gSoul.maxHp));
        if (krw > cur) krw = cur;
        fillRect(gMemDC, hpx + cur - krw, hpy, krw, hph, gKarmaBr);
    }
    wsprintfW(buf, L"HP %d / %d", gSoul.hp, gSoul.maxHp);
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
        drawTextCentered(CLIENT_W / 2, 118, L"UNDERTALE", RGB(255, 255, 255), gFontBig);
        drawTextCentered(CLIENT_W / 2, 232, L"* 샌즈와의 전투", RGB(255, 255, 255), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 286, L"Z 또는 Enter를 눌러 시작", RGB(255, 255, 0), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 324, L"이동: 방향키/WASD    메뉴: ← →    확인: Z", RGB(160, 160, 160), gFontTiny);
        drawTextCentered(CLIENT_W / 2, 348, L"종료: ESC", RGB(160, 160, 160), gFontTiny);
        return;
    }
    if (gState == ST_GAMEOVER) {
        drawTextCentered(CLIENT_W / 2, 172, L"GAME OVER", RGB(255, 0, 0), gFontBig);
        drawTextCentered(CLIENT_W / 2, 284, L"* 의지를 잃지 마...", RGB(255, 255, 255), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 324, L"Z를 눌러 타이틀로 돌아가기", RGB(255, 255, 0), gFontSmall);
        return;
    }
    if (gState == ST_WIN) {
        drawSansHead();
        drawTextCentered(CLIENT_W / 2, 200, L"* 흠. 난 그릴비네 가게나 가야겠다.", RGB(255, 255, 255), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 240, L"승리!  (자비)", RGB(255, 255, 0), gFontBig);
        drawTextCentered(CLIENT_W / 2, 308, L"Z를 눌러 타이틀로 돌아가기", RGB(255, 255, 0), gFontSmall);
        return;
    }

    /* ---- 전투 ---- */
    drawSansHead();

    /* 샌즈 말풍선(한국어) — 흰 풍선 + 검은 글씨 */
    if (gBubbleLen > 0) {
        int n = (int)gBubbleType; wchar_t bb[256];
        if (n > gBubbleLen) n = gBubbleLen;
        wmemcpy(bb, gBubble, n); bb[n] = L'\0';
        fillRect(gMemDC, 384, 54, 234, 62, gWhite);
        drawTextWrapped(392, 60, 218, 50, bb, RGB(0, 0, 0), gFontSmall);
    }

    /* 전투 박스 (VM 공격 중엔 동적 gBox 사용) */
    {
        int bx = (gUseVM && gPhase == PH_ENEMY) ? gBox.x : BOX_X;
        int by = (gUseVM && gPhase == PH_ENEMY) ? gBox.y : BOX_Y;
        int bw = (gUseVM && gPhase == PH_ENEMY) ? gBox.w : BOX_W;
        int bh = (gUseVM && gPhase == PH_ENEMY) ? gBox.h : BOX_H;
        fillRect(gMemDC, bx - 3, by - 3, bw + 6, bh + 6, gWhite);
        fillRect(gMemDC, bx, by, bw, bh, gBlack);
    }

    if (gPhase == PH_DIALOGUE) {
        const wchar_t* line = gDlgQueue[gDlgIdx];
        int n = (int)gTypePos; int len = (int)wcslen(line);
        wchar_t buf[160];
        if (n > len) n = len;
        wmemcpy(buf, line, n); buf[n] = L'\0';
        drawTextWrapped(BOX_X + 12, BOX_Y + 14, BOX_W - 24, BOX_H - 30, buf, RGB(255, 255, 255), gFontSmall);
        if (n >= len) drawText(gMemDC, BOX_X + BOX_W - 70, BOX_Y + BOX_H - 24, L"[Z]", RGB(255, 255, 0), gFontTiny);
    } else if (gPhase == PH_FIGHT) {
        drawText(gMemDC, BOX_X + 12, BOX_Y + 16, L"* 공격!", RGB(255, 255, 255), gFontSmall);
        drawTextCentered(320 + (int)gSansOffsetX, SANS_Y + SANS_H + 4, L"빗나감!", RGB(255, 255, 0), gFontSmall);
    } else if (gPhase == PH_ENEMY) {
        if (gUseVM) {
            haz_render(gMemDC);   /* VM 탄막 렌더 */
        } else if (gTurn % 2 == 0) {
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
        int n = (int)gTypePos; int len = (int)wcslen(gMessage);
        wchar_t buf[160];
        if (n > len) n = len;
        wmemcpy(buf, gMessage, n); buf[n] = L'\0';
        drawTextWrapped(BOX_X + 12, BOX_Y + 14, BOX_W - 24, BOX_H - 30, buf, RGB(255, 255, 255), gFontSmall);
        if (n >= len) drawText(gMemDC, BOX_X + BOX_W - 70, BOX_Y + BOX_H - 24, L"[Z]", RGB(255, 255, 0), gFontTiny);
    } else { /* PH_MENU */
        drawText(gMemDC, BOX_X + 12, BOX_Y + 16, L"* 무엇을 할까...", RGB(255, 255, 255), gFontSmall);
        if (gHitAttempts >= MERCY_HA)
            drawText(gMemDC, BOX_X + 12, BOX_Y + 44, L"* (자비 가능!)", RGB(255, 255, 0), gFontTiny);
    }

    drawHpBar();
    drawMenuButton(&gSprFight, L"FIGHT", 0);
    drawMenuButton(&gSprAct,   L"ACT",   1);
    drawMenuButton(&gSprItem,  L"ITEM",  2);
    drawMenuButton(&gSprMercy, L"MERCY", 3);

    if (gBlackScreen) fillRect(gMemDC, 0, 0, CLIENT_W, CLIENT_H, gBlack);  /* 플래시 연출 */

    /* F1 디버그 오버레이: VM 상태(핑퐁 진단용) */
    if (gDebug) {
        char dbg[160]; HFONT of;
        wsprintfA(dbg, "VM:%s pc=%d t=%dms run=%d fin=%d bones=%d box=%d,%d,%d,%d mode=%d",
                  gUseVM ? gAtkName : "(off)", gVM.pc, (int)(gVM.t * 1000.0f),
                  gVM.running, gVM.finished, haz_active_count(),
                  gBox.x, gBox.y, gBox.w, gBox.h, gSoulMode);
        of = (HFONT)SelectObject(gMemDC, gFontTiny);
        SetBkMode(gMemDC, TRANSPARENT); SetTextColor(gMemDC, RGB(0, 255, 0));
        TextOutA(gMemDC, 4, 2, dbg, (int)strlen(dbg));
        SelectObject(gMemDC, of);
    }
}

/* ---------------- Win32 ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        else if (w == VK_F1) gDebug ^= 1;   /* 디버그 오버레이 토글 */
        return 0;
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

    /* 언더테일 한국어 패치 느낌의 픽셀 글꼴(네오둥근모)을 설치 없이 런타임 로드.
       실패하면 맑은 고딕으로 폴백. 픽셀 폰트는 안티에일리어싱을 꺼 또렷하게. */
    {
        const char* face; DWORD qual;
        gPixelFontOk = (AddFontResourceExA(assetPath("neodgm.ttf"), FR_PRIVATE, NULL) > 0);
        face = gPixelFontOk ? "NeoDunggeunmo" : "Malgun Gothic";
        qual = gPixelFontOk ? NONANTIALIASED_QUALITY : DEFAULT_QUALITY;
        gFontBig   = CreateFontA(48, 0, 0, 0, FW_NORMAL, 0, 0, 0, HANGEUL_CHARSET, 0, 0, qual, 0, face);
        gFontSmall = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, HANGEUL_CHARSET, 0, 0, qual, 0, face);
        gFontTiny  = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, HANGEUL_CHARSET, 0, 0, qual, 0, face);
    }

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
    if (gKarmaBr) DeleteObject(gKarmaBr);
    DeleteObject(gFontBig); DeleteObject(gFontSmall); DeleteObject(gFontTiny);
    haz_free();   /* hazards.c 브러시 해제 */
    if (gPixelFontOk) RemoveFontResourceExA(assetPath("neodgm.ttf"), FR_PRIVATE, NULL);
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
    gHost.on_command = haz_on_command;     /* VM → 게임 명령 라우팅 */
    gHost.get_heart_pos = haz_get_heart_pos;
    gHost.ctx = NULL;
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
                    if (gShakeDx || gShakeDy) {   /* 흔들림: 대상 오프셋 + 빈 가장자리 검정(소스 범위초과 방지) */
                        RECT wr; wr.left = 0; wr.top = 0; wr.right = CLIENT_W; wr.bottom = CLIENT_H;
                        FillRect(dc, &wr, gBlack);
                        BitBlt(dc, gShakeDx, gShakeDy, CLIENT_W, CLIENT_H, gMemDC, 0, 0, SRCCOPY);
                    } else {
                        BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gMemDC, 0, 0, SRCCOPY);
                    }
                    ReleaseDC(gHwnd, dc);
                }
            }
        }
        Sleep(1);
    }

    timeEndPeriod(1);
    stopBGM();
    mciSendStringA("close all", NULL, 0, NULL);   /* 음성/효과음 MCI 디바이스 정리 */
    freeResources();
    return 0;
}
