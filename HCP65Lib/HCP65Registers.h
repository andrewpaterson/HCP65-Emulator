#ifndef __HCP65_REGISTERS_H__
#define __HCP65_REGISTERS_H__
#include "HCP65Device.h"


#define HCP65_REGISTER_DEVICE_INTERRUPT_LOW		0	//Read: D1-D7 = devices 1-7, 0 = interrupting.
#define HCP65_REGISTER_DEVICE_INTERRUPT_HIGH	2	//Read: D0-D3 = devices 16-19, 0 = interrupting.
#define HCP65_REGISTER_DEVICE_ENABLE_LOW		3	//D1-D7 = devices 1-7, 0 = held in reset.
#define HCP65_REGISTER_DEVICE_ENABLE_HIGH		5	//D0-D3 = devices 16-19, 0 = held in reset.
#define HCP65_REGISTER_TIMER_SPEED				6	//D0-D2.
#define HCP65_REGISTER_NMI_STATE				7
#define HCP65_REGISTER_PROCESS_ID				8
#define HCP65_REGISTER_INTERRUPT_DEPTH			9
#define HCP65_REGISTER_BOARD_STATE				10
#define HCP65_REGISTER_CLEAR_COUNTER			11
#define HCP65_REGISTER_COUNT_0					12
#define HCP65_REGISTER_COUNT_1					13
#define HCP65_REGISTER_COUNT_2					14
#define HCP65_REGISTER_COUNT_3					15


#define HCP65_NMI_TIMER			0x01
#define HCP65_NMI_BREAK_1		0x02
#define HCP65_NMI_BREAK_2		0x04
#define HCP65_NMI_BREAK_3		0x08
#define HCP65_NMI_SOURCES		0x0F
#define HCP65_NMI_ENABLED		0x10
#define HCP65_NMI_DISABLE		0x20


//The processor board's 16 registers, device slot 0 (HCP65 MPU Address Decode, MPU NMI, MPU Timer, Device
//Interrupts, Device Reset, Kernal Mode, Board Presence and Instruction Code sheets).  Only A0-A3 are
//decoded so they repeat every 16 bytes.  Data bits no chip drives read as 0.
//
//The NMI timer counts the 32.768kHz crystal once the processor has first been in native mode, and latches
//its NMI source every 2^(21 - 2 * speed) crystal ticks.
class CHCP65Registers : public CHCP65Device
{
protected:
	uint8		muiTimerSpeed;
	uint8		muiNMIState;		//HCP65_NMI_* sources and enabled.
	uint8		muiMPUID;			//0 to 3, read back in D6 and D7 of NMI State.
	uint64		muiTimerStart;		//The crystal tick the timer started counting from, HCP65_NEVER if held.
	uint64		muiTimerTick;		//The crystal tick the timer has been caught up to.
	uint16		muiBoardPresence;	//S0-S11.
	bool		mbBoardStateHigh;	//The next Board State access is S6-S11.
	uint32		muiCountSnapshot;

public:
	void	Init(void);
	void	Kill(void) override;

	uint8	Read(uint32 uiOffset) override;
	void	Write(uint32 uiOffset, uint8 uiValue) override;
	void	Reset(void) override;

	void	CatchUp(uint64 uiCycle) override;
	uint64	NextEvent(void) override;

	void	StartTimer(uint64 uiCycle);
	void	SetBreak(int iBreak);
	bool	IsNMILow(void);
	void	SetMPUID(uint8 uiMPUID);
	void	SetBoardPresence(uint16 uiPresence);

protected:
	uint64	TimerPeriod(void);
	uint8	ReadInterrupts(int iFirst, int iLast, int iFirstBit);
};


#endif // __HCP65_REGISTERS_H__
