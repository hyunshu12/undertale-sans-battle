/*
 * game.h — 게임(main.c)과 탄막(hazards.c) 공용 인터페이스.
 * windows.h 사용 가능(vm.h는 순수 C로 유지). main.c/hazards.c만 포함.
 */
#ifndef GAME_H
#define GAME_H
#include <windows.h>
#include "vm.h"

/* 전투 박스(픽셀). main이 소유, CombatZoneResize가 변경. */
typedef struct { int x, y, w, h; } Box;
extern Box gBox;

/* --- main 이 제공(hazards 가 호출) --- */
void game_get_heart(double* cx, double* cy);   /* 하트 중심 좌표 */
int  game_heart_moving(void);                  /* 이번 프레임 이동 여부(파랑/주황 뼈 판정) */
void game_hurt(int dmg, int karma);            /* 피격(무적/HP/KR) */
void game_set_heart_mode(int blue);            /* HeartMode 0=빨강,1=파랑 */
void game_teleport_heart(double x, double y);  /* HeartTeleport(중심 기준) */
void game_end_attack(void);                    /* EndAttack → 메뉴로 */
void game_play_sound(const char* name);        /* Sound 명령 → MCI sfx */
void game_shake(double intensity);             /* 화면 흔들림 */
void game_set_blackscreen(int on);             /* BlackScreen 0/1 */
void game_sans_text(const char* text);         /* SansText → 말풍선(한국어) */
void game_set_max_fall(double v);              /* HeartMaxFallSpeed */
double game_get_max_fall(void);
void game_sans_head(const char* state);        /* SansHead 표정(Default/BlueEye/NoEyes/ClosedEyes/Tired) */
void game_sans_body(const char* pose);         /* SansBody 팔 포즈(HandUp/Down/Left/Right) */
void game_sans_animation(const char* name);    /* SansAnimation 호흡(Idle/HeadBob/Tired) */
void game_sans_x(int x);                        /* SansX 가로 위치(기본 320) */
void game_sans_slam(int dir);                   /* SansSlam: 영혼을 dir(0우1하2좌3상) 방향으로 내리꽂기 */
void game_draw_blaster(HDC dc, double cx, double cy, double ang, int size, int firing); /* 회전 블래스터 스프라이트 */

/* --- hazards.c API --- */
void haz_reset(void);                          /* 탄막 초기화 */
void haz_free(void);                            /* 종료 시 브러시 해제 */
void haz_set_vm(VM* vm);                        /* CombatZoneResize의 TLResume 콜백용 */
void haz_on_command(void* ctx, const char* cmd, char args[][VM_ARG_LEN], int argc);
void haz_get_heart_pos(void* ctx, double* x, double* y);
void haz_update(float dt);
void haz_render(HDC dc);
int  haz_active_count(void);                    /* 디버그 오버레이용(뼈+블래스터 등) */
int  haz_is_solid(double x, double y, double w, double h, double* outTopY); /* 플랫폼 위 솔리드 */
double haz_platform_vx(double footX, double footY, double w);              /* 발밑 플랫폼 수평속도 */

#endif
