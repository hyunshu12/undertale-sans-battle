# UNDERTALE - Sans Battle (Win32 GDI)

언더테일 샌즈전 — Windows 콘솔/창 C 게임 (수행평가). **Win32 GDI 버전(#3)**.

## 실행 방법 (별도 세팅 없음)

1. 이 저장소를 클론/다운로드
   ```
   git clone https://github.com/hyunshu12/undertale-sans-battle
   ```
2. `UndertaleSans/UndertaleSans.sln` 을 **Visual Studio로 열기**
3. **F5** (또는 Ctrl+F5) → 게임 창이 바로 실행됨

> 외부 라이브러리·에셋 파일 불필요. Windows 시스템 라이브러리(gdi32/user32/winmm)만 사용하며 코드에서 자동 링크.

### 만약 VS가 "프로젝트 다시 대상 지정(retarget)"을 물으면
- **그냥 "확인/OK" 클릭**하면 됨 (VS 버전/SDK 차이 자동 조정). 빌드에 영향 없음.

## 조작

| 키 | 동작 |
|---|---|
| 화살표 / WASD | (적턴) 영혼 이동 |
| ← → (또는 A/D) | (메뉴) FIGHT/ACT/ITEM/MERCY 선택 |
| Z 또는 Enter | 대사 진행 / 메뉴 확정 / 시작·재시작 |
| ESC | 종료 |

## 현재 범위 (Slice 2)

타이틀 → 전투 → 게임오버/자비 엔딩. 전투는 턴제:
- **대사**(샌즈 대사 타이핑) → **적턴**(탄막 회피) → **메뉴**(FIGHT/ACT/ITEM/MERCY) 반복
- 적턴 패턴: 짝수턴 = 뼈 탄막, 홀수턴 = 게이스터 블래스터(빔)
- **4턴 생존 후 MERCY 선택 → 자비 엔딩 승리**. HP 0 → 게임오버.

실제 Bad Time Simulator 스프라이트(샌즈 머리/영혼/블래스터/메뉴 버튼)와 **Megalovania**(`assets/`)를 사용.
에셋이 없거나 못 읽어도 GDI 도형으로 폴백되어 항상 실행됨.

> 에셋은 `assets/` 폴더에 포함되어 있고, 실행 파일 위치/작업 디렉터리 어디서든 찾도록 경로를 자동 탐색함.

이후 슬라이스 예정: 난이도 선택, 파란 영혼 중력 점프, 파란/흰 뼈, ACT 분기, 더 많은 패턴/연출, 효과음.

## 빌드가 안 되거나 화면이 이상하면
오류 메시지 전문 또는 실행 화면 스크린샷을 공유해 주세요. (개발이 Mac에서 이뤄져 Windows 실행 결과 피드백이 필요합니다.)
