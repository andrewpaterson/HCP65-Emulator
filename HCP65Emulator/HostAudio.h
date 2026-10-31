#ifndef __HOST_AUDIO_H__
#define __HOST_AUDIO_H__
#include "BaseLib/PrimitiveTypes.h"


//Plays 8 bit unsigned stereo frames (left then right, as the HCP65's sound board gives them) on the host: waveOut on
//Windows (HostAudio_Windows.cpp), PulseAudio's simple API on Linux (HostAudio_Linux.cpp, loaded when it is opened so
//no development package is needed).  Write queues the frames and returns; frames that don't fit are dropped.
class CHostAudio
{
protected:
	void*	mpvNative;
	uint32	muiSampleRate;
	uint64	muiDroppedFrames;

public:
	void	Init(void);
	void	Kill(void);

	bool	Open(uint32 uiSampleRate);
	void	Close(void);
	bool	IsOpen(void);
	uint32	GetSampleRate(void);

	void	Write(uint8* puiFrames, uint32 uiFrames);
	uint64	NumDroppedFrames(void);
};


#endif // __HOST_AUDIO_H__
