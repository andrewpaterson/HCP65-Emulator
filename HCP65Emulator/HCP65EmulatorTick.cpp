#include <stdio.h>
#include <stdlib.h>
#include "StandardLib/ClassDefines.h"
#include "WindowLib/Window.h"
#include "HCP65Lib/HCP65.h"
#include "HCP65Lib/HCP65Audio.h"
#include "HostAudio.h"
#include "HCP65EmulatorTick.h"


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::Init(CHCP65* pcHCP65, CHCP65Audio* pcAudio, CHostAudio* pcHostAudio)
{
	PreInit();

	CWindowTick::Init();
	mpcHCP65 = pcHCP65;
	mpcAudio = pcAudio;
	mpcHostAudio = pcHostAudio;
	miStartTime = -1;
	muiStartCycles = 0;
	miStopTime = getenv("HCP65_EMULATOR_SECONDS") ? (int64)atoi(getenv("HCP65_EMULATOR_SECONDS")) * 1000 : -1;
	miReportTime = getenv("HCP65_EMULATOR_TRACE") ? 0 : -1;
	muiFramesPlayed = 0;

	PostInit();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::Class(void)
{
	CWindowTick::Class();
	U_Pointer(mpcHCP65);
	U_Pointer(mpcAudio);
	U_Pointer(mpcHostAudio);
	U_Int64(miStartTime);
	U_UInt64(muiStartCycles);
	U_Int64(miStopTime);
	U_Int64(miReportTime);
	U_UInt64(muiFramesPlayed);
	U_UInt8Array(mauiFrames, HCP65_EMULATOR_AUDIO_FRAMES * 2);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::Free(void)
{
	CWindowTick::Free();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::Tick(Ptr<CWindow> pWindow, int64 iUpdateTimeInMillieconds, int64 iTotalTimeInMillieconds)
{
	if (mpcHCP65 && !mpcHCP65->IsStopped())
	{
		Run(iTotalTimeInMillieconds);
		PlayAudio();
	}
	if (miReportTime >= 0)
	{
		Report(iTotalTimeInMillieconds);
	}
	if ((miStopTime >= 0) && (iTotalTimeInMillieconds >= miStopTime))
	{
		pWindow->Stop();
	}
	pWindow->Paint();
}


//////////////////////////////////////////////////////////////////////////
//
// The HCP65 runs to where its clock should be by now, at most
// HCP65_EMULATOR_MAX_TICK_MS of its time.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::Run(int64 iTotalTimeInMillieconds)
{
	uint64	uiClockRate;
	uint64	uiTarget;
	uint64	uiCycles;
	uint64	uiMaxCycles;

	uiClockRate = mpcHCP65->GetClockRate();
	if (miStartTime < 0)
	{
		miStartTime = iTotalTimeInMillieconds;
		muiStartCycles = mpcHCP65->NumCycles();
	}

	uiTarget = muiStartCycles + (uiClockRate * (uint64)(iTotalTimeInMillieconds - miStartTime)) / 1000;
	if (uiTarget <= mpcHCP65->NumCycles())
	{
		return;
	}
	uiCycles = uiTarget - mpcHCP65->NumCycles();
	uiMaxCycles = (uiClockRate * HCP65_EMULATOR_MAX_TICK_MS) / 1000;
	if (uiCycles > uiMaxCycles)
	{
		//The host can't keep up: the HCP65's time falls behind the window's.
		muiStartCycles += uiCycles - uiMaxCycles;
		uiCycles = uiMaxCycles;
	}
	mpcHCP65->Run(uiCycles);
}


//////////////////////////////////////////////////////////////////////////
//
// What the sound board played since the last tick, to the host (opened
// at the board's sample rate, again if it changes).
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::PlayAudio(void)
{
	uint32	uiSampleRate;
	uint32	uiFrames;

	if ((mpcAudio == NULL) || (mpcHostAudio == NULL))
	{
		return;
	}

	uiSampleRate = mpcAudio->GetSampleRate();
	if ((uiSampleRate != 0) && (uiSampleRate != mpcHostAudio->GetSampleRate()))
	{
		mpcHostAudio->Open(uiSampleRate);
	}

	do
	{
		uiFrames = mpcAudio->ReadFrames(mauiFrames, HCP65_EMULATOR_AUDIO_FRAMES);
		if (uiFrames > 0)
		{
			mpcHostAudio->Write(mauiFrames, uiFrames);
			muiFramesPlayed += uiFrames;
		}
	}
	while (uiFrames == HCP65_EMULATOR_AUDIO_FRAMES);
}


//////////////////////////////////////////////////////////////////////////
//
// HCP65_EMULATOR_TRACE: once a second, how far the HCP65 has got and what
// it has played.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65EmulatorTick::Report(int64 iTotalTimeInMillieconds)
{
	if (iTotalTimeInMillieconds < miReportTime)
	{
		return;
	}
	miReportTime = iTotalTimeInMillieconds + 1000;
	if (mpcHCP65)
	{
		printf("%6.1f s: %llu cycles (%.1f s of HCP65 time), PC $%06X, process $%02X, sound %s at %u Hz, %llu frames played, %llu dropped by the board, %llu by the host\n",
			(double)iTotalTimeInMillieconds / 1000.0,
			(unsigned long long)mpcHCP65->NumCycles(),
			(double)mpcHCP65->NumCycles() / (double)mpcHCP65->GetClockRate(),
			mpcHCP65->GetMPU()->GetProgramAddress(),
			mpcHCP65->GetProcessID(),
			(mpcAudio && mpcAudio->IsEnabled()) ? "on" : "off",
			mpcAudio ? mpcAudio->GetSampleRate() : 0,
			(unsigned long long)muiFramesPlayed,
			mpcAudio ? (unsigned long long)mpcAudio->NumDroppedFrames() : 0ULL,
			mpcHostAudio ? (unsigned long long)mpcHostAudio->NumDroppedFrames() : 0ULL);
		fflush(stdout);
	}
}
