#ifndef __HCP65_EMULATOR_TICK_H__
#define __HCP65_EMULATOR_TICK_H__
#include "WindowLib/WindowTick.h"


//The most of the HCP65's time one tick runs (when the host falls behind the HCP65 slows down rather than catching up).
#define HCP65_EMULATOR_MAX_TICK_MS		100
#define HCP65_EMULATOR_AUDIO_FRAMES		0x1000


class CHCP65;
class CHCP65Audio;
class CHostAudio;


//Each tick runs the HCP65 for the time since the last (at its clock rate), plays what its sound board played on the
//host and paints the window.
class CHCP65EmulatorTick : public CWindowTick
{
CONSTRUCTABLE(CHCP65EmulatorTick);
DESTRUCTABLE(CHCP65EmulatorTick);
protected:
	CHCP65*			mpcHCP65;
	CHCP65Audio*	mpcAudio;
	CHostAudio*		mpcHostAudio;
	int64			miStartTime;			//The window's time when the HCP65 started, -1 before the first tick.
	uint64			muiStartCycles;			//The HCP65's cycles then (they move on when the host falls behind).
	int64			miStopTime;				//HCP65_EMULATOR_SECONDS (environment): the window stops after it, or -1.
	int64			miReportTime;			//HCP65_EMULATOR_TRACE (environment): when the next report is printed, or -1.
	uint64			muiFramesPlayed;
	uint8			mauiFrames[HCP65_EMULATOR_AUDIO_FRAMES * 2];

public:
	void	Init(CHCP65* pcHCP65, CHCP65Audio* pcAudio, CHostAudio* pcHostAudio);
	void	Class(void) override;
	void	Free(void) override;
	void	Tick(Ptr<CWindow> pWindow, int64 iUpdateTimeInMillieconds, int64 iTotalTimeInMillieconds) override;

protected:
	void	Run(int64 iTotalTimeInMillieconds);
	void	PlayAudio(void);
	void	Report(int64 iTotalTimeInMillieconds);
};


#endif // __HCP65_EMULATOR_TICK_H__
