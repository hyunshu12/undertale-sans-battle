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

#define BOX_X      33        /* BTS 기본 박스 33,251,608,391 (전폭 575). 공격이 CombatZoneResize로 변경 */
#define BOX_Y      251
#define BOX_W      575
#define BOX_H      140

#define SOUL_SIZE  16
#define MAX_BONES  48
#define MAX_BLASTERS 6
#define HIT_DAMAGE 6
#define MAX_HP     92
#define MERCY_TURNS 4          /* (구) 폴백 패턴용 */
#define MERCY_HA 8             /* 이 횟수 이상 공격 후(샌즈가 지침) MERCY로 승리 가능 */
#define ENEMY_DURATION 8.0f    /* 적턴 길이(초) */

/* 샌즈 전신 합성(BTS: 2x 네이티브, 부품 바닥앵커). 중심 x, 발끝 y. */
#define SANS_CX   320
#define SANS_FEET 224
/* 부품 표시 크기(px): 다리88x46 / 몸통108x50 / 머리64x60 */
#define SANS_LEGS_W 88
#define SANS_LEGS_H 46
#define SANS_TORSO_W 108
#define SANS_TORSO_H 50
#define SANS_HEAD_W 64
#define SANS_HEAD_H 60

/* 메뉴 버튼 배치 (BTS 정확 좌표: x 32/184/344/496, y432, 110x42) */
#define BTN_W 110
#define BTN_H 42
#define BTN_Y 432
static const int gMenuBtnX[4] = { 32, 184, 344, 496 };

/* ---------------- 구조체 ([구현조건: 구조체]) ---------------- */
typedef enum { ST_TITLE, ST_DIFFICULTY, ST_BATTLE, ST_GAMEOVER, ST_WIN } GameState;
typedef enum { PH_DIALOGUE, PH_ENEMY, PH_MENU, PH_ACTION, PH_FIGHTAIM, PH_FIGHT } Phase;

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
static HBRUSH   gBlack, gWhite, gRed, gYellow, gBlue, gDkRed, gCyan, gKarmaBr, gHpBg;
static HFONT    gFontBig, gFontSmall, gFontTiny;
static int      gPixelFontOk = 0;   /* 네오둥근모 픽셀 폰트 로드 성공 여부 */
static int      gRunning = 1;
static int      gFullscreen = 1;    /* 1=전체화면(테두리없는 풀스크린, 기본) */
static int      gDstX = 0, gDstY = 0, gDstW = CLIENT_W, gDstH = CLIENT_H; /* 레터박스 대상 사각형 */

static Sprite   gSprLegs, gSprTorso;                                    /* 샌즈 다리/몸통 */
static Sprite   gSprBodyDown, gSprBodyUp, gSprBodyLeft, gSprBodyRight, gSprSweat; /* 팔포즈/땀 */
static Sprite   gSprHeadDef, gSprHeadBlue, gSprHeadBlue1, gSprHeadNoEye, gSprHeadClosed, gSprHeadTired, gSprHeadTired2, gSprHeadWink, gSprHeadLook; /* 머리 표정 */
static Sprite   gSprStrike[6];           /* FIGHT 슬래시(6프레임) */
static Sprite   gSprHeart, gSprHeartBlue, gSprBlaster, gSprBlasterFire; /* gSprBlaster/Fire=Slice2 폴백 */
static Sprite   gSprFight, gSprAct, gSprItem, gSprMercy;
static Sprite   gSprFightHi, gSprActHi, gSprItemHi, gSprMercyHi;       /* 선택 시 노란 채움 버튼 */
static Sprite   gSprHeartSplit, gSprHeartShard;                        /* 죽음: 금간하트/파편 */
static Sprite   gSprBlasterD[8], gSprBlasterF[8];                       /* 8각 사전회전 게이스터(VM) */

static GameState gState = ST_TITLE;
static Phase     gPhase = PH_DIALOGUE;
static Soul      gSoul;
static Bone      gBones[MAX_BONES];        /* [구현조건: 배열] */
static Blaster   gBlasters[MAX_BLASTERS];  /* [구현조건: 배열] */
static int       gTurn = 0;
static int       gMenuIndex = 0;
static int       gItemsLeft = 3;
static int       gDifficulty = 1;   /* 0=EASY 1=NORMAL 2=HARD */
static int       gDiffSel = 1;      /* 난이도 선택 커서 */
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
static int    gHeartMoving = 0;          /* BTS "Is moving"(속도/입력 기반) */
static int    gDebug = 0;       /* F1 디버그 오버레이 */
static char   gAtkName[32] = "";
static int    gAtkIndex = 0;    /* 공격 시퀀스 인덱스 */
static double gMaxFall = 750.0; /* HeartMaxFallSpeed (BLUE 물리는 다음 푸시) */
static int    gSlamDamage = 0;  /* SansSlamDamage: 벽 충돌 시 1뎀(비치명) — sans_final */
static int    gSlammed = 0;     /* SansSlam으로 발사된 직후(첫 벽충돌에서 해제) */
static int    gBgmStarted = 0;  /* 메가로바니아 시작 여부(BTS: 2번째 공격에서 드랍) */
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
static float  gSndHurtT = 0.0f;         /* 피격음 스로틀(연속피격시 소리 도배 방지) */
static float  gKarmaStreak = 0.0f;      /* 연속피격 카르마 캡(BTS: 지속접촉시 카르마 2로 제한) */
/* 죽음 연출: 금간 하트 → 6조각 파편 (BTS) */
static float  gDeathT = 0.0f, gDeathX = 0.0f, gDeathY = 0.0f;
static int    gDeathShatter = 0;
static float  gShardX[6], gShardY[6], gShardVx[6], gShardVy[6];

/* --- 전투 흐름(HitAttempts) / 샌즈 회피 / 대사 큐 --- */
static int    gHitAttempts = 0;          /* FIGHT(샌즈 회피) 성공 횟수 → 공격 선택 */
static char   gCurAtk[32] = "";          /* 진행중 공격 이름(엔딩 트리거 판정) */
static float  gSansDodgeT = 0.0f;        /* 회피 애니 타이머 */
static float  gSansOffsetX = 0.0f;       /* 샌즈 머리 가로 오프셋(회피) */
static float  gFightAimX = 0.0f;         /* FIGHT 타겟바 위치 */
static int    gFightAimDir = 1;          /* 타겟바 방향 */
static int    gFightHit = 0;             /* 타겟존 명중 여부(중앙 정렬 성공) */
static float  gStrikeT = -1.0f;          /* 슬래시 애니 타이머(-1=없음) */
#define FIGHT_ZONE_HALF 26               /* 타겟존 반폭(중앙 ±26px) */
static const wchar_t* gDlgQueue[10];     /* 인트로/엔딩 공용 대사 큐 */
static int    gDlgN = 0, gDlgIdx = 0, gDlgAfter = 0;  /* after: 0=적턴, 1=승리 */

/* --- 샌즈 비주얼 상태(스크립트 구동) + 최적화 캐시 --- */
static int    gSansX = SANS_CX;          /* SansX 가로 위치 */
static char   gSansHead[16] = "Default"; /* 현재 머리 표정 */
static int    gSansBodyPose = 0;         /* 0=레이어(다리+몸통+머리), 1=Down 2=Up 3=Left 4=Right */
static int    gSansAnimMode = 0;         /* 0=Idle 1=HeadBob 2=Tired (호흡) */
static float  gSansAnimT = 0.0f;         /* 호흡 누적 시간 */
static int    gSansSweat = 0;            /* SansSweat 땀(0=숨김) */
static int    gGravDir = 1;              /* 파란 영혼 중력 방향(0우1하2좌3상). 기본=아래 */
static int    gHasFocus = 1;             /* 최적화: 포커스 상태 프레임당 1회 갱신 */
static HPEN   gMenuPen = NULL;           /* 메뉴 하이라이트 펜(1회 생성, 매프레임 X) */

/* 키 엣지 검출용 이전 상태 */
static int gPrevZ = 0, gPrevLeft = 0, gPrevRight = 0;

/* 인트로 독백 (사용자 요청으로 복원) — 언더테일 샌즈 오프닝 분위기 */
static const wchar_t* gDialogues[] = {
    L"* 오늘은 밖이 참 아름다운 날이지.",
    L"* 새들은 지저귀고, 꽃들은 피어나고...",
    L"* 이런 날엔 말이야... 너 같은 녀석들은,",
    L"* 지옥에서 불타고 있어야 하는 거야.",
    L"* ...자, 그럼.",
    L"* 각오는 됐겠지?"
};
#define DIALOGUE_COUNT 6

/* 공격 순서 (BTS Battle.xml 원본 그대로). HitAttempts(FIGHT 횟수)로 페이즈 결정.
   인트로(HA0-12) → 스페어(HA13) → 멀티(HA14-22) → 특수공격(HA23+). */
static const char* gIntroTable[] = {   /* HA 0..12 (BTS NextAttack 0..12) */
    "sans_intro", "sans_bonegap1", "sans_bluebone", "sans_bonegap2",
    "sans_platforms1", "sans_platforms2", "sans_platforms3", "sans_platforms4",
    "sans_platformblaster", "sans_platforms4hard", "sans_bonegap1fast",
    "sans_boneslideh", "sans_bonegap2"
};
#define INTRO_N 13   /* HA 0..12 */
static const char* gMultiTable[] = {   /* HA 14..22 */
    "sans_multi1", "sans_randomblaster1", "sans_multi2", "sans_bonestab1",
    "sans_bonestab2", "sans_randomblaster2", "sans_boneslidev", "sans_multi3", "sans_bonestab3"
};
#define MULTI_N 9     /* HA 14..22 */

/* BTS 시작 인벤토리(8칸): 버터스카치99/라면90/스테이크60/전설의영웅40×5. 순서대로 소비. */
#define INVENTORY_N 8
static const struct { int heal; const wchar_t* name; } gInventory[INVENTORY_N] = {
    { 99, L"버터스카치 파이" }, { 90, L"인스턴트 라면" }, { 60, L"페이스 스테이크" },
    { 40, L"전설의 영웅" }, { 40, L"전설의 영웅" }, { 40, L"전설의 영웅" },
    { 40, L"전설의 영웅" }, { 40, L"전설의 영웅" }
};

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
    if (!gHasFocus) return 0;   /* 창 비활성 시 무시. GetForegroundWindow는 update()에서 1회만(최적화) */
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
static float frand(float a, float b) { return a + (b - a) * ((float)rand() / (float)RAND_MAX); } /* [구현조건: 랜덤함수] */

/* 난이도 스케일: NORMAL=BTS 웹 원본과 동일, EASY=채점/접근성, HARD=웹+조기자비 없음 */
static float diffInvuln(void)  { return gDifficulty == 0 ? 0.4f : 0.034f; }     /* NORMAL/HARD=BTS 1프레임 */
static int   diffKarma(int k)  { return gDifficulty == 0 ? (k + 1) / 2 : k; }   /* EASY만 카르마 절반 */
static int   diffItems(void)   { return INVENTORY_N; }   /* BTS: 모든 난이도 8개(인벤토리 고정) */
static int   diffMercyHA(void) { return gDifficulty == 0 ? 5 : 99; }  /* BTS=조기 자비승 없음. EASY만 HA5 접근성 */

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
    SIZE sz; int len = (int)wcslen(s);   /* 최적화: wcslen 1회 */
    HFONT old = (HFONT)SelectObject(gMemDC, f);
    GetTextExtentPoint32W(gMemDC, s, len, &sz);
    SetBkMode(gMemDC, TRANSPARENT); SetTextColor(gMemDC, col);
    TextOutW(gMemDC, cx - sz.cx / 2, y, s, len);
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
/* 마젠타(255,0,255) 투명키 블릿 — 샌즈 부품/블래스터(검은 외곽선 보존) */
static void drawSpriteMag(Sprite* s, int x, int y) {
    if (!s->ok) return;
    TransparentBlt(gMemDC, x, y, s->w, s->h, s->dc, 0, 0, s->w, s->h, RGB(255, 0, 255));
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
/* BTS HitAttempts 페이즈: 0-12=인트로, 13=스페어, 14-22=멀티(bonestab3까지), 23+=특수공격 */
static const char* chooseAttack(void) {
    int ha = gHitAttempts;
    if (ha < INTRO_N) return gIntroTable[ha];        /* HA0-12 */
    if (ha == 13)     return "sans_spare";           /* HA13: 샌즈가 쉬는 턴 */
    if (ha <= 22)     return gMultiTable[ha - 14];   /* HA14-22: 멀티 9개 */
    return "sans_final";                             /* HA23+: 특수공격(엔딩) */
}
/* 메뉴 박스 정보 텍스트(BTS InfoText). HitAttempts·KARMA 기반 플레이버. */
static const wchar_t* menuInfoText(void) {
    int ha = gHitAttempts;   /* BTS InfoText: 정확한 HitAttempts 값에만 특수문구, 나머진 기본(죄가 기어오른다) */
    if (ha >= 22) return L"* 샌즈가 특수 공격을 쓸 준비를 한다.";
    if (ha == 21) return L"* 샌즈가 뭔가 준비하고 있다.";
    if (ha == 20) return L"* 샌즈가 정말 지쳐 보이기 시작한다.";
    if (ha == 19) return L"* 이걸 읽는 게 시간을 잘 쓰는 것 같진 않다.";
    if (ha >= 15) return L"* 진짜 전투가 마침내 시작된다.";
    if (ha == 13) return L"* 샌즈가 잠시 쉬고 있다.";
    if (ha <= 1)  return L"* 안 좋은 일이 생길 것 같은 기분이 든다.";
    return L"* 등 뒤로 죄가 기어오르는 게 느껴진다.";   /* BTS 기본 플레이버(제노사이드) */
}
/* 엔딩 시퀀스: 탄막 정리 후 마무리 대사 → 승리 */
static void startEnding(int viaFinal) {
    static const wchar_t* fin[] = {      /* BTS: "huff... puff..." → "alright, i guess you win." */
        L"* 헉... 헉...",
        L"* 좋아, 인정하지... 네가 이겼어."
    };
    static const wchar_t* spare[] = {
        L"* 흠...",
        L"* 좋아. 오늘은 이쯤에서 봐주지."
    };
    clearHazards(); haz_reset();
    gSoulMode = 0; gBlackScreen = 0; gBubbleLen = 0; gSansOffsetX = 0.0f;
    gSansX = SANS_CX; lstrcpyA(gSansHead, "Tired1");   /* 지친 표정으로 마무리 */
    gKR = 0; gKR_t = 0.0f;   /* 승리 확정 — 잔여 KARMA로 엔딩 중 사망하지 않게 */
    startDialogue(viaFinal ? fin : spare, 2, 1);
}

static void startBattle(void) {
    gSoul.maxHp = MAX_HP; gSoul.hp = MAX_HP; gSoul.invuln = 0.0f;
    gKR = 0; gKR_t = 0.0f; gAtkIndex = 0; gHitAttempts = 0;
    gTurn = 0; gMenuIndex = 0; gItemsLeft = diffItems();
    gCurAtk[0] = 0; gSansOffsetX = 0.0f; gBubbleLen = 0; gBlackScreen = 0;
    gSansX = SANS_CX; lstrcpyA(gSansHead, "Default");
    clearHazards(); centerSoul();
    gState = ST_BATTLE;
    gBgmStarted = 1;     /* 인트로 독백부터 BGM 재생(아래 playBGM) */
    playBGM();
    startDialogue(gDialogues, DIALOGUE_COUNT, 0);   /* 인트로 독백 → 끝나면 첫 공격 */
}
static void startTurn(void) {
    gPhase = PH_DIALOGUE;
    gTypePos = 0.0f;
}
static void startEnemyPhase(void) {
    gPhase = PH_ENEMY;
    if (gHitAttempts >= 1 && !gBgmStarted) { playBGM(); gBgmStarted = 1; }  /* BTS: 2번째 공격에서 메가로바니아 드랍 */
    clearHazards(); centerSoul();
    gSpawnTimer = 0.0f; gBlasterTimer = 0.6f;
    gEnemyTime = ENEMY_DURATION;
    gBox.x = BOX_X; gBox.y = BOX_Y; gBox.w = BOX_W; gBox.h = BOX_H;  /* 박스 기본값 리셋 */
    gAttackEnded = 0;
    gSoulMode = 0; gVx = 0.0f; gVy = 0.0f; gPrevUp = 0; gGravDir = 1;  /* 영혼 물리 리셋 */
    gSlammed = 0; gSlamDamage = 0;   /* 슬램 상태 리셋(공격 간 누수 방지) */
    gSansX = SANS_CX; gSansOffsetX = 0.0f; lstrcpyA(gSansHead, "Default"); /* 샌즈 비주얼 리셋 */
    gSansBodyPose = 0; gSansAnimMode = 0; gSansSweat = 0;
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

/* 죽음 연출 시작: 영혼이 죽은 자리에서 금간 하트 → 파편 (BTS) */
static void startDeath(void) {
    gSoul.hp = 0; gKR = 0;
    gDeathX = gSoul.x + SOUL_SIZE / 2.0f; gDeathY = gSoul.y + SOUL_SIZE / 2.0f;
    gDeathT = 0.0f; gDeathShatter = 0;
    gState = ST_GAMEOVER; stopBGM();
    game_play_sound("HeartSplit");   /* BTS: 하트 금가는 소리 */
}
static void hurtSoul(void) {
    if (gSoul.invuln > 0.0f) return;
    gSoul.hp -= HIT_DAMAGE;
    gSoul.invuln = 1.0f;
    if (gSoul.hp <= 0) startDeath();
}

/* --- hazards/VM 가 호출하는 game 후크 (game.h 선언) --- */
void game_get_heart(double* cx, double* cy) { *cx = gSoul.x + SOUL_SIZE / 2.0; *cy = gSoul.y + SOUL_SIZE / 2.0; }
int  game_heart_moving(void) {
    /* BTS CustomMovement "Is moving" = 속도≠0(입력 의도) — 위치변화 아님.
       → 벽에 붙어 눌러도 '이동'(파란뼈 피격). updateEnemyPhase에서 매프레임 계산. */
    return gHeartMoving;
}
void game_hurt(int dmg, int karma) {
    if (gSoul.invuln > 0.0f) return;
    gSoul.hp -= dmg;
    if (gSndHurtT <= 0.0f) { game_play_sound("PlayerDamaged"); gSndHurtT = 0.15f; }  /* 소리 스로틀 */
    if (gKarmaStreak > 0.0f && karma > 2) karma = 2;   /* BTS: 지속접촉 카르마 캡 2 */
    gKarmaStreak = 0.12f;
    gKR += diffKarma(karma); if (gKR > 40) gKR = 40;
    if (gKR >= gSoul.hp) gKR = gSoul.hp > 1 ? gSoul.hp - 1 : 0;   /* 즉사 방지 */
    gSoul.invuln = diffInvuln();   /* 난이도별 무적시간(HARD=BTS 0.034s) */
    if (gSoul.hp <= 0) startDeath();
}
void game_set_heart_mode(int blue) { gSoulMode = blue; if (blue) gGravDir = 1; }  /* 파랑=기본 아래중력 */
void game_teleport_heart(double x, double y) {
    gSoul.x = (float)(x - SOUL_SIZE / 2.0); gSoul.y = (float)(y - SOUL_SIZE / 2.0);
}
void game_end_attack(void) { gAttackEnded = 1; }
void game_set_max_fall(double v) { gMaxFall = v; }
double game_get_max_fall(void) { return gMaxFall; }
/* 샌즈 비주얼 상태 후크 */
void game_sans_head(const char* state) { lstrcpynA(gSansHead, (state && state[0]) ? state : "Default", 15); }
void game_sans_body(const char* pose) {   /* 팔 포즈: 다리/몸통 숨기고 팔 스프라이트 */
    if (!pose || !pose[0])                   gSansBodyPose = 0;
    else if (strcmp(pose, "HandDown") == 0)  gSansBodyPose = 1;
    else if (strcmp(pose, "HandUp") == 0)    gSansBodyPose = 2;
    else if (strcmp(pose, "HandLeft") == 0)  gSansBodyPose = 3;
    else if (strcmp(pose, "HandRight") == 0) gSansBodyPose = 4;
    else                                     gSansBodyPose = 1;
}
void game_sans_animation(const char* name) {   /* 호흡 모드(레이어 복귀) */
    gSansBodyPose = 0;
    if (name && strcmp(name, "HeadBob") == 0)    gSansAnimMode = 1;
    else if (name && strcmp(name, "Tired") == 0) gSansAnimMode = 2;
    else                                         gSansAnimMode = 0;
}
void game_sans_sweat(int n) { gSansSweat = n; }
void game_sans_x(int x) { gSansX = x; }
/* SansSlam: 영혼을 dir 방향(0우1하2좌3상)으로 MaxFallSpeed 속도로 벽까지 내리꽂기(탄도) */
void game_sans_slam(int dir) {
    int d = dir & 3;
    double a = (double)(d * 90) * M_PI / 180.0;
    double spd = gMaxFall;   /* BTS: 부호 있는 MaxFallSpeed 그대로 */
    gSoulMode = 1; gGravDir = d;   /* 중력이 슬램 방향을 따름(방향중력) */
    gVx = (float)(cos(a) * spd);
    gVy = (float)(sin(a) * spd);
    gPrevUp = 1;   /* 슬램 직후 점프 엣지 무시 */
    gSlammed = 1;  /* BTS: Slammed=1 → 첫 벽충돌에서 효과음/흔들림(+SlamDamage 시 1뎀) */
    game_shake(5.0);
}
void game_slam_damage(int on) { gSlamDamage = (on != 0); }   /* BTS SansSlamDamage */
/* 슬램된 영혼이 벽에 충돌한 순간(첫 1회). speed=충돌 직전 중력방향 속도. */
static void slamImpact(float speed) {
    float s;
    if (!gSlammed) return;
    gSlammed = 0;
    s = speed < 0 ? -speed : speed;
    if (s > 330.0f) {   /* BTS: |속도|>330 강충돌 → Slam음 + 흔들림(floor(s/30/3)) */
        game_play_sound("Slam");
        game_shake((double)((int)(s / 30.0f / 3.0f)));
    }
    if (gSlamDamage && gSoul.hp > 1) {   /* BTS: SlamDamage 켜짐 & HP>1 → 1뎀(비치명) */
        gSoul.hp -= 1;
        game_play_sound("PlayerDamaged");
    }
}
/* 회전 게이스터 블래스터 스프라이트(8각 사전회전 중 최근접). firing=발사중 */
void game_draw_blaster(HDC dc, double cx, double cy, double ang, int size, int firing) {
    double na = ang - 360.0 * floor(ang / 360.0);   /* [0,360) 정규화(음수/초과 각 대비) */
    int idx = ((int)((na + 22.5) / 45.0)) & 7;
    Sprite* s = firing ? &gSprBlasterF[idx] : &gSprBlasterD[idx];
    if (!s->ok) {   /* 폴백: 흰 사각 */
        int hs = size == 2 ? 40 : 28; RECT r;
        r.left = (int)cx - hs / 2; r.top = (int)cy - hs / 2;
        r.right = (int)cx + hs / 2; r.bottom = (int)cy + hs / 2;
        FillRect(dc, &r, gWhite); return;
    }
    {
        double sc = (size == 0 ? 0.5 : size == 1 ? 1.0 : 1.5);   /* BTS strip Scale */
        int w = (int)(s->w * sc), h = (int)(s->h * sc);
        TransparentBlt(dc, (int)cx - w / 2, (int)cy - h / 2, w, h, s->dc, 0, 0, s->w, s->h, RGB(255, 0, 255));
    }
}
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
    /* BTS: 기본 150px/s, Shift(집중) 누르면 75px/s 정밀이동 */
    float speed = (keyDown(VK_SHIFT) || keyDown(VK_LCONTROL)) ? 75.0f : 150.0f;
    int i;

    /* 영혼 이동 [구현조건: 키보드입력] */
    if (keyDown(VK_LEFT)  || keyDown('A')) dx -= 1.0f;
    if (keyDown(VK_RIGHT) || keyDown('D')) dx += 1.0f;
    if (keyDown(VK_UP)    || keyDown('W')) dy -= 1.0f;
    if (keyDown(VK_DOWN)  || keyDown('S')) dy += 1.0f;
    if (gSoul.invuln > 0.0f) gSoul.invuln -= dt;

    if (gUseVM) {
        if (gSoulMode == 1 && gGravDir == 1) {
            /* === 파란 영혼: 아래 중력+점프 (플랫폼 포함). dir1=down. (down은 기존 그대로) === */
            float floorY = (float)(gBox.y + gBox.h - 2 - SOUL_SIZE);
            int   up = keyDown(VK_UP) || keyDown('W');
            int   grounded;
            double ptopY = 0;
            float lat = 0.0f, g;
            if (keyDown(VK_LEFT)  || keyDown('A')) lat -= 1.0f;
            if (keyDown(VK_RIGHT) || keyDown('D')) lat += 1.0f;
            gVx = lat * speed;   /* 파랑 횡이동도 집중 시 75 */
            grounded = (gSoul.y >= floorY - 1.0f) ||
                       (gVy >= 0 && haz_is_solid(gSoul.x, gSoul.y + SOUL_SIZE, SOUL_SIZE, 3, &ptopY) &&
                        gSoul.y + SOUL_SIZE <= ptopY + 8);
            if (!grounded) {                                  /* 가변 중력 밴드 */
                if (gVy > 15.0f) g = 540.0f;        /* BTS: 15<v 본낙하 강중력(≥240도 540 유지) */
                else if (gVy > -30.0f) g = 180.0f;  /* -30<v≤15 정점 부근 약중력 */
                else if (gVy > -120.0f) g = 450.0f; /* -120<v≤-30 상승 강감속 */
                else g = 180.0f;                    /* v≤-120 */
                gVy += g * dt;
            }
            if (gMaxFall >= 0) { if (gVy > (float)gMaxFall) gVy = (float)gMaxFall; }  /* BTS: 단방향 부호 클램프 */
            else               { if (gVy < (float)gMaxFall) gVy = (float)gMaxFall; }  /* 음수=역중력 드리프트 */
            if (up && !gPrevUp && grounded) gVy = -180.0f;    /* 점프 임펄스 */
            if (!up && gVy < -30.0f) gVy = -30.0f;            /* 가변 점프컷(짧은 점프) */
            gPrevUp = up;
            gSoul.x += gVx * dt;
            gSoul.y += gVy * dt;
            if (gSoul.x < gBox.x + 2) gSoul.x = (float)(gBox.x + 2);
            if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
            if (gVy >= 0 && haz_is_solid(gSoul.x, gSoul.y + SOUL_SIZE, SOUL_SIZE, 4, &ptopY) &&
                gSoul.y + SOUL_SIZE <= ptopY + 10) { gSoul.y = (float)(ptopY - SOUL_SIZE); gVy = 0.0f; }
            if (gSoul.y >= floorY) { slamImpact(gVy); gSoul.y = floorY; gVy = 0.0f; }
            if (gSoul.y < gBox.y + 2) { gSoul.y = (float)(gBox.y + 2); if (gVy < 0.0f) gVy = 0.0f; }
            if (gVy == 0.0f) {   /* 이동 플랫폼 위에 서면 함께 끌려감(착지 물리) */
                double pvx = haz_platform_vx(gSoul.x, gSoul.y + SOUL_SIZE, SOUL_SIZE);
                if (pvx != 0.0) {
                    gSoul.x += (float)(pvx * dt);
                    if (gSoul.x < gBox.x + 2) gSoul.x = (float)(gBox.x + 2);
                    if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
                }
            }
        } else if (gSoulMode == 1) {
            /* === 파란 영혼: 방향 중력(dir 0=우/2=좌/3=상). SansSlam 다방향. 플랫폼 없음 === */
            int gd = gGravDir;
            float gvx = (gd == 0 ? 1.0f : gd == 2 ? -1.0f : 0.0f);   /* 중력 단위(벽 방향) */
            float gvy = (gd == 1 ? 1.0f : gd == 3 ? -1.0f : 0.0f);
            float lvx = gvy, lvy = -gvx;   /* 횡(중력 수직) 단위 */
            int kL = keyDown(VK_LEFT) || keyDown('A');
            int kR = keyDown(VK_RIGHT) || keyDown('D');
            int kU = keyDown(VK_UP) || keyDown('W');
            int kD = keyDown(VK_DOWN) || keyDown('S');
            float ix = (kR ? 1.0f : 0.0f) - (kL ? 1.0f : 0.0f);
            float iy = (kD ? 1.0f : 0.0f) - (kU ? 1.0f : 0.0f);
            float lat = ix * lvx + iy * lvy;               /* 횡 입력 */
            int jumpKey = (-(ix * gvx + iy * gvy) > 0.5f); /* 중력 반대로 누름 = 점프 */
            float fall = gVx * gvx + gVy * gvy;            /* 중력방향 속도 */
            float latV = lat * speed;
            float leadX, leadY, soulLead, wallX, wallY, wallPos, g, lv;
            int grounded;
            leadX = gSoul.x + (gvx > 0 ? SOUL_SIZE : 0.0f);
            leadY = gSoul.y + (gvy > 0 ? SOUL_SIZE : 0.0f);
            soulLead = leadX * gvx + leadY * gvy;
            wallX = (float)(gvx > 0 ? gBox.x + gBox.w - 2 : gvx < 0 ? gBox.x + 2 : 0);
            wallY = (float)(gvy > 0 ? gBox.y + gBox.h - 2 : gvy < 0 ? gBox.y + 2 : 0);
            wallPos = wallX * gvx + wallY * gvy;
            grounded = (soulLead >= wallPos - 1.0f);
            if (!grounded) {                                /* 가변 중력 밴드(아래와 동일) */
                if (fall > 15.0f) g = 540.0f;        /* BTS: 15<v 본낙하 강중력(≥240도 540 유지) */
                else if (fall > -30.0f) g = 180.0f;  /* -30<v≤15 정점 부근 약중력 */
                else if (fall > -120.0f) g = 450.0f; /* -120<v≤-30 상승 강감속 */
                else g = 180.0f;                     /* v≤-120 */
                fall += g * dt;
            }
            if (gMaxFall >= 0) { if (fall > (float)gMaxFall) fall = (float)gMaxFall; }  /* BTS 단방향 부호 클램프 */
            else               { if (fall < (float)gMaxFall) fall = (float)gMaxFall; }
            if (jumpKey && !gPrevUp && grounded) fall = -180.0f;
            if (!jumpKey && fall < -30.0f) fall = -30.0f;
            gPrevUp = jumpKey;
            gVx = latV * lvx + fall * gvx;
            gVy = latV * lvy + fall * gvy;
            gSoul.x += gVx * dt; gSoul.y += gVy * dt;
            if (gSoul.x < gBox.x + 2) gSoul.x = (float)(gBox.x + 2);
            if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
            if (gSoul.y < gBox.y + 2) gSoul.y = (float)(gBox.y + 2);
            if (gSoul.y > gBox.y + gBox.h - 2 - SOUL_SIZE) gSoul.y = (float)(gBox.y + gBox.h - 2 - SOUL_SIZE);
            leadX = gSoul.x + (gvx > 0 ? SOUL_SIZE : 0.0f);   /* 벽 도달 시 중력속도 0 */
            leadY = gSoul.y + (gvy > 0 ? SOUL_SIZE : 0.0f);
            soulLead = leadX * gvx + leadY * gvy;
            if (soulLead >= wallPos - 0.5f) { slamImpact(fall); lv = gVx * lvx + gVy * lvy; gVx = lv * lvx; gVy = lv * lvy; }
        } else {
            /* === 빨간 영혼: 자유 8방향 === */
            gSoul.x += dx * speed * dt;
            gSoul.y += dy * speed * dt;
            if (gSoul.x < gBox.x + 2)                      gSoul.x = (float)(gBox.x + 2);
            if (gSoul.y < gBox.y + 2)                      gSoul.y = (float)(gBox.y + 2);
            if (gSoul.x > gBox.x + gBox.w - 2 - SOUL_SIZE) gSoul.x = (float)(gBox.x + gBox.w - 2 - SOUL_SIZE);
            if (gSoul.y > gBox.y + gBox.h - 2 - SOUL_SIZE) gSoul.y = (float)(gBox.y + gBox.h - 2 - SOUL_SIZE);
        }
        /* BTS "Is moving"(속도≠0): RED=입력의도, BLUE=속도(벽에 눌러도 이동·낙하도 이동) */
        gHeartMoving = (gSoulMode == 0) ? (dx != 0.0f || dy != 0.0f)
                       : (gVx > 0.5f || gVx < -0.5f || gVy > 0.5f || gVy < -0.5f);
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
    case 0: /* FIGHT → 타겟바(BTS: 랜덤 끝에서 출발, 단방향) → Z 또는 끝 도달 → 슬래시 → 회피 */
        gPhase = PH_FIGHTAIM;
        if (rand() & 1) { gFightAimX = (float)(BOX_X + 6); gFightAimDir = 1; }                 /* 좌→우 */
        else            { gFightAimX = (float)(BOX_X + BOX_W - 6); gFightAimDir = -1; }         /* 우→좌 */
        return;
    case 1: /* ACT(Check) — BTS: HA>4 이후 2페이지로 전환 */
        if (gHitAttempts > 4)
            lstrcpyW(gMessage, L"* 샌즈 - 공격 1 방어 1\n* 영원히 피할 순 없어.  계속 공격해.");
        else
            lstrcpyW(gMessage, L"* 샌즈 - 공격 1 방어 1\n* 가장 쉬운 적이다.  1의 데미지밖에 못 준다.");
        break;
    case 2: /* ITEM — BTS 인벤토리(버터스카치99/라면90/스테이크60/전설의영웅40×5) 순서대로 */
        if (gItemsLeft > 0) {
            int slot = INVENTORY_N - gItemsLeft;
            int heal = gInventory[slot].heal;
            gItemsLeft--;
            gSoul.hp += heal; if (gSoul.hp > gSoul.maxHp) gSoul.hp = gSoul.maxHp;
            game_play_sound("PlayerHeal");   /* BTS: 회복 소리 */
            wsprintfW(gMessage, L"* %s을(를) 먹었다.\n* 체력을 %d 회복했다!", gInventory[slot].name, heal);
        } else {
            lstrcpyW(gMessage, L"* 아이템이 다 떨어졌다.");
        }
        break;
    default: /* MERCY */
        if (gHitAttempts >= diffMercyHA()) { startEnding(0); return; }   /* 샌즈가 지침 → 자비 승리 */
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
    int z, l, r, zPressed, lPressed, rPressed;
    gHasFocus = (GetForegroundWindow() == gHwnd);   /* 최적화: 프레임당 1회만(키입력 12+회 호출 절감) */
    z = keyDown('Z') || keyDown(VK_RETURN);
    l = keyDown(VK_LEFT) || keyDown('A');
    r = keyDown(VK_RIGHT) || keyDown('D');
    zPressed = z && !gPrevZ;
    lPressed = l && !gPrevLeft;
    rPressed = r && !gPrevRight;
    gPrevZ = z; gPrevLeft = l; gPrevRight = r;

    gMenacePulse += dt;
    gSansAnimT += dt; if (gSansAnimT > 1000.0f) gSansAnimT = 0.0f;   /* 호흡 누적 */
    if (gSndHurtT > 0.0f) gSndHurtT -= dt;
    if (gKarmaStreak > 0.0f) gKarmaStreak -= dt;

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
        if (zPressed) { gState = ST_DIFFICULTY; gDiffSel = 1; }   /* 난이도 선택으로 */
        break;
    case ST_DIFFICULTY:
        if (lPressed && gDiffSel > 0) gDiffSel--;
        if (rPressed && gDiffSel < 2) gDiffSel++;
        if (zPressed) { gDifficulty = gDiffSel; startBattle(); }   /* 확정 → 전투 (ESC는 전역 종료) */
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
                if (gSoul.hp <= 0) startDeath();
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
                        if (gDlgAfter == 1) { gState = ST_TITLE; stopBGM(); }  /* BTS: 승리 2줄 후 곧바로 메인메뉴(타이틀)로 */
                        else startEnemyPhase();
                    } else { gTypePos = 0.0f; }
                }
            }
        } else if (gPhase == PH_ENEMY) {
            updateEnemyPhase(dt);
        } else if (gPhase == PH_MENU) {
            updateMenuPhase(lPressed, rPressed, zPressed);
        } else if (gPhase == PH_FIGHTAIM) {
            /* 타겟바 단방향 360px/s. Z 또는 반대 끝 도달 시 정지 → 중앙 타겟존이면 명중 */
            int stop = zPressed;
            gFightAimX += gFightAimDir * 360.0f * dt;
            if (gFightAimDir > 0 && gFightAimX >= BOX_X + BOX_W - 6) { gFightAimX = (float)(BOX_X + BOX_W - 6); stop = 1; }
            if (gFightAimDir < 0 && gFightAimX <= BOX_X + 6)         { gFightAimX = (float)(BOX_X + 6); stop = 1; }
            if (stop) {
                float barC = gFightAimX + 3.0f, zc = (float)(BOX_X + BOX_W / 2);   /* 바 중심 vs 타겟존 중심 */
                gFightHit = (barC >= zc - FIGHT_ZONE_HALF && barC <= zc + FIGHT_ZONE_HALF);
                gPhase = PH_FIGHT; gSansDodgeT = 0.0f; gSansOffsetX = 0.0f; gStrikeT = 0.0f;
                game_play_sound(gFightHit ? "PlayerFight" : "Slam");   /* 명중=공격음, 빗나감=헛스윙 */
                if (gFightHit) game_shake(4.0);   /* 명중 시 샌즈 흠칫 */
            }
        } else if (gPhase == PH_FIGHT) {
            if (gStrikeT >= 0.0f) gStrikeT += dt;
            gSansDodgeT += dt;
            if (gFightHit) {   /* 명중: 샌즈 못 피함 → 회피 없이 짧게 표시 후 다음 턴 */
                gSansOffsetX = 0.0f;
                if (gSansDodgeT > 0.8f) { gHitAttempts++; startEnemyPhase(); }
            } else {           /* 빗나감: 샌즈 회피 4단계 → 1.5s 복귀 → HA++ */
                if (gSansDodgeT < 0.4f)      gSansOffsetX = -100.0f * (gSansDodgeT / 0.4f);
                else if (gSansDodgeT < 1.1f) gSansOffsetX = -100.0f;
                else if (gSansDodgeT < 1.5f) gSansOffsetX = -100.0f * (1.0f - (gSansDodgeT - 1.1f) / 0.4f);
                else { gSansOffsetX = 0.0f; gHitAttempts++; startEnemyPhase(); }
            }
        } else { /* PH_ACTION (ACT/ITEM/MERCY 결과) → BTS: 모든 행동 후 샌즈가 공격 */
            int len = (int)wcslen(gMessage);
            gTypePos += dt * 32.0f;
            if ((int)gTypePos < len) { gVoiceTimer -= dt; if (gVoiceTimer <= 0.0f) { playVoice(); gVoiceTimer = 0.09f; } }
            if (zPressed) {
                if (gTypePos < len) gTypePos = (float)len;
                else { gHitAttempts++; startEnemyPhase(); }   /* BTS: 행동 후 적 턴(공격) */
            }
        }
        break;
    case ST_GAMEOVER:
        gDeathT += dt;
        if (!gDeathShatter && gDeathT >= 0.7f) {   /* 금간 하트 → 산산조각 */
            int si;
            gDeathShatter = 1;
            game_play_sound("HeartShatter");   /* BTS: 하트 산산조각 소리 */
            for (si = 0; si < 6; si++) {
                float a = (float)((30 + si * 60) * 3.14159265 / 180.0);
                gShardX[si] = gDeathX; gShardY[si] = gDeathY;
                gShardVx[si] = (float)cos(a) * 150.0f;
                gShardVy[si] = (float)sin(a) * 150.0f - 120.0f;   /* 살짝 위로 튄 뒤 낙하 */
            }
        }
        if (gDeathShatter) {
            int si;
            for (si = 0; si < 6; si++) { gShardVy[si] += 360.0f * dt; gShardX[si] += gShardVx[si] * dt; gShardY[si] += gShardVy[si] * dt; }
        }
        if (gDeathT > 2.2f && zPressed) gState = ST_TITLE;
        break;
    case ST_WIN:
        if (zPressed) { gState = ST_TITLE; }
        break;
    }
}

/* ---------------- 렌더 ---------------- */
/* 현재 표정 문자열 → 머리 스프라이트 */
static Sprite* sansHeadSprite(void) {
    if (strcmp(gSansHead, "BlueEye") == 0)    /* 파란눈 깜빡임(2프레임 교대) */
        return (((int)(gMenacePulse * 10.0f) & 1) && gSprHeadBlue1.ok) ? &gSprHeadBlue1 : &gSprHeadBlue;
    if (strcmp(gSansHead, "NoEyes") == 0)     return &gSprHeadNoEye;
    if (strcmp(gSansHead, "ClosedEyes") == 0) return &gSprHeadClosed;
    if (strcmp(gSansHead, "Tired2") == 0)     return &gSprHeadTired2;
    if (strncmp(gSansHead, "Tired", 5) == 0)  return &gSprHeadTired;
    if (strcmp(gSansHead, "Wink") == 0)       return &gSprHeadWink;
    if (strcmp(gSansHead, "LookLeft") == 0)   return &gSprHeadLook;
    return &gSprHeadDef;   /* Default 등 */
}
/* 샌즈 전신 합성. BTS: 다리(발끝224)+몸통(top128)+머리(top80, 몸통과 12px 겹침).
   호흡(Idle/HeadBob/Tired)·팔 포즈(SansBody) 반영. */
static void drawSans(void) {
    int cx = gSansX + (int)gSansOffsetX;
    Sprite* head = sansHeadSprite();
    int tox = 0, toy = 0, hox = 0, hoy = 0;
    const double DEG = 3.14159265358979323846 / 180.0;
    if (gSansBodyPose == 0) {   /* 레이어 모드일 때만 호흡(±1px) */
        float T = gSansAnimT;
        if (gSansAnimMode == 1)      { hox = (int)sinf((float)(360.0*T/1.1*DEG)); hoy = (int)sinf((float)(720.0*T/1.1*DEG)); }
        else if (gSansAnimMode == 2) { toy = (int)sinf((float)(360.0*T/3.8*DEG)); hoy = toy; }
        else { tox = (int)sinf((float)(360.0*T/1.2*DEG)); toy = (int)sinf((float)(720.0*T/1.2*DEG)); hoy = -(int)(sinf((float)(720.0*T/1.2*DEG))*0.4f); }
    }
    if (gSansBodyPose != 0) {   /* 팔 포즈: 다리/몸통 숨기고 팔 스프라이트 + 머리 */
        Sprite* body = gSansBodyPose == 1 ? &gSprBodyDown : gSansBodyPose == 2 ? &gSprBodyUp :
                       gSansBodyPose == 3 ? &gSprBodyLeft : &gSprBodyRight;
        if (body->ok && head->ok) {
            int bx = (gSansBodyPose <= 2) ? cx - 60 : cx - 66;
            int by = (gSansBodyPose <= 2) ? SANS_FEET - 140 : SANS_FEET - 96;
            drawSpriteMag(body, bx, by);
            drawSpriteMag(head, cx - SANS_HEAD_W / 2, 140 - SANS_HEAD_H);   /* 머리 바닥 y140 */
            if (gSansSweat > 0) drawSpriteMag(&gSprSweat, cx - 32, 72);
            return;
        }
    }
    if (gSprLegs.ok && gSprTorso.ok && head->ok) {
        drawSpriteMag(&gSprLegs,  cx - 42, SANS_FEET - SANS_LEGS_H);                  /* (cx-42,178) */
        drawSpriteMag(&gSprTorso, cx - SANS_TORSO_W / 2 + tox, 178 - SANS_TORSO_H + toy); /* (cx-54,128) */
        drawSpriteMag(head,       cx - SANS_HEAD_W / 2 + hox, 140 - SANS_HEAD_H + hoy);   /* (cx-32, 80) */
        if (gSansSweat > 0) drawSpriteMag(&gSprSweat, cx - 32 + hox, 72 + hoy);
        return;
    }
    /* 폴백: GDI 해골+몸통 박스 */
    {
        int hx = cx - 40, hy = 70;
        int blue = (strcmp(gSansHead, "BlueEye") == 0);
        fillRect(gMemDC, cx - 30, 120, 60, 100, gWhite);                    /* 몸통 */
        fillRect(gMemDC, hx + 4, hy, 72, 56, gWhite);                        /* 머리 */
        fillRect(gMemDC, hx + 18, hy + 20, 12, 14, gBlack);
        fillRect(gMemDC, hx + 46, hy + 20, 12, 14, blue ? gCyan : gBlack);
        fillRect(gMemDC, hx + 20, hy + 44, 36, 5, gBlack);
    }
}
static void drawMenuButton(Sprite* s, Sprite* hi, const wchar_t* label, int idx) {
    int x = gMenuBtnX[idx];
    int sel = (gPhase == PH_MENU && gMenuIndex == idx);
    Sprite* spr = (sel && hi->ok) ? hi : s;   /* BTS: 선택 시 노란 채움 스프라이트로 교체 */
    if (spr->ok) drawSprite(spr, x, BTN_Y);
    else {   /* 폴백: 선택 시 노랑 */
        fillRect(gMemDC, x, BTN_Y, BTN_W, BTN_H, sel ? gYellow : gDkRed);
        drawText(gMemDC, x + 14, BTN_Y + 10, label, sel ? RGB(0, 0, 0) : RGB(255, 160, 0), gFontSmall);
    }
    if (sel) {   /* 하트 커서(버튼 왼쪽) */
        if (gSprHeart.ok) drawSprite(&gSprHeart, x - 22, BTN_Y + 13);
        else fillRect(gMemDC, x - 22, BTN_Y + 13, SOUL_SIZE, SOUL_SIZE, gRed);
    }
}
static void drawHpBar(void) {
    int hpx = 256, hpy = 400, hpw = 110, hph = 21;   /* BTS 정확 좌표/크기 */
    int cur = (int)(hpw * (gSoul.hp / (float)gSoul.maxHp));
    wchar_t buf[48];
    drawText(gMemDC, 32, hpy, L"CHARA   LV 19", RGB(255, 255, 255), gFontSmall);
    drawText(gMemDC, hpx - 30, hpy, L"HP", RGB(255, 255, 0), gFontSmall);   /* BTS: 바 왼쪽의 별도 HP 라벨(노랑) */
    fillRect(gMemDC, hpx, hpy, hpw, hph, gHpBg);
    fillRect(gMemDC, hpx, hpy, cur, hph, gYellow);
    if (gKR > 0) {   /* KARMA(마젠타, BTS KR #FF00FF) — 현재 HP 오른쪽 끝 일부가 깎일 예정 */
        int krw;
        if (!gKarmaBr) gKarmaBr = CreateSolidBrush(RGB(255, 0, 255));
        krw = (int)(hpw * (gKR / (float)gSoul.maxHp));
        if (krw > cur) krw = cur;
        fillRect(gMemDC, hpx + cur - krw, hpy, krw, hph, gKarmaBr);
    }
    wsprintfW(buf, L"%d / %d", gSoul.hp, gSoul.maxHp);   /* BTS: 바 오른쪽엔 숫자만(HP 라벨 분리) */
    drawText(gMemDC, hpx + hpw + 14, hpy, buf, gKR > 0 ? RGB(255, 0, 255) : RGB(255, 255, 255), gFontSmall);
}
static void drawSoul(void) {
    Sprite* h;
    int blink = (gSoul.invuln > 0.0f && ((int)(gSoul.invuln * 16.0f) % 2));
    if (blink) return;
    h = (gSoulMode == 1 && gSprHeartBlue.ok) ? &gSprHeartBlue : &gSprHeart;  /* 파란 영혼=파랑 */
    if (h->ok) drawSprite(h, (int)gSoul.x, (int)gSoul.y);
    else fillRect(gMemDC, (int)gSoul.x, (int)gSoul.y, SOUL_SIZE, SOUL_SIZE, gSoulMode == 1 ? gBlue : gRed);
}

static void render(void) {
    int i;
    fillRect(gMemDC, 0, 0, CLIENT_W, CLIENT_H, gBlack);

    if (gState == ST_TITLE) {
        drawTextCentered(CLIENT_W / 2, 118, L"UNDERTALE", RGB(255, 255, 255), gFontBig);
        drawTextCentered(CLIENT_W / 2, 232, L"* 샌즈와의 전투", RGB(255, 255, 255), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 286, L"Z 또는 Enter를 눌러 시작", RGB(255, 255, 0), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 324, L"이동: 방향키/WASD    메뉴: ← →    확인: Z", RGB(160, 160, 160), gFontTiny);
        drawTextCentered(CLIENT_W / 2, 348, L"F11: 창/전체화면    종료: ESC", RGB(160, 160, 160), gFontTiny);
        return;
    }
    if (gState == ST_DIFFICULTY) {
        static const wchar_t* names[3] = { L"EASY", L"NORMAL", L"HARD" };
        static const wchar_t* desc[3]  = {
            L"쉬움 — 채점·연습용. 넉넉한 무적시간·아이템 5개·빠른 자비",
            L"보통 — BTS 웹 원본과 동일 (4px 히트박스·1프레임 무적)",
            L"어려움 — 웹 원본 + 조기 자비 없음·아이템 2개"
        };
        int i;
        drawTextCentered(CLIENT_W / 2, 96, L"난이도 선택", RGB(255, 255, 255), gFontBig);
        for (i = 0; i < 3; i++) {
            int bx = 120 + i * 140, by = 210, bw = 120, bh = 54;
            int sel = (gDiffSel == i);
            COLORREF col = sel ? RGB(255, 255, 0) : RGB(120, 120, 120);
            fillRect(gMemDC, bx, by, bw, bh, gDkRed);
            if (sel) {   /* 선택 테두리 */
                HBRUSH ob = (HBRUSH)SelectObject(gMemDC, GetStockObject(NULL_BRUSH));
                HPEN op = (HPEN)SelectObject(gMemDC, gMenuPen);
                Rectangle(gMemDC, bx - 4, by - 4, bx + bw + 4, by + bh + 4);
                SelectObject(gMemDC, op); SelectObject(gMemDC, ob);
            }
            drawTextCentered(bx + bw / 2, by + 16, names[i], col, gFontSmall);
        }
        drawTextWrapped(60, 300, CLIENT_W - 120, 60, desc[gDiffSel], RGB(255, 255, 255), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 372, L"← → 선택    Z 확인", RGB(160, 160, 160), gFontTiny);
        return;
    }
    if (gState == ST_GAMEOVER) {   /* BTS: 금간 하트 → 6조각 파편 (GAME OVER 텍스트 없음) */
        if (gDeathT < 0.7f) {
            if (gSprHeartSplit.ok) drawSprite(&gSprHeartSplit, (int)gDeathX - 8, (int)gDeathY - 10);
            else fillRect(gMemDC, (int)gDeathX - 8, (int)gDeathY - 8, SOUL_SIZE, SOUL_SIZE, gRed);
        } else {
            int si;
            for (si = 0; si < 6; si++) {
                if (gSprHeartShard.ok) drawSprite(&gSprHeartShard, (int)gShardX[si] - 8, (int)gShardY[si] - 8);
                else fillRect(gMemDC, (int)gShardX[si] - 3, (int)gShardY[si] - 3, 6, 6, gRed);
            }
        }
        if (gDeathT > 2.2f)
            drawTextCentered(CLIENT_W / 2, 340, L"Z를 눌러 타이틀로 돌아가기", RGB(255, 255, 0), gFontSmall);
        return;
    }
    if (gState == ST_WIN) {
        drawSans();   /* 샌즈 y68~224 — 텍스트는 그 아래로 */
        drawTextCentered(CLIENT_W / 2, 250, L"* 흠. 난 그릴비네 가게나 가야겠다.", RGB(255, 255, 255), gFontSmall);
        drawTextCentered(CLIENT_W / 2, 290, L"승리!  (자비)", RGB(255, 255, 0), gFontBig);
        drawTextCentered(CLIENT_W / 2, 360, L"Z를 눌러 타이틀로 돌아가기", RGB(255, 255, 0), gFontSmall);
        return;
    }

    /* ---- 전투 ---- */
    drawSans();

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
    } else if (gPhase == PH_FIGHTAIM) {   /* 타겟존(중앙 노란마커) + 스위핑 바(흰색) */
        int zc = BOX_X + BOX_W / 2;
        drawText(gMemDC, BOX_X + 12, BOX_Y + 16, L"* 가운데에 맞춰 공격!", RGB(255, 255, 255), gFontSmall);
        fillRect(gMemDC, zc - FIGHT_ZONE_HALF, BOX_Y + 6, FIGHT_ZONE_HALF * 2, BOX_H - 12, gDkRed);      /* 타겟존 */
        fillRect(gMemDC, zc - FIGHT_ZONE_HALF, BOX_Y + 6, 3, BOX_H - 12, gYellow);                       /* 좌 마커 */
        fillRect(gMemDC, zc + FIGHT_ZONE_HALF - 3, BOX_Y + 6, 3, BOX_H - 12, gYellow);                   /* 우 마커 */
        fillRect(gMemDC, (int)gFightAimX, BOX_Y + 6, 6, BOX_H - 12, gWhite);                             /* 스위핑 바 */
    } else if (gPhase == PH_FIGHT) {
        drawText(gMemDC, BOX_X + 12, BOX_Y + 16, L"* 공격!", RGB(255, 255, 255), gFontSmall);
        if (gStrikeT >= 0.0f && gStrikeT < 0.3f) {   /* 샌즈 위 흰 슬래시(6프레임) */
            int f = (int)(gStrikeT * 20.0f); if (f > 5) f = 5;
            if (gSprStrike[f].ok) drawSprite(&gSprStrike[f], SANS_CX - gSprStrike[f].w / 2, 132 - gSprStrike[f].h / 2);
        }
        if (gSansDodgeT > 0.3f) {   /* 명중=노랑, 빗나감=회색 (샌즈 위 고정 위치) */
            if (gFightHit) drawTextCentered(SANS_CX, 76, L"명중!", RGB(255, 220, 0), gFontBig);
            else           drawTextCentered(300, 80, L"빗나감!", RGB(191, 191, 191), gFontSmall);
        }
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
    } else { /* PH_MENU — BTS InfoText 플레이버 텍스트 */
        drawTextWrapped(BOX_X + 12, BOX_Y + 14, BOX_W - 24, 40, menuInfoText(), RGB(255, 255, 255), gFontSmall);
        if (gHitAttempts >= diffMercyHA())
            drawText(gMemDC, BOX_X + 12, BOX_Y + 50, L"* (자비 가능!)", RGB(255, 255, 0), gFontTiny);
    }

    drawHpBar();
    drawMenuButton(&gSprFight, &gSprFightHi, L"FIGHT", 0);
    drawMenuButton(&gSprAct,   &gSprActHi,   L"ACT",   1);
    drawMenuButton(&gSprItem,  &gSprItemHi,  L"ITEM",  2);
    drawMenuButton(&gSprMercy, &gSprMercyHi, L"MERCY", 3);

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
/* 레터박스 대상 사각형 계산(현재 클라이언트에 640x480 비율유지로 맞춤) */
static void computeDest(void) {
    RECT cr; int cw, ch; double s, s2;
    if (!gHwnd) return;
    GetClientRect(gHwnd, &cr);
    cw = cr.right - cr.left; ch = cr.bottom - cr.top;
    if (cw <= 0 || ch <= 0) { gDstX = 0; gDstY = 0; gDstW = CLIENT_W; gDstH = CLIENT_H; return; }
    s = (double)cw / CLIENT_W; s2 = (double)ch / CLIENT_H; if (s2 < s) s = s2;
    gDstW = (int)(CLIENT_W * s); gDstH = (int)(CLIENT_H * s);
    gDstX = (cw - gDstW) / 2; gDstY = (ch - gDstH) / 2;
}
/* 백버퍼(640x480)를 레터박스 스케일로 표시. 흔들림 오프셋(스케일 반영). */
static void present(HDC dc) {
    RECT cr; int sx, sy;
    if (!gMemDC) return;
    GetClientRect(gHwnd, &cr);
    FillRect(dc, &cr, gBlack);   /* 레터박스 검정 */
    sx = gDstX + (int)((double)gShakeDx * gDstW / CLIENT_W);
    sy = gDstY + (int)((double)gShakeDy * gDstH / CLIENT_H);
    SetStretchBltMode(dc, COLORONCOLOR);   /* 픽셀 또렷하게(니어리스트) */
    StretchBlt(dc, sx, sy, gDstW, gDstH, gMemDC, 0, 0, CLIENT_W, CLIENT_H, SRCCOPY);
}
/* 전체화면 ↔ 창 적용(테두리없는 풀스크린=디스플레이모드 변경 없음→안정적, Alt+Tab 안전) */
static void applyWindowMode(void) {
    if (gFullscreen) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongPtrA(gHwnd, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_VISIBLE));
        SetWindowPos(gHwnd, HWND_TOP, 0, 0, sw, sh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        DWORD st = (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE;
        RECT rc; int ww, wh, px, py;
        rc.left = 0; rc.top = 0; rc.right = CLIENT_W; rc.bottom = CLIENT_H;
        AdjustWindowRect(&rc, st, FALSE);
        ww = rc.right - rc.left; wh = rc.bottom - rc.top;
        px = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2; py = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
        if (px < 0) px = 0; if (py < 0) py = 0;
        SetWindowLongPtrA(gHwnd, GWL_STYLE, (LONG_PTR)st);
        SetWindowPos(gHwnd, HWND_TOP, px, py, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    computeDest();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_ERASEBKGND: return 1;   /* 배경 지우기 차단 — 전체화면 깜빡임 방지(present가 전체 그림) */
    case WM_SIZE:    computeDest(); return 0;   /* 해상도/크기 변경 시 레터박스 재계산 */
    case WM_SETCURSOR:
        if (gFullscreen && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; } /* 전체화면 커서 숨김 */
        break;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        else if (w == VK_F1)  gDebug ^= 1;                       /* 디버그 오버레이 */
        else if (w == VK_F11) { gFullscreen ^= 1; applyWindowMode(); }  /* 전체화면 토글 */
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        present(dc);
        EndPaint(h, &ps); return 0;
    }
    }
    return DefWindowProcA(h, m, w, l);
}

/* MCI 오디오(음성+효과음)를 시작 시 한 번에 연다.
   BGM(PlaySound SND_LOOP)이 켜진 뒤 게임 루프 안에서 MCI 'open'을 호출하면
   오디오 장치 경합으로 mciSendString이 블록되어 게임이 멈추는 문제가 있다.
   → BGM 시작(startBattle) 전인 여기서 미리 열어두고, 이후엔 'play'만 한다. */
static void openAllAudio(void) {
    static const char* sfx[] = {
        "GasterBlaster", "GasterBlast", "Ding", "Flash",
        "Slam", "Warning", "BoneStab", "PlayerDamaged",
        "HeartSplit", "HeartShatter", "PlayerFight", "PlayerHeal"
    };
    int n = (int)(sizeof(sfx) / sizeof(sfx[0]));
    char cmd[400], rel[64], alias[40]; int i;
    if (!gVoiceOpen) {
        wsprintfA(cmd, "open \"%s\" type waveaudio alias spk", assetPath("sans_speak.wav"));
        if (mciSendStringA(cmd, NULL, 0, NULL) == 0) gVoiceOpen = 1;
    }
    for (i = 0; i < n; i++) {
        wsprintfA(alias, "sfx_%s", sfx[i]);
        wsprintfA(rel, "sfx_%s.wav", sfx[i]);
        wsprintfA(cmd, "open \"%s\" type waveaudio alias %s", assetPath(rel), alias);
        if (mciSendStringA(cmd, NULL, 0, NULL) == 0 && gSfxN < 16) {
            strncpy(gSfxOpen[gSfxN], alias, 39); gSfxOpen[gSfxN][39] = 0; gSfxN++;
        }
    }
}

static void initResources(void) {
    HDC wdc = GetDC(gHwnd);
    gMemDC  = CreateCompatibleDC(wdc);
    gMemBmp = CreateCompatibleBitmap(wdc, CLIENT_W, CLIENT_H);
    gOldBmp = (HBITMAP)SelectObject(gMemDC, gMemBmp);
    PatBlt(gMemDC, 0, 0, CLIENT_W, CLIENT_H, BLACKNESS);   /* 첫 표시 전 검정 클리어(쓰레기값 방지) */
    ReleaseDC(gHwnd, wdc);

    gBlack  = CreateSolidBrush(RGB(0, 0, 0));
    gWhite  = CreateSolidBrush(RGB(255, 255, 255));
    gRed    = CreateSolidBrush(RGB(255, 0, 0));
    gYellow = CreateSolidBrush(RGB(255, 255, 0));
    gBlue   = CreateSolidBrush(RGB(60, 120, 255));
    gDkRed  = CreateSolidBrush(RGB(90, 0, 0));
    gCyan   = CreateSolidBrush(RGB(0, 220, 255));
    gHpBg   = CreateSolidBrush(RGB(191, 0, 0));   /* BTS HP바 빈칸 색 */

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
    gSprLegs        = loadSprite("sans_legs.bmp");
    gSprTorso       = loadSprite("sans_torso.bmp");
    gSprHeadDef     = loadSprite("sans_head.bmp");
    gSprHeadBlue    = loadSprite("sans_head_blue.bmp");
    gSprHeadBlue1   = loadSprite("sans_head_blue1.bmp");
    gSprHeadNoEye   = loadSprite("sans_head_noeyes.bmp");
    gSprHeadClosed  = loadSprite("sans_head_closed.bmp");
    gSprHeadTired   = loadSprite("sans_head_tired.bmp");
    gSprHeadTired2  = loadSprite("sans_head_tired2.bmp");
    gSprHeadWink    = loadSprite("sans_head_wink.bmp");
    gSprHeadLook    = loadSprite("sans_head_lookleft.bmp");
    gSprBodyDown    = loadSprite("sans_body_down.bmp");
    gSprBodyUp      = loadSprite("sans_body_up.bmp");
    gSprBodyLeft    = loadSprite("sans_body_left.bmp");
    gSprBodyRight   = loadSprite("sans_body_right.bmp");
    gSprSweat       = loadSprite("sans_sweat.bmp");
    gSprHeart       = loadSprite("heart.bmp");
    gSprHeartBlue   = loadSprite("heart_blue.bmp");
    gSprBlaster     = loadSprite("blaster.bmp");
    gSprBlasterFire = loadSprite("blaster_fire.bmp");
    gSprFight       = loadSprite("ui_fight.bmp");
    gSprAct         = loadSprite("ui_act.bmp");
    gSprItem        = loadSprite("ui_item.bmp");
    gSprMercy       = loadSprite("ui_mercy.bmp");
    gSprFightHi     = loadSprite("ui_fight_hi.bmp");
    gSprActHi       = loadSprite("ui_act_hi.bmp");
    gSprItemHi      = loadSprite("ui_item_hi.bmp");
    gSprMercyHi     = loadSprite("ui_mercy_hi.bmp");
    gSprHeartSplit  = loadSprite("heart_split.bmp");
    gSprHeartShard  = loadSprite("heart_shard.bmp");
    {   /* FIGHT 슬래시 6프레임 */
        int i; char nm[24];
        for (i = 0; i < 6; i++) { wsprintfA(nm, "strike%d.bmp", i); gSprStrike[i] = loadSprite(nm); }
    }
    {   /* 8각 사전회전 게이스터 블래스터 */
        int i; char nm[24];
        for (i = 0; i < 8; i++) {
            wsprintfA(nm, "blaster_d%d.bmp", i); gSprBlasterD[i] = loadSprite(nm);
            wsprintfA(nm, "blaster_f%d.bmp", i); gSprBlasterF[i] = loadSprite(nm);
        }
    }
    gMenuPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));   /* 메뉴 하이라이트(최적화: 1회 생성) */
}

static void freeResources(void) {
    int i;
    freeSprite(&gSprLegs); freeSprite(&gSprTorso);
    freeSprite(&gSprBodyDown); freeSprite(&gSprBodyUp); freeSprite(&gSprBodyLeft); freeSprite(&gSprBodyRight); freeSprite(&gSprSweat);
    freeSprite(&gSprHeadDef); freeSprite(&gSprHeadBlue); freeSprite(&gSprHeadBlue1); freeSprite(&gSprHeadNoEye);
    freeSprite(&gSprHeadClosed); freeSprite(&gSprHeadTired); freeSprite(&gSprHeadTired2);
    freeSprite(&gSprHeadWink); freeSprite(&gSprHeadLook);
    for (i = 0; i < 6; i++) freeSprite(&gSprStrike[i]);
    freeSprite(&gSprHeart); freeSprite(&gSprHeartBlue); freeSprite(&gSprBlaster); freeSprite(&gSprBlasterFire);
    freeSprite(&gSprFight); freeSprite(&gSprAct); freeSprite(&gSprItem); freeSprite(&gSprMercy);
    freeSprite(&gSprFightHi); freeSprite(&gSprActHi); freeSprite(&gSprItemHi); freeSprite(&gSprMercyHi);
    freeSprite(&gSprHeartSplit); freeSprite(&gSprHeartShard);
    for (i = 0; i < 8; i++) { freeSprite(&gSprBlasterD[i]); freeSprite(&gSprBlasterF[i]); }
    if (gMenuPen) DeleteObject(gMenuPen);
    if (gMemDC) { SelectObject(gMemDC, gOldBmp); DeleteDC(gMemDC); }
    if (gMemBmp) DeleteObject(gMemBmp);
    DeleteObject(gBlack); DeleteObject(gWhite); DeleteObject(gRed); DeleteObject(gYellow);
    DeleteObject(gBlue); DeleteObject(gDkRed); DeleteObject(gCyan); DeleteObject(gHpBg);
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

    initResources();   /* gMemDC 생성(창 존재 필요) — applyWindowMode(표시) 전에 */
    openAllAudio();                        /* BGM 켜기 전에 MCI 음성/효과음 장치 미리 열기(루프 멈춤 방지) */
    gHost.on_command = haz_on_command;     /* VM → 게임 명령 라우팅 */
    gHost.get_heart_pos = haz_get_heart_pos;
    gHost.ctx = NULL;
    applyWindowMode();   /* 전체화면(기본) 적용 + 표시 + 레터박스 계산 */
    UpdateWindow(gHwnd);
    (void)show;

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
                HDC dc;
                render();
                dc = GetDC(gHwnd);
                present(dc);            /* 레터박스 스케일 표시(전체화면/창 공용) */
                ReleaseDC(gHwnd, dc);
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
