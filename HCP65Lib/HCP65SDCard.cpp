#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BaseLib/PointerFunctions.h"
#include "HCP65.h"
#include "HCP65SDCard.h"


#define HCP65_SD_SECTOR_SIZE		512
#define HCP65_SD_WRITE_NYBBLES		1042		//Start, 1024 data, 16 CRC and end nybbles.
#define HCP65_SD_ACCESS_CLOCKS		8			//SD clocks from Read Start to the data's start bit (Nac).
#define HCP65_SD_RESPONSE_DELAY		2			//SD clocks between a command's end bit and its response (Ncr).
#define HCP65_SD_RCA				0x1234

//Card status bits (R1).
#define HCP65_SD_OUT_OF_RANGE		0x80000000
#define HCP65_SD_COM_CRC_ERROR		0x00800000
#define HCP65_SD_ILLEGAL_COMMAND	0x00400000
#define HCP65_SD_READY_FOR_DATA		0x00000100
#define HCP65_SD_APP_CMD			0x00000020
#define HCP65_SD_CLEARED_ON_READ	(HCP65_SD_OUT_OF_RANGE | HCP65_SD_COM_CRC_ERROR | HCP65_SD_ILLEGAL_COMMAND)


static const uint8 gauiHCP65SDCID[15] = { 0x03, 'H', 'C', 'H', 'C', 'P', '6', '5', 0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0xA4 };


//////////////////////////////////////////////////////////////////////////
//
// The card slot starts empty.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::Init(const char* szIdentifier)
{
	size	uiLength;

	CHCP65Device::Init(HCP65_SMALL_SLOT_SIZE);
	memset(mauiROM, 0xFF, HCP65_SD_ROM_SIZE);
	uiLength = strlen(szIdentifier);
	if (uiLength >= HCP65_SD_ROM_SIZE)
	{
		uiLength = HCP65_SD_ROM_SIZE - 1;
	}
	memcpy(mauiROM, szIdentifier, uiLength);
	mauiROM[uiLength] = 0;

	mpuiImage = NULL;
	muiNumSectors = 0;
	mbWriteBack = false;
	mszFilename.Init();
	muiGPIO = 0;
	muiAddress = 0;
	muiDataCount = 0;
	muiCRCCount = 0;
	muiSectorsRead = 0;
	muiSectorsWritten = 0;
	Reset();
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::Kill(void)
{
	RemoveCard();
	mszFilename.Kill();
	CHCP65Device::Kill();
}


//////////////////////////////////////////////////////////////////////////
//
// The board is disabled; the card goes back to idle (a reset of the system
// doesn't power cycle the card, but the card is only used after CMD0).
// The GPIO latch and the address and count latches are not reset.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::Reset(void)
{
	muiFlags = 0;
	mbEnabled = false;
	mbInterrupt = false;
	mbReading = false;
	muiReadDone = 0;
	muiNumWriteNybbles = 0;
	mbPreviousCLK = true;

	meState = HSDCS_Idle;
	mbApplicationCommand = false;
	muiRCA = 0;
	muiStatus = 0;
	mbWideBus = false;
	muiCommandBits = 0;
	muiResponseBits = 0;
	muiResponseSent = 0;
	muiResponseDelay = 0;
	mbReadPending = false;
	mbWritePending = false;
	muiNumDAT0Bits = 0;
	muiDAT0Sent = 0;
}


//////////////////////////////////////////////////////////////////////////
//
// Reads a whole image file into memory; bWriteBack writes it back on Flush.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65SDCard::InsertCard(const char* szImageFilename, bool bWriteBack)
{
	FILE*	pcFile;
	long	iSize;

	RemoveCard();
	pcFile = fopen(szImageFilename, "rb");
	if (!pcFile)
	{
		return false;
	}
	fseek(pcFile, 0, SEEK_END);
	iSize = ftell(pcFile);
	fseek(pcFile, 0, SEEK_SET);
	if ((iSize <= 0) || (iSize % HCP65_SD_SECTOR_SIZE != 0))
	{
		fclose(pcFile);
		return false;
	}
	mpuiImage = (uint8*)malloc((size_t)iSize);
	if (fread(mpuiImage, 1, (size_t)iSize, pcFile) != (size_t)iSize)
	{
		fclose(pcFile);
		SafeFree(mpuiImage);
		return false;
	}
	fclose(pcFile);
	muiNumSectors = (uint32)(iSize / HCP65_SD_SECTOR_SIZE);
	mbWriteBack = bWriteBack;
	mszFilename.Kill();
	mszFilename.Init(szImageFilename);
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// A copy of uiSize bytes of puiImage.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65SDCard::InsertCard(uint8* puiImage, uint32 uiSize)
{
	RemoveCard();
	if ((uiSize == 0) || (uiSize % HCP65_SD_SECTOR_SIZE != 0))
	{
		return false;
	}
	mpuiImage = (uint8*)malloc(uiSize);
	memcpy(mpuiImage, puiImage, uiSize);
	muiNumSectors = uiSize / HCP65_SD_SECTOR_SIZE;
	mbWriteBack = false;
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::RemoveCard(void)
{
	Flush();
	SafeFree(mpuiImage);
	muiNumSectors = 0;
	mbWriteBack = false;
	meState = HSDCS_Idle;
}


//////////////////////////////////////////////////////////////////////////
//
// Writes the image back to its file if it was inserted with bWriteBack.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65SDCard::Flush(void)
{
	FILE*	pcFile;
	size_t	uiSize;
	bool	bResult;

	if (!mpuiImage || !mbWriteBack || mszFilename.Empty())
	{
		return true;
	}
	pcFile = fopen(mszFilename.Text(), "r+b");
	if (!pcFile)
	{
		return false;
	}
	uiSize = (size_t)muiNumSectors * HCP65_SD_SECTOR_SIZE;
	bResult = fwrite(mpuiImage, 1, uiSize, pcFile) == uiSize;
	fclose(pcFile);
	return bResult;
}


//////////////////////////////////////////////////////////////////////////
//
// Only socket 0 can hold a card.
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65SDCard::HasCard(void)
{
	return mbEnabled && ((muiFlags & HCP65_SD_FLAG_CARD) == 0) && (mpuiImage != NULL);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65SDCard::Read(uint32 uiOffset)
{
	if (uiOffset < HCP65_SD_ROM_SIZE)
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
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::Write(uint32 uiOffset, uint8 uiValue)
{
	if ((uiOffset >= HCP65_SD_ROM_SIZE) && (uiOffset < 0x800))
	{
		WriteRegister(uiOffset & 0x1F, uiValue);
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The levels the card drives (CMD while responding, DAT0 after a write);
// a line nothing drives is pulled up.
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65SDCard::CardDrivenLines(void)
{
	uint8	uiLines;
	uint8	uiByte;
	uint8	uiBit;

	uiLines = HCP65_SD_GPIO_CLK | HCP65_SD_GPIO_CMD | HCP65_SD_GPIO_DAT0 | HCP65_SD_GPIO_DAT3;
	if (!HasCard())
	{
		return uiLines;
	}
	if ((muiResponseBits != 0) && (muiResponseDelay == 0) && (muiResponseSent < muiResponseBits))
	{
		uiByte = mauiResponse[muiResponseSent / 8];
		uiBit = (uint8)((uiByte >> (7 - (muiResponseSent % 8))) & 1);
		if (!uiBit)
		{
			uiLines &= ~HCP65_SD_GPIO_CMD;
		}
	}
	if (muiDAT0Sent < muiNumDAT0Bits)
	{
		if (!muiDAT0Bits[muiDAT0Sent])
		{
			uiLines &= ~HCP65_SD_GPIO_DAT0;
		}
	}
	return uiLines;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65SDCard::ReadRegister(uint32 uiRegister)
{
	uint8	uiDriven;
	uint8	uiLines;
	uint8	uiValue;

	switch (uiRegister)
	{
	case HCP65_SD_GPIO:
		uiLines = CardDrivenLines();
		if (mbEnabled)
		{
			uiDriven = (uint8)(muiGPIO >> 4);
			uiLines = (uint8)((uiLines & ~uiDriven) | (muiGPIO & uiDriven));
		}
		return (uint8)(uiLines & 0x0F);

	case HCP65_SD_FLAGS:
		uiValue = (uint8)(muiFlags & 0x3F);
		if (mbEnabled)
		{
			uiValue |= HCP65_SD_FLAG_ENABLE;
		}
		if (mbInterrupt)
		{
			uiValue |= HCP65_SD_FLAG_INTERRUPT;
		}
		return uiValue;
	}
	return 0xFF;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::WriteRegister(uint32 uiRegister, uint8 uiValue)
{
	switch (uiRegister)
	{
	case HCP65_SD_ADDRESS_LOW:
		muiAddress = (muiAddress & 0xFFFF00) | uiValue;
		break;
	case HCP65_SD_ADDRESS_HIGH:
		muiAddress = (muiAddress & 0xFF00FF) | ((uint32)uiValue << 8);
		break;
	case HCP65_SD_ADDRESS_BANK:
		muiAddress = (muiAddress & 0x00FFFF) | ((uint32)uiValue << 16);
		break;
	case HCP65_SD_GPIO:
		WriteGPIO(uiValue);
		break;
	case HCP65_SD_DATA_COUNT_LOW:
		muiDataCount = (uint16)((muiDataCount & 0xF00) | uiValue);
		break;
	case HCP65_SD_DATA_COUNT_HIGH:
		muiDataCount = (uint16)((muiDataCount & 0x0FF) | ((uiValue & 0x0F) << 8));
		break;
	case HCP65_SD_CRC_COUNT_LOW:
		muiCRCCount = (uint16)((muiCRCCount & 0xF00) | uiValue);
		break;
	case HCP65_SD_CRC_COUNT_HIGH:
		muiCRCCount = (uint16)((muiCRCCount & 0x0FF) | ((uiValue & 0x0F) << 8));
		break;
	case HCP65_SD_CLEAR_INTERRUPT:
		mbInterrupt = false;
		break;
	case HCP65_SD_READ_START:
		StartRead();
		break;
	case HCP65_SD_FLAGS:
		WriteFlags(uiValue);
		break;
	case HCP65_SD_WRITE_DATA_LOW:
	case HCP65_SD_WRITE_DATA_HIGH:
		WriteStream(uiValue);
		break;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// D6 enables the board (the bus switch to the selected socket and the GPIO
// drivers); it can't while the board is held in reset.  Disabling stops a
// read.  The interrupt is cleared while interrupts are disabled.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::WriteFlags(uint8 uiValue)
{
	bool	bPreviousCLK;

	bPreviousCLK = (ReadRegister(HCP65_SD_GPIO) & HCP65_SD_GPIO_CLK) != 0;
	muiFlags = (uint8)(uiValue & ~HCP65_SD_FLAG_ENABLE);
	mbEnabled = ((uiValue & HCP65_SD_FLAG_ENABLE) != 0) && !mbHeldInReset;
	if (!mbEnabled)
	{
		mbReading = false;
	}
	if (!mbEnabled || !(muiFlags & HCP65_SD_FLAG_INTERRUPT))
	{
		mbInterrupt = false;
	}
	mbPreviousCLK = bPreviousCLK;
	WriteGPIO(muiGPIO);
}


//////////////////////////////////////////////////////////////////////////
//
// The card sees a clock edge when the level on SD CLK changes.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::WriteGPIO(uint8 uiValue)
{
	bool	bCLK;

	muiGPIO = uiValue;
	bCLK = (ReadRegister(HCP65_SD_GPIO) & HCP65_SD_GPIO_CLK) != 0;
	if (bCLK != mbPreviousCLK)
	{
		mbPreviousCLK = bCLK;
		if (HasCard())
		{
			if (bCLK)
			{
				RisingEdge();
			}
			else
			{
				FallingEdge();
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The card samples CMD.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::RisingEdge(void)
{
	bool	bCMD;

	if ((muiResponseBits != 0) && (muiResponseSent < muiResponseBits))
	{
		//Busy sending; the card doesn't listen while it responds.
		return;
	}
	bCMD = (ReadRegister(HCP65_SD_GPIO) & HCP65_SD_GPIO_CMD) != 0;
	ReceiveCommandBit(bCMD);
}


//////////////////////////////////////////////////////////////////////////
//
// The card moves its outputs on.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::FallingEdge(void)
{
	if (muiResponseBits != 0)
	{
		if (muiResponseDelay > 0)
		{
			muiResponseDelay--;
		}
		else if (muiResponseSent < muiResponseBits)
		{
			muiResponseSent++;
		}
	}
	if (muiDAT0Sent < muiNumDAT0Bits)
	{
		muiDAT0Sent++;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Commands are 48 bits: 0, 1, the 6 bit index, the 32 bit argument, CRC7
// and 1.  The card waits for a start bit, and ignores a command whose
// framing or CRC is wrong.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::ReceiveCommandBit(bool bBit)
{
	uint8	uiIndex;
	uint32	uiArgument;

	if ((muiCommandBits == 0) && bBit)
	{
		return;
	}
	if (muiCommandBits % 8 == 0)
	{
		muiCommand[muiCommandBits / 8] = 0;
	}
	if (bBit)
	{
		muiCommand[muiCommandBits / 8] |= (uint8)(0x80 >> (muiCommandBits % 8));
	}
	muiCommandBits++;
	if (muiCommandBits < 48)
	{
		return;
	}
	muiCommandBits = 0;

	if (((muiCommand[0] & 0xC0) != 0x40) || ((muiCommand[5] & 1) != 1))
	{
		return;
	}
	if ((muiCommand[5] >> 1) != CRC7(muiCommand, 5))
	{
		muiStatus |= HCP65_SD_COM_CRC_ERROR;
		return;
	}

	uiIndex = (uint8)(muiCommand[0] & 0x3F);
	uiArgument = ((uint32)muiCommand[1] << 24) | ((uint32)muiCommand[2] << 16) | ((uint32)muiCommand[3] << 8) | muiCommand[4];
	if (mbApplicationCommand)
	{
		mbApplicationCommand = false;
		ApplicationCommand(uiIndex, uiArgument);
	}
	else
	{
		Command(uiIndex, uiArgument);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::Command(uint8 uiIndex, uint32 uiArgument)
{
	uint8	auiCSD[16];
	uint8	auiCID[16];
	uint32	uiSize;

	switch (uiIndex)
	{
	case 0:
		meState = HSDCS_Idle;
		muiRCA = 0;
		mbWideBus = false;
		mbReadPending = false;
		mbWritePending = false;
		muiNumDAT0Bits = 0;
		return;

	case 8:
		//Interface condition: echo the voltage and check pattern.
		if (meState == HSDCS_Idle)
		{
			Respond48(8, uiArgument & 0xFFF, true);
		}
		return;

	case 55:
		RespondR1(55);
		mbApplicationCommand = true;
		return;

	case 2:
		if (meState == HSDCS_Ready)
		{
			memcpy(auiCID, gauiHCP65SDCID, 15);
			auiCID[15] = (uint8)((CRC7(auiCID, 15) << 1) | 1);
			RespondR2(auiCID);
			meState = HSDCS_Identification;
		}
		return;

	case 3:
		if ((meState == HSDCS_Identification) || (meState == HSDCS_StandBy))
		{
			muiRCA = HCP65_SD_RCA;
			meState = HSDCS_StandBy;
			Respond48(3, ((uint32)muiRCA << 16) | ((StateBits() >> 0) & 0x1FFF), true);
		}
		return;

	case 9:
		if ((meState == HSDCS_StandBy) && ((uiArgument >> 16) == muiRCA))
		{
			//CSD version 2: C_SIZE is the capacity in 512KB units, less one.
			memset(auiCSD, 0, sizeof(auiCSD));
			uiSize = (muiNumSectors / 1024) - 1;
			auiCSD[0] = 0x40;
			auiCSD[1] = 0x0E;
			auiCSD[3] = 0x32;
			auiCSD[4] = 0x5B;
			auiCSD[5] = 0x59;
			auiCSD[7] = (uint8)((uiSize >> 16) & 0x3F);
			auiCSD[8] = (uint8)(uiSize >> 8);
			auiCSD[9] = (uint8)uiSize;
			auiCSD[10] = 0x7F;
			auiCSD[11] = 0x80;
			auiCSD[12] = 0x0A;
			auiCSD[13] = 0x40;
			auiCSD[15] = (uint8)((CRC7(auiCSD, 15) << 1) | 1);
			RespondR2(auiCSD);
		}
		return;

	case 7:
		if ((uiArgument >> 16) == muiRCA)
		{
			RespondR1(7);
			meState = HSDCS_Transfer;
		}
		else
		{
			meState = HSDCS_StandBy;
		}
		return;

	case 13:
		if ((uiArgument >> 16) == muiRCA)
		{
			RespondR1(13);
		}
		return;

	case 16:
		if (meState == HSDCS_Transfer)
		{
			RespondR1(16);
		}
		return;

	case 12:
		if ((meState == HSDCS_SendingData) || (meState == HSDCS_ReceivingData))
		{
			mbReadPending = false;
			mbWritePending = false;
			meState = HSDCS_Transfer;
		}
		RespondR1(12);
		return;

	case 17:
	case 24:
		if (meState != HSDCS_Transfer)
		{
			muiStatus |= HCP65_SD_ILLEGAL_COMMAND;
			return;
		}
		if (uiArgument >= muiNumSectors)
		{
			muiStatus |= HCP65_SD_OUT_OF_RANGE;
			RespondR1(uiIndex);
			return;
		}
		RespondR1(uiIndex);
		if (uiIndex == 17)
		{
			muiReadSector = uiArgument;
			mbReadPending = true;
			meState = HSDCS_SendingData;
		}
		else
		{
			muiWriteSector = uiArgument;
			mbWritePending = true;
			muiNumWriteNybbles = 0;
			meState = HSDCS_ReceivingData;
		}
		return;
	}

	muiStatus |= HCP65_SD_ILLEGAL_COMMAND;
}


//////////////////////////////////////////////////////////////////////////
//
// After CMD55.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::ApplicationCommand(uint8 uiIndex, uint32 uiArgument)
{
	switch (uiIndex)
	{
	case 41:
		//The first ACMD41 finds the card still busy initialising; after that it's ready.  The OCR: bit 31 ready,
		//bit 30 high capacity, 2.7V to 3.6V.  R3 has no CRC.
		if (meState == HSDCS_Idle)
		{
			Respond48(0x3F, 0x40FF8000, false);
			meState = HSDCS_Ready;
		}
		else if (meState == HSDCS_Ready)
		{
			Respond48(0x3F, 0xC0FF8000, false);
		}
		return;

	case 6:
		if (meState == HSDCS_Transfer)
		{
			mbWideBus = (uiArgument & 3) == 2;
			RespondR1(6);
		}
		return;
	}
	Command(uiIndex, uiArgument);
}


//////////////////////////////////////////////////////////////////////////
//
// The card's state, ready for data and app command bits.
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65SDCard::StateBits(void)
{
	uint32	uiState;

	switch (meState)
	{
	case HSDCS_Idle:			uiState = 0;	break;
	case HSDCS_Ready:			uiState = 1;	break;
	case HSDCS_Identification:	uiState = 2;	break;
	case HSDCS_StandBy:			uiState = 3;	break;
	case HSDCS_Transfer:		uiState = 4;	break;
	case HSDCS_SendingData:		uiState = 5;	break;
	default:					uiState = 6;	break;
	}
	return (uiState << 9) | HCP65_SD_READY_FOR_DATA | (mbApplicationCommand ? HCP65_SD_APP_CMD : 0);
}


//////////////////////////////////////////////////////////////////////////
//
// R1: the card status.  Its error bits are cleared by sending them.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::RespondR1(uint8 uiIndex)
{
	if (uiIndex == 55)
	{
		Respond48(uiIndex, muiStatus | StateBits() | HCP65_SD_APP_CMD, true);
	}
	else
	{
		Respond48(uiIndex, muiStatus | StateBits(), true);
	}
	muiStatus &= ~HCP65_SD_CLEARED_ON_READ;
}


//////////////////////////////////////////////////////////////////////////
//
// A 48 bit response: 0, 0, the 6 bit index, 32 bits of content, CRC7 (or
// all ones) and 1.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::Respond48(uint8 uiIndex, uint32 uiContent, bool bCRC)
{
	mauiResponse[0] = (uint8)(uiIndex & 0x3F);
	mauiResponse[1] = (uint8)(uiContent >> 24);
	mauiResponse[2] = (uint8)(uiContent >> 16);
	mauiResponse[3] = (uint8)(uiContent >> 8);
	mauiResponse[4] = (uint8)uiContent;
	mauiResponse[5] = bCRC ? (uint8)((CRC7(mauiResponse, 5) << 1) | 1) : 0xFF;
	muiResponseBits = 48;
	muiResponseSent = 0;
	muiResponseDelay = HCP65_SD_RESPONSE_DELAY;
}


//////////////////////////////////////////////////////////////////////////
//
// A 136 bit response: 0, 0, 111111, then the CID or CSD's 127 bits and 1
// (its 16th byte is its CRC7 and end bit).
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::RespondR2(uint8* puiRegister)
{
	mauiResponse[0] = 0x3F;
	memcpy(&mauiResponse[1], puiRegister, 16);
	muiResponseBits = 136;
	muiResponseSent = 0;
	muiResponseDelay = HCP65_SD_RESPONSE_DELAY;
}


//////////////////////////////////////////////////////////////////////////
//
// Read Start: the board clocks the card until its data starts, stores Data
// Count bytes and clocks the CRC bytes through.  The card only sends a
// block after a read command; without one the board waits for ever.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::StartRead(void)
{
	uint64	uiClocks;

	mbInterrupt = false;
	if (!mbEnabled || !mpcHCP65)
	{
		return;
	}
	mbReading = true;
	if (HasCard() && mbReadPending)
	{
		uiClocks = HCP65_SD_ACCESS_CLOCKS + 1 + 2 * ((uint64)muiDataCount + muiCRCCount) + 1;
		muiReadDone = mpcHCP65->NumCycles() + uiClocks;
	}
	else
	{
		muiReadDone = HCP65_NEVER;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The block is in main memory.  More than 512 bytes takes the CRC bytes and
// then the bus floating high.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::FinishRead(void)
{
	uint8*	puiMain;
	uint8*	puiSector;
	uint8	auiCRC[8];
	uint32	i;
	uint32	uiLine;
	uint8	auiBits[1024];
	uint16	uiCRC;
	uint8	uiByte;

	mbReading = false;
	puiMain = mpcHCP65->GetMainMemory();
	puiSector = &mpuiImage[(size_t)muiReadSector * HCP65_SD_SECTOR_SIZE];

	//The CRC bytes as the card sends them: CRC16 of each DAT line, the lines' bits interleaved in nybbles.
	memset(auiCRC, 0, sizeof(auiCRC));
	for (uiLine = 0; uiLine < 4; uiLine++)
	{
		for (i = 0; i < 1024; i++)
		{
			uiByte = puiSector[i / 2];
			auiBits[i] = (uint8)((((i & 1) ? uiByte : (uiByte >> 4)) >> uiLine) & 1);
		}
		uiCRC = CRC16(auiBits, 1024);
		for (i = 0; i < 16; i++)
		{
			if (uiCRC & (0x8000 >> i))
			{
				auiCRC[i / 2] |= (uint8)(1 << (uiLine + ((i & 1) ? 0 : 4)));
			}
		}
	}

	for (i = 0; i < muiDataCount; i++)
	{
		if (i < HCP65_SD_SECTOR_SIZE)
		{
			uiByte = puiSector[i];
		}
		else if (i < HCP65_SD_SECTOR_SIZE + 8)
		{
			uiByte = auiCRC[i - HCP65_SD_SECTOR_SIZE];
		}
		else
		{
			uiByte = 0xFF;
		}
		puiMain[(muiAddress + i) & 0xFFFFFF] = uiByte;
	}
	muiAddress = (muiAddress + muiDataCount) & 0xFFFFFF;
	muiSectorsRead++;
	mbReadPending = false;
	meState = HSDCS_Transfer;
	if (muiFlags & HCP65_SD_FLAG_INTERRUPT)
	{
		mbInterrupt = true;
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Each byte stored to Write Data is two nybbles on DAT3-DAT0, high first.
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::WriteStream(uint8 uiValue)
{
	if (!HasCard() || !mbWritePending)
	{
		return;
	}
	if (muiNumWriteNybbles < HCP65_SD_WRITE_NYBBLES)
	{
		muiWriteNybbles[muiNumWriteNybbles++] = (uint8)(uiValue >> 4);
	}
	if (muiNumWriteNybbles < HCP65_SD_WRITE_NYBBLES)
	{
		muiWriteNybbles[muiNumWriteNybbles++] = (uint8)(uiValue & 0xF);
	}
	if (muiNumWriteNybbles == HCP65_SD_WRITE_NYBBLES)
	{
		FinishWrite();
	}
}


//////////////////////////////////////////////////////////////////////////
//
// Checks the start and end nybbles and each line's CRC16, writes the
// sector, and gives DAT0's CRC status (0, 010 accepted or 101 CRC error,
// 1) two clocks later, then a few clocks of busy (0).
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::FinishWrite(void)
{
	uint8	auiBits[1024];
	uint32	uiLine;
	uint32	i;
	uint16	uiCRC;
	uint16	uiSent;
	bool	bGood;
	uint8*	puiSector;

	bGood = (muiWriteNybbles[0] == 0) && (muiWriteNybbles[HCP65_SD_WRITE_NYBBLES - 1] == 0xF);
	for (uiLine = 0; bGood && (uiLine < 4); uiLine++)
	{
		for (i = 0; i < 1024; i++)
		{
			auiBits[i] = (uint8)((muiWriteNybbles[1 + i] >> uiLine) & 1);
		}
		uiCRC = CRC16(auiBits, 1024);
		uiSent = 0;
		for (i = 0; i < 16; i++)
		{
			uiSent = (uint16)((uiSent << 1) | ((muiWriteNybbles[1025 + i] >> uiLine) & 1));
		}
		bGood = uiCRC == uiSent;
	}

	if (bGood)
	{
		puiSector = &mpuiImage[(size_t)muiWriteSector * HCP65_SD_SECTOR_SIZE];
		for (i = 0; i < HCP65_SD_SECTOR_SIZE; i++)
		{
			puiSector[i] = (uint8)((muiWriteNybbles[1 + i * 2] << 4) | muiWriteNybbles[2 + i * 2]);
		}
		muiSectorsWritten++;
	}

	//Two clocks high (Ncrc), then the status token.
	muiNumDAT0Bits = 0;
	muiDAT0Bits[muiNumDAT0Bits++] = 1;
	muiDAT0Bits[muiNumDAT0Bits++] = 1;
	muiDAT0Bits[muiNumDAT0Bits++] = 0;
	muiDAT0Bits[muiNumDAT0Bits++] = bGood ? 0 : 1;
	muiDAT0Bits[muiNumDAT0Bits++] = bGood ? 1 : 0;
	muiDAT0Bits[muiNumDAT0Bits++] = bGood ? 0 : 1;
	muiDAT0Bits[muiNumDAT0Bits++] = 1;
	for (i = 0; i < 4; i++)
	{
		muiDAT0Bits[muiNumDAT0Bits++] = 0;
	}
	muiDAT0Sent = 0;
	muiNumWriteNybbles = 0;
	mbWritePending = false;
	meState = HSDCS_Transfer;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CHCP65SDCard::CatchUp(uint64 uiCycle)
{
	if (mbReading && (muiReadDone != HCP65_NEVER) && (uiCycle >= muiReadDone))
	{
		FinishRead();
	}
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint64 CHCP65SDCard::NextEvent(void)
{
	if (mbReading)
	{
		return muiReadDone;
	}
	return HCP65_NEVER;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CHCP65SDCard::IsInterrupting(void)
{
	return mbInterrupt;
}


//////////////////////////////////////////////////////////////////////////
//
// x^7 + x^3 + 1, over whole bytes.
//
//////////////////////////////////////////////////////////////////////////
uint8 CHCP65SDCard::CRC7(uint8* puiData, uint32 uiLength)
{
	uint8	uiCRC;
	uint32	i;
	int		iBit;
	uint8	uiIn;

	uiCRC = 0;
	for (i = 0; i < uiLength; i++)
	{
		for (iBit = 7; iBit >= 0; iBit--)
		{
			uiIn = (uint8)(((puiData[i] >> iBit) & 1) ^ ((uiCRC >> 6) & 1));
			uiCRC = (uint8)((uiCRC << 1) & 0x7F);
			if (uiIn)
			{
				uiCRC ^= 0x09;
			}
		}
	}
	return uiCRC;
}


//////////////////////////////////////////////////////////////////////////
//
// x^16 + x^12 + x^5 + 1 (CCITT, starting at 0) over uiLength bits, one bit
// per byte.
//
//////////////////////////////////////////////////////////////////////////
uint16 CHCP65SDCard::CRC16(uint8* puiBits, uint32 uiLength)
{
	uint16	uiCRC;
	uint32	i;
	uint16	uiIn;

	uiCRC = 0;
	for (i = 0; i < uiLength; i++)
	{
		uiIn = (uint16)((puiBits[i] & 1) ^ (uiCRC >> 15));
		uiCRC = (uint16)(uiCRC << 1);
		if (uiIn)
		{
			uiCRC ^= 0x1021;
		}
	}
	return uiCRC;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint8* CHCP65SDCard::GetImage(void)
{
	return mpuiImage;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65SDCard::NumSectors(void)
{
	return muiNumSectors;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65SDCard::NumSectorsRead(void)
{
	return muiSectorsRead;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint32 CHCP65SDCard::NumSectorsWritten(void)
{
	return muiSectorsWritten;
}
