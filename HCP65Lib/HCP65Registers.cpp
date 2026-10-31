#include "HCP65.h"
#include "HCP65Registers.h"


//////////////////////////////////////////////////////////////////////////
//
// Every optional processor sub-board is fitted (S0 timer, S4 kernel mode...).
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::Init(void)
{
	CHCP65Device::Init(HCP65_SMALL_SLOT_SIZE);
	muiMPUID = 0;
	muiBoardPresence = 0xFFF;
	Reset();
	mbHeldInReset = false;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::Kill(void)
{
	CHCP65Device::Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// The timer is held until the processor first goes native.  NMIs are enabled.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::Reset(void)
{
	muiTimerSpeed = 0;
	muiNMIState = HCP65_NMI_ENABLED;
	muiTimerStart = HCP65_NEVER;
	muiTimerTick = 0;
	mbBoardStateHigh = false;
	muiCountSnapshot = 0;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65Registers::Read(uint32 uiOffset)
{
	uint8	uiValue;
	uint32	uiCount;

	switch (uiOffset & 0xF)
	{
	case HCP65_REGISTER_DEVICE_INTERRUPT_LOW:
		return ReadInterrupts(1, 7, 1);

	case HCP65_REGISTER_DEVICE_INTERRUPT_HIGH:
		return ReadInterrupts(16, 19, 0);

	case HCP65_REGISTER_DEVICE_ENABLE_LOW:
		return (uint8)((mpcHCP65->GetDeviceEnables() & 0xFE));

	case HCP65_REGISTER_DEVICE_ENABLE_HIGH:
		return (uint8)((mpcHCP65->GetDeviceEnables() >> 16) & 0x0F);

	case HCP65_REGISTER_TIMER_SPEED:
		return muiTimerSpeed;

	case HCP65_REGISTER_NMI_STATE:
		return (uint8)((muiNMIState & (HCP65_NMI_SOURCES | HCP65_NMI_ENABLED)) | (muiMPUID << 6));

	case HCP65_REGISTER_PROCESS_ID:
		return mpcHCP65->GetProcessID();

	case HCP65_REGISTER_INTERRUPT_DEPTH:
		return mpcHCP65->GetInterruptDepth();

	case HCP65_REGISTER_BOARD_STATE:
		if (mbBoardStateHigh)
		{
			uiValue = (uint8)(((muiBoardPresence >> 6) & 0x3F) << 2);
		}
		else
		{
			uiValue = (uint8)((muiBoardPresence & 0x3F) << 2);
		}
		mbBoardStateHigh = !mbBoardStateHigh;
		return uiValue;

	case HCP65_REGISTER_COUNT_0:
		uiCount = mpcHCP65->GetInstructionCount();
		muiCountSnapshot = uiCount;
		return (uint8)uiCount;

	case HCP65_REGISTER_COUNT_1:
		return (uint8)(muiCountSnapshot >> 8);

	case HCP65_REGISTER_COUNT_2:
		return (uint8)(muiCountSnapshot >> 16);

	case HCP65_REGISTER_COUNT_3:
		return (uint8)(muiCountSnapshot >> 24);
	}

	//Registers 1, 4 and 11 drive nothing when read.
	return 0xFF;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::Write(uint32 uiOffset, uint8 uiValue)
{
	uint32	uiEnables;

	switch (uiOffset & 0xF)
	{
	case HCP65_REGISTER_DEVICE_ENABLE_LOW:
		uiEnables = mpcHCP65->GetDeviceEnables();
		uiEnables = (uiEnables & ~0xFEu) | (uiValue & 0xFE);
		mpcHCP65->SetDeviceEnables(uiEnables);
		break;

	case HCP65_REGISTER_DEVICE_ENABLE_HIGH:
		uiEnables = mpcHCP65->GetDeviceEnables();
		uiEnables = (uiEnables & ~0xF0000u) | ((uint32)(uiValue & 0x0F) << 16);
		mpcHCP65->SetDeviceEnables(uiEnables);
		break;

	case HCP65_REGISTER_TIMER_SPEED:
		muiTimerSpeed = (uint8)(uiValue & 0x07);
		break;

	case HCP65_REGISTER_NMI_STATE:
		//A one clears a source, enables or disables; a zero does nothing.
		muiNMIState &= (uint8)~(uiValue & HCP65_NMI_SOURCES);
		if (uiValue & HCP65_NMI_DISABLE)
		{
			muiNMIState &= (uint8)~HCP65_NMI_ENABLED;
		}
		else if (uiValue & HCP65_NMI_ENABLED)
		{
			muiNMIState |= HCP65_NMI_ENABLED;
		}
		break;

	case HCP65_REGISTER_PROCESS_ID:
		mpcHCP65->SetProcessID(uiValue);
		break;

	case HCP65_REGISTER_INTERRUPT_DEPTH:
		mpcHCP65->SetInterruptDepth(uiValue);
		break;

	case HCP65_REGISTER_BOARD_STATE:
		mbBoardStateHigh = !mbBoardStateHigh;
		break;

	case HCP65_REGISTER_CLEAR_COUNTER:
	case HCP65_REGISTER_COUNT_0:
		mpcHCP65->ClearInstructionCount();
		break;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// D(iFirstBit + n) is device (iFirst + n)'s interrupt line, low when it is
// interrupting.
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65Registers::ReadInterrupts(int iFirst, int iLast, int iFirstBit)
{
	uint8	uiValue;
	int		i;

	uiValue = 0;
	for (i = iFirst; i <= iLast; i++)
	{
		if (!mpcHCP65->IsDeviceInterrupting(i))
		{
			uiValue |= (uint8)(1 << (iFirstBit + i - iFirst));
		}
	}
	return uiValue;
}


//////////////////////////////////////////////////////////////////////////
//
// Called when the processor first goes native (the Native Latch).
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::StartTimer(uint64 uiCycle)
{
	if (muiTimerStart == HCP65_NEVER)
	{
		muiTimerStart = mpcHCP65->CrystalTick(uiCycle);
		muiTimerTick = muiTimerStart;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// In crystal ticks.
//
//////////////////////////////////////////////////////////////////////////
uint64 CHCP65Registers::TimerPeriod(void)
{
	return 1ULL << (21 - 2 * muiTimerSpeed);
}


//////////////////////////////////////////////////////////////////////////
//
// The timer's NMI source is latched each time the timer's count reaches a
// multiple of its period.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::CatchUp(uint64 uiCycle)
{
	uint64	uiTick;
	uint64	uiPeriod;

	if (muiTimerStart == HCP65_NEVER)
	{
		return;
	}

	uiTick = mpcHCP65->CrystalTick(uiCycle);
	if (uiTick <= muiTimerTick)
	{
		return;
	}

	uiPeriod = TimerPeriod();
	if (((uiTick - muiTimerStart) / uiPeriod) != ((muiTimerTick - muiTimerStart) / uiPeriod))
	{
		muiNMIState |= HCP65_NMI_TIMER;
	}
	muiTimerTick = uiTick;
}


//////////////////////////////////////////////////////////////////////////
//
// The timer's next tick, even if its source is already latched; it is
// cheap and keeps the timer's time exact.
//
//////////////////////////////////////////////////////////////////////////
uint64 CHCP65Registers::NextEvent(void)
{
	uint64	uiPeriod;
	uint64	uiCount;

	if (muiTimerStart == HCP65_NEVER)
	{
		return HCP65_NEVER;
	}

	uiPeriod = TimerPeriod();
	uiCount = muiTimerTick - muiTimerStart;
	return mpcHCP65->CycleOfCrystalTick(muiTimerStart + (uiCount / uiPeriod + 1) * uiPeriod);
}


//////////////////////////////////////////////////////////////////////////
//
// iBreak is 1, 2 or 3: a falling edge on that break input.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::SetBreak(int iBreak)
{
	if ((iBreak >= 1) && (iBreak <= 3))
	{
		muiNMIState |= (uint8)(HCP65_NMI_TIMER << iBreak);
	}
}


//////////////////////////////////////////////////////////////////////////
//
// NMI is low while any source is latched and NMIs are enabled.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65Registers::IsNMILow(void)
{
	return (muiNMIState & HCP65_NMI_ENABLED) && (muiNMIState & HCP65_NMI_SOURCES);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::SetMPUID(uint8 uiMPUID)
{
	muiMPUID = (uint8)(uiMPUID & 3);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65Registers::SetBoardPresence(uint16 uiPresence)
{
	muiBoardPresence = (uint16)(uiPresence & 0xFFF);
}
