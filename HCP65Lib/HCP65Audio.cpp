#include <stdlib.h>
#include <string.h>
#include "BaseLib/PointerFunctions.h"
#include "HCP65.h"
#include "HCP65Audio.h"


#define HCP65_AUDIO_ADDRESS_MASK	0x7FFF


//////////////////////////////////////////////////////////////////////////
//
// szIdentifier is put in the device ROM (the rest of it is erased flash).
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::Init(const char* szIdentifier)
{
	size	uiLength;

	CHCP65Device::Init(HCP65_LARGE_SLOT_SIZE);

	memset(mauiROM, 0xFF, HCP65_AUDIO_ROM_SIZE);
	uiLength = strlen(szIdentifier);
	if (uiLength >= HCP65_AUDIO_ROM_SIZE)
	{
		uiLength = HCP65_AUDIO_ROM_SIZE - 1;
	}
	memcpy(mauiROM, szIdentifier, uiLength);
	mauiROM[uiLength] = 0;

	mpuiRAMA = (uint8*)malloc(HCP65_AUDIO_RAM_SIZE);
	mpuiRAMB = (uint8*)malloc(HCP65_AUDIO_RAM_SIZE);
	memset(mpuiRAMA, 0x80, HCP65_AUDIO_RAM_SIZE);
	memset(mpuiRAMB, 0x80, HCP65_AUDIO_RAM_SIZE);
	mpuiFrames = (uint8*)malloc(HCP65_AUDIO_BUFFER_FRAMES * 2);
	muiFirstFrame = 0;
	muiNumFrames = 0;
	muiDroppedFrames = 0;

	muiStart = 0;
	muiEnd = HCP65_AUDIO_ADDRESS_MASK;
	muiInterruptAddress = 0;
	muiCrystalTick = 0;
	Reset();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::Kill(void)
{
	SafeFree(mpuiFrames);
	SafeFree(mpuiRAMB);
	SafeFree(mpuiRAMA);
	CHCP65Device::Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// Disabled, interrupt cleared and the divider and counter held at zero.
// The address latches are not reset.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::Reset(void)
{
	muiFlags = 0;
	mbEnabled = false;
	mbInterrupt = false;
	muiCounter = 0;
	muiCounterSnapshot = 0;
	muiDivider = 0;
}


//////////////////////////////////////////////////////////////////////////
//
// A16 selects the sample RAMs (A15 picks A or B).  Below that only the
// device ROM and registers are decoded.
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65Audio::Read(uint32 uiOffset)
{
	if (uiOffset >= HCP65_AUDIO_RAM_B)
	{
		return mpuiRAMB[uiOffset & HCP65_AUDIO_ADDRESS_MASK];
	}
	if (uiOffset >= HCP65_AUDIO_RAM_A)
	{
		return mpuiRAMA[uiOffset & HCP65_AUDIO_ADDRESS_MASK];
	}
	if (uiOffset < HCP65_AUDIO_REGISTERS)
	{
		return mauiROM[uiOffset];
	}
	if (uiOffset < 0x800)
	{
		return ReadRegister(uiOffset & 0x1F);
	}
	return 0xFF;
}


//////////////////////////////////////////////////////////////////////////
//
// The device ROM is flash; writes to it are ignored.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::Write(uint32 uiOffset, uint8 uiValue)
{
	if (uiOffset >= HCP65_AUDIO_RAM_B)
	{
		mpuiRAMB[uiOffset & HCP65_AUDIO_ADDRESS_MASK] = uiValue;
	}
	else if (uiOffset >= HCP65_AUDIO_RAM_A)
	{
		mpuiRAMA[uiOffset & HCP65_AUDIO_ADDRESS_MASK] = uiValue;
	}
	else if ((uiOffset >= HCP65_AUDIO_REGISTERS) && (uiOffset < 0x800))
	{
		WriteRegister(uiOffset & 0x1F, uiValue);
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Only the first of the four Stage B register groups is fitted.
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65Audio::ReadRegister(uint32 uiRegister)
{
	uint8	uiValue;

	switch (uiRegister)
	{
	case HCP65_AUDIO_FLAGS:
		uiValue = (uint8)(muiFlags & 0x3F);
		if (mbEnabled)
		{
			uiValue |= HCP65_AUDIO_FLAG_ENABLE;
		}
		if (mbInterrupt)
		{
			uiValue |= HCP65_AUDIO_FLAG_INTERRUPT;
		}
		return uiValue;

	case HCP65_AUDIO_START_LOW:
		muiCounterSnapshot = muiCounter;
		return (uint8)muiCounterSnapshot;

	case HCP65_AUDIO_START_HIGH:
		return (uint8)(muiCounterSnapshot >> 8);
	}
	return 0xFF;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::WriteRegister(uint32 uiRegister, uint8 uiValue)
{
	switch (uiRegister)
	{
	case HCP65_AUDIO_FLAGS:
		WriteFlags(uiValue);
		break;
	case HCP65_AUDIO_CLEAR_INTERRUPT:
		mbInterrupt = false;
		break;
	case HCP65_AUDIO_START_LOW:
		muiStart = SetLow(muiStart, uiValue);
		break;
	case HCP65_AUDIO_START_HIGH:
		muiStart = SetHigh(muiStart, uiValue);
		break;
	case HCP65_AUDIO_END_LOW:
		muiEnd = SetLow(muiEnd, uiValue);
		break;
	case HCP65_AUDIO_END_HIGH:
		muiEnd = SetHigh(muiEnd, uiValue);
		break;
	case HCP65_AUDIO_INTERRUPT_LOW:
		muiInterruptAddress = SetLow(muiInterruptAddress, uiValue);
		break;
	case HCP65_AUDIO_INTERRUPT_HIGH:
		muiInterruptAddress = SetHigh(muiInterruptAddress, uiValue);
		break;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The enable flip flop is held clear while the board is held in reset.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::WriteFlags(uint8 uiValue)
{
	bool	bEnable;

	muiFlags = (uint8)(uiValue & ~HCP65_AUDIO_FLAG_ENABLE);
	bEnable = ((uiValue & HCP65_AUDIO_FLAG_ENABLE) != 0) && !mbHeldInReset;

	if (bEnable && !mbEnabled)
	{
		muiCounter = muiStart;
		muiDivider = 0;
	}
	else if (!bEnable)
	{
		muiCounter = 0;
		muiDivider = 0;
	}
	mbEnabled = bEnable;

	if (!mbEnabled || !(muiFlags & HCP65_AUDIO_FLAG_INTERRUPT))
	{
		mbInterrupt = false;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Whether the divider has just made a rising edge of the selected sample
// clock: every crystal tick at 32768Hz, otherwise a rising edge of divider
// bit (rate - 1).
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65Audio::IsSampleEdge(void)
{
	uint8	uiRate;
	uint8	uiMask;

	uiRate = (uint8)(muiFlags & HCP65_AUDIO_FLAG_RATE);
	if (uiRate == 0)
	{
		return true;
	}
	uiMask = (uint8)((1 << uiRate) - 1);
	return (muiDivider & uiMask) == (1 << (uiRate - 1));
}


//////////////////////////////////////////////////////////////////////////
//
// One tick of the sample clock.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::Sample(void)
{
	AddFrame(mpuiRAMA[muiCounter], mpuiRAMB[muiCounter]);

	if (muiCounter == muiEnd)
	{
		muiCounter = muiStart;
	}
	else
	{
		muiCounter = (uint16)((muiCounter + 1) & HCP65_AUDIO_ADDRESS_MASK);
	}

	if ((muiCounter == muiInterruptAddress) && (muiFlags & HCP65_AUDIO_FLAG_INTERRUPT))
	{
		mbInterrupt = true;
	}
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::CatchUp(uint64 uiCycle)
{
	uint64	uiTick;

	if (!mpcHCP65)
	{
		return;
	}

	uiTick = mpcHCP65->CrystalTick(uiCycle);
	if (!mbEnabled)
	{
		muiCrystalTick = uiTick;
		return;
	}

	while (muiCrystalTick < uiTick)
	{
		muiCrystalTick++;
		muiDivider = (uint8)((muiDivider + 1) & 7);
		if (IsSampleEdge())
		{
			Sample();
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Sample ticks until the counter reaches the interrupt address, or 0 if it
// never will: it runs up from where it is to End, then loops Start to End.
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65Audio::SamplesUntilInterrupt(void)
{
	uint32	uiToEnd;
	uint32	uiToTarget;
	uint32	uiLoop;

	uiToEnd = (uint32)((muiEnd - muiCounter) & HCP65_AUDIO_ADDRESS_MASK);
	uiToTarget = (uint32)((muiInterruptAddress - muiCounter) & HCP65_AUDIO_ADDRESS_MASK);
	if ((uiToTarget != 0) && (uiToTarget <= uiToEnd))
	{
		return uiToTarget;
	}

	uiLoop = (uint32)((muiEnd - muiStart) & HCP65_AUDIO_ADDRESS_MASK);
	uiToTarget = (uint32)((muiInterruptAddress - muiStart) & HCP65_AUDIO_ADDRESS_MASK);
	if (uiToTarget <= uiLoop)
	{
		return uiToEnd + 1 + uiToTarget;
	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////
//
// The CPU cycle of the sample tick that will latch the interrupt.
//
//////////////////////////////////////////////////////////////////////////
uint64 CHCP65Audio::NextEvent(void)
{
	uint32	uiSamples;
	uint8	uiRate;
	uint32	uiPeriod;
	uint32	uiFirst;
	uint8	uiMask;

	if (!mpcHCP65 || !mbEnabled || mbInterrupt || !(muiFlags & HCP65_AUDIO_FLAG_INTERRUPT))
	{
		return HCP65_NEVER;
	}

	uiSamples = SamplesUntilInterrupt();
	if (uiSamples == 0)
	{
		return HCP65_NEVER;
	}

	uiRate = (uint8)(muiFlags & HCP65_AUDIO_FLAG_RATE);
	uiPeriod = 1u << uiRate;
	if (uiRate == 0)
	{
		uiFirst = 1;
	}
	else
	{
		uiMask = (uint8)(uiPeriod - 1);
		uiFirst = (uint32)(((1 << (uiRate - 1)) - (muiDivider & uiMask)) & uiMask);
		if (uiFirst == 0)
		{
			uiFirst = uiPeriod;
		}
	}
	return mpcHCP65->CycleOfCrystalTick(muiCrystalTick + uiFirst + (uint64)(uiSamples - 1) * uiPeriod);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65Audio::IsInterrupting(void)
{
	return mbInterrupt;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Audio::AddFrame(uint8 uiLeft, uint8 uiRight)
{
	uint32	uiIndex;

	if (muiNumFrames == HCP65_AUDIO_BUFFER_FRAMES)
	{
		muiFirstFrame = (muiFirstFrame + 1) % HCP65_AUDIO_BUFFER_FRAMES;
		muiNumFrames--;
		muiDroppedFrames++;
	}
	uiIndex = (muiFirstFrame + muiNumFrames) % HCP65_AUDIO_BUFFER_FRAMES;
	mpuiFrames[uiIndex * 2] = uiLeft;
	mpuiFrames[uiIndex * 2 + 1] = uiRight;
	muiNumFrames++;
}


//////////////////////////////////////////////////////////////////////////
//
// Takes up to uiMaxFrames left/right pairs out of the buffer.
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65Audio::ReadFrames(uint8* puiDest, uint32 uiMaxFrames)
{
	uint32	uiCount;
	uint32	i;

	uiCount = (uiMaxFrames < muiNumFrames) ? uiMaxFrames : muiNumFrames;
	for (i = 0; i < uiCount; i++)
	{
		puiDest[i * 2] = mpuiFrames[muiFirstFrame * 2];
		puiDest[i * 2 + 1] = mpuiFrames[muiFirstFrame * 2 + 1];
		muiFirstFrame = (muiFirstFrame + 1) % HCP65_AUDIO_BUFFER_FRAMES;
	}
	muiNumFrames -= uiCount;
	return uiCount;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65Audio::NumFrames(void)
{
	return muiNumFrames;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint64 CHCP65Audio::NumDroppedFrames(void)
{
	return muiDroppedFrames;
}


//////////////////////////////////////////////////////////////////////////
//
// In Hz.
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65Audio::GetSampleRate(void)
{
	return HCP65_CRYSTAL_RATE >> (muiFlags & HCP65_AUDIO_FLAG_RATE);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8* CHCP65Audio::GetRAMA(void)
{
	return mpuiRAMA;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8* CHCP65Audio::GetRAMB(void)
{
	return mpuiRAMB;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint16 CHCP65Audio::GetCounter(void)
{
	return muiCounter;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65Audio::IsEnabled(void)
{
	return mbEnabled;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint16 CHCP65Audio::SetLow(uint16 uiWord, uint8 uiValue)
{
	return (uint16)(((uiWord & 0xFF00) | uiValue) & HCP65_AUDIO_ADDRESS_MASK);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint16 CHCP65Audio::SetHigh(uint16 uiWord, uint8 uiValue)
{
	return (uint16)(((uiWord & 0x00FF) | ((uint16)uiValue << 8)) & HCP65_AUDIO_ADDRESS_MASK);
}
