#define _CRT_SECURE_NO_WARNINGS

#include<stdio.h>
#include<stdlib.h>  //rand()
#include<windows.h>
#include<mmsystem.h>
#include<time.h>

#define WIDTH 50
#define HEIGHT 24

#define LEFT 75
#define RIGHT 77
#define STAR_NUM 50

struct Star {
	int x;
	int y;
	int speed;
}star[STAR_NUM];

void initStar()
{
	srand(time(NULL));
	for (int i = 0; i < STAR_NUM; i++)
	{
		star[i].x = rand()%46+2; //2 ~ 47
		star[i].y = 3;
		star[i].speed = rand()%11+10; //10~20
	}
}

void resizeConsole(int w, int h)
{
	char chTemp[100];
	sprintf(chTemp, "mode con cols=%d lines=%d",w,h);
	system(chTemp);
}
void gotoxy(int x, int y)
{
	COORD pos = { x,y };
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}
void displayStart()
{
	while (!_kbhit())
	{
		gotoxy(10, 12);
		printf("시작하려면 아무키나 누르세요!");
		Sleep(500);
		gotoxy(10, 12);
		printf("                                           ");
		Sleep(500);
	}
	char ch = _getch();
	system("cls");
	
}
void clearCursor()
{
	CONSOLE_CURSOR_INFO c = { 0 };
	c.bVisible = FALSE;
	c.dwSize = 1;
	SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &c);
}
void printBound()
{
	for (int y = 2; y < HEIGHT; y+=1)
	{
		for (int x = 0; x < WIDTH; x += 2)
		{
			if (y == 2 || y == HEIGHT-1 || x == 0 || x == WIDTH-2)
			{
				gotoxy(x, y);
				printf("■");
			}
				
		}
	}
}
void displayScore(int score)
{
	gotoxy((int)(WIDTH * 0.6), 1);
	printf("Score : %3d", score);
}
void setPlayerPosition(int x)
{
	gotoxy(x, HEIGHT - 2);
	printf("▲");
}
void displayStar(int cnt)
{
	for (int i = 0; i < STAR_NUM; i++)
	{
		if (star[i].y < 23)
		{			
			if (star[i].y > 3)
			{
				gotoxy(star[i].x, star[i].y - 1);
				printf(" ");
			}	
			gotoxy(star[i].x, star[i].y);
			printf("*");

			if(cnt % star[i].speed == 0)
				star[i].y++;
		}
		else
		{
			gotoxy(star[i].x, star[i].y - 1);
			printf(" ");
			star[i].y = 3;
			star[i].x = rand() % 46 + 2;
			star[i].speed = rand() % 5 + 10;
		}
		
	}
	
}
int checkCrush(int x)
{
	for (int i = 0; i < STAR_NUM; i++)
	{
		if (star[i].y == HEIGHT - 2)
		{
			if (star[i].x == x || star[i].x == x + 1)
				return 1;
		}
	}
	return 0;
}
void displayEnding(int score)
{
	system("cls");
	PlaySound(NULL, NULL, SND_PURGE);
	gotoxy(WIDTH / 2 - 4, HEIGHT / 2);
	printf("게임 종료!");
	gotoxy(WIDTH / 2 - 4, HEIGHT / 2+1);
	printf("Score : %3d", score);
}
void main()
{
	int score = 0;
	int count = 0;
	int xPosition = WIDTH / 2;
	clearCursor();
	resizeConsole(50,24);


	PlaySound(TEXT("bgm.wav"),
		NULL, SND_FILENAME | SND_ASYNC | SND_LOOP);
	displayStart();
	printBound();
	displayScore(score);
	setPlayerPosition(xPosition);
	initStar();

	while (1)
	{
		displayStar(count);
		if (_kbhit())
		{
			char key = _getch();
			if (key == LEFT && xPosition >2)
				setPlayerPosition(--xPosition);
			if (key == RIGHT && xPosition < WIDTH-4)
				setPlayerPosition(++xPosition);
		}
		count++;
		if (count % 100 == 0)
			displayScore(++score);

		if (checkCrush(xPosition) == 1)
		{
			break;
		}
		Sleep(10);
	}
	displayEnding(score);
	getchar();

}