#ifndef __HCP65_AUDIO_H__
#define __HCP65_AUDIO_H__
#include "HCP65Device.h"


#define HCP65_AUDIO_ROM_SIZE			0x400		//The device identifier, $00000 to $003FF.
#define HCP65_AUDIO_REGISTERS			0x400		//$00400 to $007FF, eight registers repeating every 32 bytes.
#define HCP65_AUDIO_RAM_A				0x10000		//32KB of left samples.
#define HCP65_AUDIO_RAM_B				0x18000		//32KB of right samples.
#define HCP65_AUDIO_RAM_SIZE			0x8000

#define HCP65_AUDIO_FLAGS				0	//Read and write.
#define HCP65_AUDIO_CLEAR_INTERRUPT		1	//Write.
#define HCP65_AUDIO_START_LOW			2	//Write.  Read: counter low, which snapshots the counter.
#define HCP65_AUDIO_START_HIGH			3	//Write.  Read: the snapshot's high byte.
#define HCP65_AUDIO_END_LOW				4
#define HCP65_AUDIO_END_HIGH			5
#define HCP65_AUDIO_INTERRUPT_LOW		6
#define HCP65_AUDIO_INTERRUPT_HIGH		7

#define HCP65_AUDIO_FLAG_RATE			0x03	//0 = 32768Hz, 1 = 16384Hz, 2 = 8192Hz, 3 = 4096Hz.
#define HCP65_AUDIO_FLAG_ENABLE			0x40	//Write 1 to enable, 0 to disable.  Read: enabled.
#define HCP65_AUDIO_FLAG_INTERRUPT		0x80	//Write: interrupt enable.  Read: interrupt pending.

#define HCP65_AUDIO_BUFFER_FRAMES		0x10000


//The HCP65 Sound Board (Audio Board, Audio Address Decode, Audio Clock and Audio DAC sheets): two 32KB
//sample RAMs played together through an 8 bit stereo DAC (MAX5102), RAM A on the left and RAM B on the
//right.  It needs a 128KB device socket.
//
//On each tick of the sample clock (the 32.768kHz crystal divided by 1, 2, 4 or 8) the samples at the
//counter are written to the DAC and the counter moves on: from End back to Start, otherwise up by one.
//When the counter reaches the Interrupt address the interrupt is latched, if enabled, until Clear Interrupt
//is written, interrupts are disabled or the board is disabled.  Enabling the board loads the counter from
//Start, so playback starts with the sample at Start.  Addresses are 15 bits.
//
//The DAC's frames are kept in a buffer as unsigned 8 bit stereo pairs for the host to play.  Frames are
//only made while the board is enabled; when the buffer is full the oldest are dropped.
class CHCP65Audio : public CHCP65Device
{
protected:
	uint8		mauiROM[HCP65_AUDIO_ROM_SIZE];
	uint8*		mpuiRAMA;
	uint8*		mpuiRAMB;

	uint8		muiFlags;			//F0-F5 and interrupt enable as written.
	bool		mbEnabled;
	bool		mbInterrupt;
	uint16		muiStart;
	uint16		muiEnd;
	uint16		muiInterruptAddress;
	uint16		muiCounter;
	uint16		muiCounterSnapshot;
	uint8		muiDivider;			//The crystal divider, 0 to 7, held at 0 while disabled.
	uint64		muiCrystalTick;		//The crystal tick the board has been caught up to.

	uint8*		mpuiFrames;			//HCP65_AUDIO_BUFFER_FRAMES left/right pairs.
	uint32		muiFirstFrame;
	uint32		muiNumFrames;
	uint64		muiDroppedFrames;

public:
	void	Init(const char* szIdentifier = "HCP65 Sound Board");
	void	Kill(void) override;

	uint8	Read(uint32 uiOffset) override;
	void	Write(uint32 uiOffset, uint8 uiValue) override;
	void	Reset(void) override;

	void	CatchUp(uint64 uiCycle) override;
	uint64	NextEvent(void) override;
	bool	IsInterrupting(void) override;

	uint32	GetSampleRate(void);
	uint32	NumFrames(void);
	uint32	ReadFrames(uint8* puiDest, uint32 uiMaxFrames);
	uint64	NumDroppedFrames(void);
	uint8*	GetRAMA(void);
	uint8*	GetRAMB(void);
	uint16	GetCounter(void);
	bool	IsEnabled(void);

protected:
	uint8	ReadRegister(uint32 uiRegister);
	void	WriteRegister(uint32 uiRegister, uint8 uiValue);
	void	WriteFlags(uint8 uiValue);
	bool	IsSampleEdge(void);
	void	Sample(void);
	void	AddFrame(uint8 uiLeft, uint8 uiRight);
	uint32	SamplesUntilInterrupt(void);
	uint16	SetLow(uint16 uiWord, uint8 uiValue);
	uint16	SetHigh(uint16 uiWord, uint8 uiValue);
};


#endif // __HCP65_AUDIO_H__
