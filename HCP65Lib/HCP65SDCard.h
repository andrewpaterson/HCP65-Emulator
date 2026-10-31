#ifndef __HCP65_SD_CARD_H__
#define __HCP65_SD_CARD_H__
#include "BaseLib/Chars.h"
#include "HCP65Device.h"


#define HCP65_SD_ROM_SIZE				0x400
#define HCP65_SD_NUM_SOCKETS			4

//Registers, from $400.  They repeat every 32 bytes; $410-$41F decode nothing.
#define HCP65_SD_ADDRESS_LOW			0x00	//W: DMA main memory address bits 0-7.
#define HCP65_SD_ADDRESS_HIGH			0x01	//W: bits 8-15.
#define HCP65_SD_ADDRESS_BANK			0x02	//W: bits 16-23.
#define HCP65_SD_GPIO					0x04	//W: D0-D3 levels, D4-D7 output enables.  R: D0-D3 pin levels.
#define HCP65_SD_DATA_COUNT_LOW			0x06	//W: bytes to store, bits 0-7.
#define HCP65_SD_DATA_COUNT_HIGH		0x07	//W: bits 8-11.
#define HCP65_SD_CRC_COUNT_LOW			0x08	//W: CRC bytes to clock through, bits 0-7.
#define HCP65_SD_CRC_COUNT_HIGH			0x09	//W: bits 8-11.
#define HCP65_SD_CLEAR_INTERRUPT		0x0A	//W.
#define HCP65_SD_READ_START				0x0B	//W: starts a block read.
#define HCP65_SD_FLAGS					0x0C	//R/W.
#define HCP65_SD_WRITE_DATA_LOW			0x0E	//W: one byte of the write stream (two nybbles, high first).
#define HCP65_SD_WRITE_DATA_HIGH		0x0F	//W: the same.

#define HCP65_SD_GPIO_CLK				0x01
#define HCP65_SD_GPIO_CMD				0x02
#define HCP65_SD_GPIO_DAT0				0x04
#define HCP65_SD_GPIO_DAT3				0x08

#define HCP65_SD_FLAG_CARD				0x03	//The socket, 0 to 3.
#define HCP65_SD_FLAG_ENABLE			0x40
#define HCP65_SD_FLAG_INTERRUPT			0x80	//W: interrupt enable.  R: interrupt pending.


enum EHCP65SDCardState
{
	HSDCS_Idle,
	HSDCS_Ready,
	HSDCS_Identification,
	HSDCS_StandBy,
	HSDCS_Transfer,
	HSDCS_SendingData,
	HSDCS_ReceivingData,
};


//The HCP65 SD Card Board (SD Card sheets), with an SD card (SDHC, block addressed) whose contents are an image file
//in memory.  It is a 32KB device.
//
//The board as it is meant to work (the schematics have faults, see the SD Card sheets' notes):
//  - Commands are bit banged through the GPIO register: SD CLK, CMD, DAT0 and DAT3.  The card samples CMD on SD CLK's
//    rising edge and changes its outputs (the CMD response, DAT0's CRC status and busy) on the falling edge.
//  - A block read is started by Read Start after the read command.  The board clocks the card and stores Data Count
//    bytes at the DMA address in main memory, clocks the CRC bytes through, then stops and latches its interrupt
//    (Flags D7).  Here the bytes are stored when the transfer ends, HCP65_SD_CLOCKS_PER_BYTE SD clocks per byte at
//    the CPU's clock rate after Read Start.
//  - A block write is a stream of bytes stored to Write Data: the start nybble (0), 1024 data nybbles, 16 CRC nybbles
//    (CRC16 of each DAT line) and the end nybble (F), high nybble first: 521 bytes.  Then the card's CRC status and
//    busy are clocked out on DAT0 through the GPIO register.
//The card checks the CRC7 of each command (and ignores a command with a bad one) and the CRC16s of written data.
class CHCP65SDCard : public CHCP65Device
{
protected:
	uint8				mauiROM[HCP65_SD_ROM_SIZE];

	//The board.
	uint8				muiFlags;
	bool				mbEnabled;
	bool				mbInterrupt;
	uint8				muiGPIO;
	uint32				muiAddress;
	uint16				muiDataCount;
	uint16				muiCRCCount;
	bool				mbReading;
	uint64				muiReadDone;		//The CPU cycle the board's read finishes.
	uint8				muiWriteNybbles[1042];
	uint16				muiNumWriteNybbles;
	bool				mbPreviousCLK;

	//The card in socket 0 (the other sockets are empty).
	uint8*				mpuiImage;
	uint32				muiNumSectors;
	bool				mbWriteBack;
	CChars				mszFilename;
	EHCP65SDCardState	meState;
	bool				mbApplicationCommand;
	uint16				muiRCA;
	uint32				muiStatus;			//The card status (R1's 32 bits).
	bool				mbWideBus;
	uint8				muiCommand[6];
	uint8				muiCommandBits;
	uint8				mauiResponse[17];
	uint8				muiResponseBits;	//Bits in mauiResponse to send, including the start bit and end bit.
	uint8				muiResponseSent;
	uint8				muiResponseDelay;	//Clocks of CMD high before the response's start bit.
	uint32				muiReadSector;		//The sector the last read command asked for.
	bool				mbReadPending;
	uint32				muiWriteSector;
	bool				mbWritePending;
	uint8				muiDAT0Bits[16];	//What the card drives on DAT0 after a write, one bit per falling edge.
	uint8				muiNumDAT0Bits;
	uint8				muiDAT0Sent;
	uint32				muiSectorsRead;
	uint32				muiSectorsWritten;

public:
	void	Init(const char* szIdentifier = "HCP65 SD Card Board");
	void	Kill(void) override;

	bool	InsertCard(const char* szImageFilename, bool bWriteBack);
	bool	InsertCard(uint8* puiImage, uint32 uiSize);
	void	RemoveCard(void);
	bool	Flush(void);

	uint8	Read(uint32 uiOffset) override;
	void	Write(uint32 uiOffset, uint8 uiValue) override;
	void	Reset(void) override;

	void	CatchUp(uint64 uiCycle) override;
	uint64	NextEvent(void) override;
	bool	IsInterrupting(void) override;

	uint8*	GetImage(void);
	uint32	NumSectors(void);
	uint32	NumSectorsRead(void);
	uint32	NumSectorsWritten(void);

	static uint8	CRC7(uint8* puiData, uint32 uiLength);
	static uint16	CRC16(uint8* puiBits, uint32 uiLength);

protected:
	uint8	ReadRegister(uint32 uiRegister);
	void	WriteRegister(uint32 uiRegister, uint8 uiValue);
	void	WriteFlags(uint8 uiValue);
	void	WriteGPIO(uint8 uiValue);
	bool	HasCard(void);
	uint8	CardDrivenLines(void);
	void	RisingEdge(void);
	void	FallingEdge(void);
	void	ReceiveCommandBit(bool bBit);
	void	Command(uint8 uiIndex, uint32 uiArgument);
	void	ApplicationCommand(uint8 uiIndex, uint32 uiArgument);
	void	RespondR1(uint8 uiIndex);
	void	Respond48(uint8 uiIndex, uint32 uiContent, bool bCRC);
	void	RespondR2(uint8* puiRegister);
	void	StartRead(void);
	void	FinishRead(void);
	void	WriteStream(uint8 uiValue);
	void	FinishWrite(void);
	uint32	StateBits(void);
};


#endif // __HCP65_SD_CARD_H__
