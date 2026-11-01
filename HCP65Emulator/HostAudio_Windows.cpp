#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/Logger.h"
#include "HostAudio.h"


//waveOut with a ring of buffers: Write fills the current one and queues it when it is full.  A buffer still queued
//(playing or waiting to) isn't refilled: frames that don't fit are dropped.
//
//The emulator makes frames only as fast as they play, in bursts once a tick, so the output is paused until
//HOST_AUDIO_LEAD_BUFFERS are queued (as PulseAudio's pre-buffer does on Linux).  Started as soon as one buffer was
//queued it would never be more than a buffer ahead and would run dry whenever a tick came late.  If it does run dry
//it is paused again until the lead is back.
#define HOST_AUDIO_BUFFERS			16
#define HOST_AUDIO_LEAD_BUFFERS		8
#define HOST_AUDIO_BUFFERS_A_SECOND	32		//A buffer is 1/32 of a second, the lead 1/4 of a second.

struct SHostAudioWindows
{
	HWAVEOUT	hWaveOut;
	WAVEHDR		asHeaders[HOST_AUDIO_BUFFERS];
	uint8*		puiData;		//HOST_AUDIO_BUFFERS buffers of uiBufferFrames left/right pairs.
	uint32		uiBufferFrames;
	uint32		uiCurrent;
	uint32		uiFilled;		//Frames in the current buffer.
	bool		bPlaying;		//False while paused, waiting for the lead.
};


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
static uint32 NumQueuedBuffers(SHostAudioWindows* psAudio)
{
	uint32	i;
	uint32	uiQueued;

	uiQueued = 0;
	for (i = 0; i < HOST_AUDIO_BUFFERS; i++)
	{
		if (((volatile DWORD&)psAudio->asHeaders[i].dwFlags) & WHDR_INQUEUE)
		{
			uiQueued++;
		}
	}
	return uiQueued;
}


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
	waveOutPause(psAudio->hWaveOut);

	psAudio->uiBufferFrames = uiSampleRate / HOST_AUDIO_BUFFERS_A_SECOND;
	if (psAudio->uiBufferFrames < 64)
	{
		psAudio->uiBufferFrames = 64;
	}
	psAudio->puiData = (uint8*)malloc(HOST_AUDIO_BUFFERS * psAudio->uiBufferFrames * 2);
	for (i = 0; i < HOST_AUDIO_BUFFERS; i++)
	{
		psAudio->asHeaders[i].lpData = (LPSTR)&psAudio->puiData[i * psAudio->uiBufferFrames * 2];
		psAudio->asHeaders[i].dwBufferLength = psAudio->uiBufferFrames * 2;
		waveOutPrepareHeader(psAudio->hWaveOut, &psAudio->asHeaders[i], sizeof(WAVEHDR));
	}
	psAudio->uiCurrent = 0;
	psAudio->uiFilled = 0;
	psAudio->bPlaying = false;

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
		free(psAudio->puiData);
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
	uint32				uiQueued;

	psAudio = (SHostAudioWindows*)mpvNative;
	if (psAudio == NULL)
	{
		muiDroppedFrames += uiFrames;
		return;
	}

	uiQueued = NumQueuedBuffers(psAudio);
	if (psAudio->bPlaying && (uiQueued == 0))
	{
		//Ran dry: wait for the lead again rather than playing each buffer as it fills.
		waveOutPause(psAudio->hWaveOut);
		psAudio->bPlaying = false;
	}

	while (uiFrames > 0)
	{
		psHeader = &psAudio->asHeaders[psAudio->uiCurrent];
		if (((volatile DWORD&)psHeader->dwFlags) & WHDR_INQUEUE)
		{
			muiDroppedFrames += uiFrames;
			break;
		}

		uiCopy = psAudio->uiBufferFrames - psAudio->uiFilled;
		if (uiCopy > uiFrames)
		{
			uiCopy = uiFrames;
		}
		memcpy(&psHeader->lpData[psAudio->uiFilled * 2], puiFrames, uiCopy * 2);
		psAudio->uiFilled += uiCopy;
		puiFrames += uiCopy * 2;
		uiFrames -= uiCopy;

		if (psAudio->uiFilled == psAudio->uiBufferFrames)
		{
			waveOutWrite(psAudio->hWaveOut, psHeader, sizeof(WAVEHDR));
			uiQueued++;
			psAudio->uiCurrent = (psAudio->uiCurrent + 1) % HOST_AUDIO_BUFFERS;
			psAudio->uiFilled = 0;
		}
	}

	if (!psAudio->bPlaying && (uiQueued >= HOST_AUDIO_LEAD_BUFFERS))
	{
		waveOutRestart(psAudio->hWaveOut);
		psAudio->bPlaying = true;
	}
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHostAudio::IsOpen(void) { return mpvNative != NULL; }
uint32 CHostAudio::GetSampleRate(void) { return muiSampleRate; }
uint64 CHostAudio::NumDroppedFrames(void) { return muiDroppedFrames; }
