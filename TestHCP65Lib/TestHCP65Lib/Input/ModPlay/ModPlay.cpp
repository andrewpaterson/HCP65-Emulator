#include <stdlib.h>
#include <string.h>
#include "BaseLib/TypeNames.h"
#include "BaseLib/TypeConverter.h"
#include "BaseLib/GlobalMemory.h"
#include "BaseLib/GlobalDataTypesIO.h"
#include "StandardLib/Objects.h"
#include "SupportLib/MODReader.h"
#include "SupportLib/ModPlayer.h"
#include "HCP65User.h"
#include "ModPlaySamples.h"


//The sound board's registers (HCP65Lib/HCP65Audio.h).
#define AUDIO_FLAGS				0
#define AUDIO_START_LOW			2
#define AUDIO_START_HIGH		3
#define AUDIO_END_LOW			4
#define AUDIO_END_HIGH			5
#define AUDIO_FLAG_ENABLE		0x40


SModUnsigned8	gsModPlayBlock;		//A block as the sound board plays it (MODPLAY_BLOCK_FRAMES is MOD_MIX_BLOCK).


//////////////////////////////////////////////////////////////////////////
//
// Renders the next block (or what is left of the song, or of the frames
// to play) and writes it to the sound board at uiWrite.  The number of
// frames written.
//
//////////////////////////////////////////////////////////////////////////
uint16 ModPlayBlock(CModPlayer* pcPlayer, uint16 uiWrite, lsize uiFramesLeft)
{
	lsize	uiFrames;

	uiFrames = (uiFramesLeft < MODPLAY_BLOCK_FRAMES) ? uiFramesLeft : MODPLAY_BLOCK_FRAMES;
	uiFrames = pcPlayer->Render8(&gsModPlayBlock, uiFrames);
	if (uiFrames > 0)
	{
		KernalAudioWrite(uiWrite, gsModPlayBlock.auiLeft, gsModPlayBlock.auiRight, (uint16)uiFrames);
	}
	return (uint16)uiFrames;
}


//////////////////////////////////////////////////////////////////////////
//
// Frames written to the sound board that it hasn't played yet.
//
//////////////////////////////////////////////////////////////////////////
uint16 ModPlayQueued(uint16 uiWrite)
{
	return (uint16)((uiWrite - KernalAudioGetCounter()) & (MODPLAY_BUFFER_FRAMES - 1));
}


//////////////////////////////////////////////////////////////////////////
//
// Plays the song through the sound board: renders MODPLAY_START_FRAMES,
// starts it, then renders a block whenever there is room for one (it
// keeps up to MODPLAY_MAX_QUEUED ahead), until the song ends.
// The sound board plays its 32KB again and again, so after the song a
// block of silence follows and it is stopped once it reaches it.
//
//////////////////////////////////////////////////////////////////////////
void ModPlaySong(CModPlayer* pcPlayer, lsize uiMaxFrames)
{
	uint16	uiWrite;
	uint16	uiWritten;
	lsize	uiPlayed;
	uint16	i;
	bool	bStarted;

	KernalAudioSetRegister(AUDIO_FLAGS, MODPLAY_RATE_SELECT);
	KernalAudioSetRegister(AUDIO_START_LOW, 0);
	KernalAudioSetRegister(AUDIO_START_HIGH, 0);
	KernalAudioSetRegister(AUDIO_END_LOW, (MODPLAY_BUFFER_FRAMES - 1) & 0xFF);
	KernalAudioSetRegister(AUDIO_END_HIGH, (MODPLAY_BUFFER_FRAMES - 1) >> 8);

	uiWrite = 0;
	uiPlayed = 0;
	bStarted = false;
	while (uiPlayed < uiMaxFrames)
	{
		if (bStarted && (ModPlayQueued(uiWrite) > MODPLAY_MAX_QUEUED - MODPLAY_BLOCK_FRAMES))
		{
			continue;
		}
		uiWritten = ModPlayBlock(pcPlayer, uiWrite, uiMaxFrames - uiPlayed);
		if (uiWritten == 0)
		{
			break;
		}
		uiWrite = (uint16)((uiWrite + uiWritten) & (MODPLAY_BUFFER_FRAMES - 1));
		uiPlayed += uiWritten;
		if (!bStarted && ((uiWrite >= MODPLAY_START_FRAMES) || (uiPlayed >= uiMaxFrames)))
		{
			KernalAudioSetRegister(AUDIO_FLAGS, AUDIO_FLAG_ENABLE | MODPLAY_RATE_SELECT);
			bStarted = true;
		}
	}
	if (!bStarted)
	{
		KernalAudioSetRegister(AUDIO_FLAGS, AUDIO_FLAG_ENABLE | MODPLAY_RATE_SELECT);
	}

	//Silence after the end, then wait for the last of the song to play.
	for (i = 0; i < MODPLAY_BLOCK_FRAMES; i++)
	{
		gsModPlayBlock.auiLeft[i] = 0x80;
		gsModPlayBlock.auiRight[i] = 0x80;
	}
	KernalAudioWrite(uiWrite, gsModPlayBlock.auiLeft, gsModPlayBlock.auiRight, MODPLAY_BLOCK_FRAMES);
	while (ModPlayQueued(uiWrite) != 0)
	{
	}
	KernalAudioSetRegister(AUDIO_FLAGS, MODPLAY_RATE_SELECT);
}


//////////////////////////////////////////////////////////////////////////
//
// modplay.exe: plays Space Debris from the SD card through the sound
// board.  Its argument is how many seconds to play, zero for all of it.
//
//////////////////////////////////////////////////////////////////////////
int main(void)
{
	CModPlayer		cPlayer;
	Ptr<CModSong>	pcSong;
	uint32			uiSize;
	void*			pvFile;
	lsize			uiMaxFrames;
	uint16			uiSeconds;
	int				iResult;

	uiSeconds = KernalGetArgument();
	uiMaxFrames = (uiSeconds == 0) ? (lsize)-1 : (lsize)uiSeconds * MODPLAY_SAMPLE_RATE;

	uiSize = KernalFileSize(MODPLAY_FILE);
	if (uiSize == KERNAL_FILE_NOT_FOUND)
	{
		return MODPLAY_EXIT_NO_FILE;
	}
	pvFile = malloc(uiSize);
	if (!pvFile)
	{
		return MODPLAY_EXIT_NO_MEMORY;
	}
	if (KernalFileLoad(MODPLAY_FILE, pvFile, uiSize) != KERNAL_RESULT_SUCCESS)
	{
		free(pvFile);
		return MODPLAY_EXIT_NO_FILE;
	}
	if (KernalAudioOpen() != KERNAL_RESULT_SUCCESS)
	{
		free(pvFile);
		return MODPLAY_EXIT_NO_SOUND_BOARD;
	}

	TypesInit();
	TypeConverterInit();
	MemoryInit();
	DataIOInit();
	ObjectsInit();

	pcSong = LoadMOD(pvFile, uiSize);
	if (pcSong.IsNull())
	{
		iResult = (memcmp((char*)pvFile + 1080, "M.K.", 4) == 0) ? MODPLAY_EXIT_NOT_A_MODULE : MODPLAY_EXIT_BAD_LOAD;
		free(pvFile);
	}
	else
	{
		free(pvFile);
		cPlayer.Init(pcSong, MODPLAY_SAMPLE_RATE, MI_Nearest, 1.0);
		ModPlaySong(&cPlayer, uiMaxFrames);
		cPlayer.Kill();
		iResult = MODPLAY_EXIT_SUCCESS;
	}
	pcSong = NULL;

	ObjectsKill(false);
	DataIOKill();
	MemoryKill();
	TypeConverterKill();
	TypesKill();
	return iResult;
}
