#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/Logger.h"
#include "HostAudio.h"


//waveOut with a ring of buffers: Write fills the current one and queues it when it is full.  A buffer still queued
//(playing or waiting to) isn't refilled: frames that don't fit are dropped.
#define HOST_AUDIO_BUFFERS			8
#define HOST_AUDIO_BUFFER_FRAMES	512

struct SHostAudioWindows
{
	HWAVEOUT	hWaveOut;
	WAVEHDR		asHeaders[HOST_AUDIO_BUFFERS];
	uint8		aauiData[HOST_AUDIO_BUFFERS][HOST_AUDIO_BUFFER_FRAMES * 2];
	uint32		uiCurrent;
	uint32		uiFilled;	//Frames in the current buffer.
};


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHostAudio::Init(void)
{
	mpvNative = NULL;
	muiSampleRate = 0;
	muiDroppedFrames = 0;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHostAudio::Kill(void)
{
	Close();
}


//////////////////////////////////////////////////////////////////////////
//
// The wave mapper's output at uiSampleRate 8 bit stereo frames.
//
//////////////////////////////////////////////////////////////////////////
bool CHostAudio::Open(uint32 uiSampleRate)
{
	SHostAudioWindows*	psAudio;
	WAVEFORMATEX		sFormat;
	MMRESULT			eResult;
	uint32				i;

	Close();
	psAudio = (SHostAudioWindows*)malloc(sizeof(SHostAudioWindows));
	memset(psAudio, 0, sizeof(SHostAudioWindows));

	memset(&sFormat, 0, sizeof(WAVEFORMATEX));
	sFormat.wFormatTag = WAVE_FORMAT_PCM;
	sFormat.nChannels = 2;
	sFormat.nSamplesPerSec = uiSampleRate;
	sFormat.wBitsPerSample = 8;
	sFormat.nBlockAlign = 2;
	sFormat.nAvgBytesPerSec = uiSampleRate * 2;
	eResult = waveOutOpen(&psAudio->hWaveOut, WAVE_MAPPER, &sFormat, 0, 0, CALLBACK_NULL);
	if (eResult != MMSYSERR_NOERROR)
	{
		gcLogger.Error2(__METHOD__, " Could not open the wave output: there is no sound.", NULL);
		free(psAudio);
		return false;
	}

	for (i = 0; i < HOST_AUDIO_BUFFERS; i++)
	{
		psAudio->asHeaders[i].lpData = (LPSTR)psAudio->aauiData[i];
		psAudio->asHeaders[i].dwBufferLength = HOST_AUDIO_BUFFER_FRAMES * 2;
		waveOutPrepareHeader(psAudio->hWaveOut, &psAudio->asHeaders[i], sizeof(WAVEHDR));
	}
	psAudio->uiCurrent = 0;
	psAudio->uiFilled = 0;

	mpvNative = psAudio;
	muiSampleRate = uiSampleRate;
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHostAudio::Close(void)
{
	SHostAudioWindows*	psAudio;
	uint32				i;

	psAudio = (SHostAudioWindows*)mpvNative;
	if (psAudio)
	{
		waveOutReset(psAudio->hWaveOut);
		for (i = 0; i < HOST_AUDIO_BUFFERS; i++)
		{
			waveOutUnprepareHeader(psAudio->hWaveOut, &psAudio->asHeaders[i], sizeof(WAVEHDR));
		}
		waveOutClose(psAudio->hWaveOut);
		free(psAudio);
		mpvNative = NULL;
	}
	muiSampleRate = 0;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHostAudio::Write(uint8* puiFrames, uint32 uiFrames)
{
	SHostAudioWindows*	psAudio;
	WAVEHDR*			psHeader;
	uint32				uiCopy;

	psAudio = (SHostAudioWindows*)mpvNative;
	if (psAudio == NULL)
	{
		muiDroppedFrames += uiFrames;
		return;
	}

	while (uiFrames > 0)
	{
		psHeader = &psAudio->asHeaders[psAudio->uiCurrent];
		if (psHeader->dwFlags & WHDR_INQUEUE)
		{
			muiDroppedFrames += uiFrames;
			return;
		}

		uiCopy = HOST_AUDIO_BUFFER_FRAMES - psAudio->uiFilled;
		if (uiCopy > uiFrames)
		{
			uiCopy = uiFrames;
		}
		memcpy(&psAudio->aauiData[psAudio->uiCurrent][psAudio->uiFilled * 2], puiFrames, uiCopy * 2);
		psAudio->uiFilled += uiCopy;
		puiFrames += uiCopy * 2;
		uiFrames -= uiCopy;

		if (psAudio->uiFilled == HOST_AUDIO_BUFFER_FRAMES)
		{
			waveOutWrite(psAudio->hWaveOut, psHeader, sizeof(WAVEHDR));
			psAudio->uiCurrent = (psAudio->uiCurrent + 1) % HOST_AUDIO_BUFFERS;
			psAudio->uiFilled = 0;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHostAudio::IsOpen(void) { return mpvNative != NULL; }
uint32 CHostAudio::GetSampleRate(void) { return muiSampleRate; }
uint64 CHostAudio::NumDroppedFrames(void) { return muiDroppedFrames; }
