#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "fatfs.h"

#define MAX_SONGS 100

typedef struct {
    char name[100]; 
} Song;

void Player_Init(void);
void Player_GetDiskInfo(void);
void Player_ListFiles(void);
//int Player_SelectTrack(int track_index);
void Player_ExecuteCommand(const char* cmd);
int Player_Play(int track_index);
void Player_Pause();

void Player_Process(void);
void Player_Stop(void);

void Player_TogglePause(void);
void Player_Next(void);
void Player_Prev(void);
void Player_SetVolume(uint8_t vol_percent);
uint8_t Player_GetVolume(void);
void Button_Process(void);
void Player_HandleHotSwap(void);


#endif /* _PLAYER_H_ */
