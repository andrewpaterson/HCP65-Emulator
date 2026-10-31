#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/Logger.h"
#include "HostAudio.h"


//PulseAudio's simple API (pulse/simple.h), declared here and loaded from libpulse-simple.so.0 when the audio is
//opened: only the library is needed, not its development package.
#define PULSE_STREAM_PLAYBACK		1		//PA_STREAM_PLAYBACK.
#define PULSE_SAMPLE_U8				0		//PA_SAMPLE_U8.
#define PULSE_DEFAULT				0xFFFFFFFF	//(uint32_t)-1: the server's choice.
#define HOST_AUDIO_LATENCY_MS		250		//How much the server buffers.

struct SPulseSampleSpec
{
	int		iFormat;
	uint32	uiRate;
	uint8	uiChannels;
};

struct SPulseBufferAttr
{
	uint32	uiMaxLength;
	uint32	uiTargetLength;
	uint32	uiPreBuffer;
	uint32	uiMinimumRequest;
	uint32	uiFragmentSize;
};

typedef void*	(*PulseSimpleNew)(const char* szServer, const char* szName, int iDirection, const char* szDevice, const char* szStreamName, const SPulseSampleSpec* psSpec, const void* pvChannelMap, const SPulseBufferAttr* psAttributes, int* piError);
typedef int		(*PulseSimpleWrite)(void* pvSimple, const void* pvData, size_t uiBytes, int* piError);
typedef void	(*PulseSimpleFree)(void* pvSimple);


struct SHostAudioLinux
{
	void*				pvLibrary;
	void*				pvSimple;
	PulseSimpleWrite	fWrite;
	PulseSimpleFree		fFree;
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
// A PulseAudio playback stream of uiSampleRate 8 bit stereo frames.
//
//////////////////////////////////////////////////////////////////////////
bool CHostAudio::Open(uint32 uiSampleRate)
{
	SHostAudioLinux*	psAudio;
	PulseSimpleNew		fNew;
	SPulseSampleSpec	sSpec;
	SPulseBufferAttr	sAttributes;
	int					iError;

	Close();
	psAudio = (SHostAudioLinux*)malloc(sizeof(SHostAudioLinux));
	memset(psAudio, 0, sizeof(SHostAudioLinux));
	psAudio->pvLibrary = dlopen("libpulse-simple.so.0", RTLD_NOW);
	if (psAudio->pvLibrary == NULL)
	{
		gcLogger.Error2(__METHOD__, " Could not load libpulse-simple.so.0: there is no sound.", NULL);
		free(psAudio);
		return false;
	}
	fNew = (PulseSimpleNew)dlsym(psAudio->pvLibrary, "pa_simple_new");
	psAudio->fWrite = (PulseSimpleWrite)dlsym(psAudio->pvLibrary, "pa_simple_write");
	psAudio->fFree = (PulseSimpleFree)dlsym(psAudio->pvLibrary, "pa_simple_free");
	if ((fNew == NULL) || (psAudio->fWrite == NULL) || (psAudio->fFree == NULL))
	{
		gcLogger.Error2(__METHOD__, " libpulse-simple.so.0 doesn't have the simple API: there is no sound.", NULL);
		dlclose(psAudio->pvLibrary);
		free(psAudio);
		return false;
	}

	sSpec.iFormat = PULSE_SAMPLE_U8;
	sSpec.uiRate = uiSampleRate;
	sSpec.uiChannels = 2;
	sAttributes.uiMaxLength = PULSE_DEFAULT;
	sAttributes.uiTargetLength = (uiSampleRate * 2 * HOST_AUDIO_LATENCY_MS) / 1000;
	sAttributes.uiPreBuffer = PULSE_DEFAULT;
	sAttributes.uiMinimumRequest = PULSE_DEFAULT;
	sAttributes.uiFragmentSize = PULSE_DEFAULT;
	iError = 0;
	psAudio->pvSimple = fNew(NULL, "HCP65 Emulator", PULSE_STREAM_PLAYBACK, NULL, "HCP65 Sound Board", &sSpec, NULL, &sAttributes, &iError);
	if (psAudio->pvSimple == NULL)
	{
		gcLogger.Error2(__METHOD__, " Could not open a PulseAudio stream: there is no sound.", NULL);
		dlclose(psAudio->pvLibrary);
		free(psAudio);
		return false;
	}

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
	SHostAudioLinux*	psAudio;

	psAudio = (SHostAudioLinux*)mpvNative;
	if (psAudio)
	{
		psAudio->fFree(psAudio->pvSimple);
		dlclose(psAudio->pvLibrary);
		free(psAudio);
		mpvNative = NULL;
	}
	muiSampleRate = 0;
}


//////////////////////////////////////////////////////////////////////////
//
// pa_simple_write waits only when the server's buffer is full: the
// emulator makes frames as fast as they play.
//
//////////////////////////////////////////////////////////////////////////
void CHostAudio::Write(uint8* puiFrames, uint32 uiFrames)
{
	SHostAudioLinux*	psAudio;
	int					iError;

	psAudio = (SHostAudioLinux*)mpvNative;
	if (psAudio == NULL)
	{
		muiDroppedFrames += uiFrames;
		return;
	}
	if (psAudio->fWrite(psAudio->pvSimple, puiFrames, (size_t)uiFrames * 2, &iError) < 0)
	{
		muiDroppedFrames += uiFrames;
	}
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHostAudio::IsOpen(void) { return mpvNative != NULL; }
uint32 CHostAudio::GetSampleRate(void) { return muiSampleRate; }
uint64 CHostAudio::NumDroppedFrames(void) { return muiDroppedFrames; }
