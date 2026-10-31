#ifndef __MOD_PLAY_SAMPLES_H__
#define __MOD_PLAY_SAMPLES_H__
#include "BaseLib/PrimitiveTypes.h"


//modplay.exe's settings, shared with the test that checks what it plays against CModPlayer on the host.
#define MODPLAY_FILE				"\\SpaceDebris.mod"
#define MODPLAY_RATE_SELECT			2										//The sound board's rate: 32768Hz >> 2.
#define MODPLAY_SAMPLE_RATE			(32768 >> MODPLAY_RATE_SELECT)
#define MODPLAY_BLOCK_FRAMES		256										//Frames rendered at a time.
#define MODPLAY_BUFFER_FRAMES		0x8000									//The sound board's RAM A and RAM B.
#define MODPLAY_MAX_QUEUED			(MODPLAY_BUFFER_FRAMES - MODPLAY_BLOCK_FRAMES)	//Written but not played yet.
#define MODPLAY_HEAP_SIZE			0x180000									//The file, the song (its samples copied), the objects.

#define MODPLAY_EXIT_SUCCESS		0
#define MODPLAY_EXIT_NO_FILE		1
#define MODPLAY_EXIT_NO_MEMORY		2
#define MODPLAY_EXIT_NOT_A_MODULE	3
#define MODPLAY_EXIT_NO_SOUND_BOARD	4
#define MODPLAY_EXIT_BAD_LOAD		5		//The file read isn't a module ("M.K." isn't at 1080).


//A mixed 16 bit sample as the sound board's DAC takes it: unsigned 8 bit, $80 the middle.
inline uint8 ModPlaySample(int16 iSample)
{
	return (uint8)((iSample >> 8) + 128);
}


#endif // __MOD_PLAY_SAMPLES_H__
