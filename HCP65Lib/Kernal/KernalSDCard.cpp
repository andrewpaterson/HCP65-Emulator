#include <stdlib.h>
#include <string.h>
#include "KernalSDCard.h"


#define SD_REGISTERS			0x400
#define SD_ADDRESS_LOW			0x00
#define SD_ADDRESS_HIGH			0x01
#define SD_ADDRESS_BANK			0x02
#define SD_GPIO					0x04
#define SD_DATA_COUNT_LOW		0x06
#define SD_DATA_COUNT_HIGH		0x07
#define SD_CRC_COUNT_LOW		0x08
#define SD_CRC_COUNT_HIGH		0x09
#define SD_CLEAR_INTERRUPT		0x0A
#define SD_READ_START			0x0B
#define SD_FLAGS				0x0C
#define SD_WRITE_DATA			0x0E

#define SD_CLK					0x01
#define SD_CMD					0x02
#define SD_DAT0					0x04
#define SD_DRIVE_CLK			0x10
#define SD_DRIVE_CMD			0x20

#define SD_FLAG_ENABLE			0x40
#define SD_FLAG_INTERRUPT		0x80

#define SD_SECTOR_SIZE			512
#define SD_CRC_BYTES			8			//The four lines' CRC16s, two nybbles a byte.
#define SD_RESPONSE_WAIT		64			//Clocks to wait for a response's start bit (Ncr).
#define SD_READ_WAIT			60000		//Polls of the flags for a block read.
#define SD_ACMD41_TRIES			1000

#define SD_R1_ERRORS			0xFDF90008	//Every R1 error bit.


//////////////////////////////////////////////////////////////////////////
//
// x^7 + x^3 + 1.
//
//////////////////////////////////////////////////////////////////////////
uint8 KernalCRC7(uint8* puiData, uint16 uiLength)
{
	uint8	uiCRC;
	uint16	i;
	uint8	uiBit;
	uint8	uiByte;
	uint8	uiIn;

	uiCRC = 0;
	for (i = 0; i < uiLength; i++)
	{
		uiByte = puiData[i];
		for (uiBit = 0; uiBit < 8; uiBit++)
		{
			uiIn = (uint8)(((uiByte >> 7) ^ (uiCRC >> 6)) & 1);
			uiCRC = (uint8)((uiCRC << 1) & 0x7F);
			if (uiIn)
			{
				uiCRC ^= 0x09;
			}
			uiByte = (uint8)(uiByte << 1);
		}
	}
	return uiCRC;
}


//////////////////////////////////////////////////////////////////////////
//
// Brings the card in socket 0 up: 80 clocks, CMD0, CMD8, ACMD41 until
// ready, CMD2, CMD3, CMD9 (its size), CMD7 and ACMD6 (4 bit bus).  Returns
// KERNAL_SD_SUCCESS or why it failed.
//
//////////////////////////////////////////////////////////////////////////
uint16 CKernalSDCard::Init(SKernalDevice* psDevice)
{
	uint16	i;
	uint32	uiSize;

	CFileDrive::Init(SD_SECTOR_SIZE);
	mpuiRegisters = (volatile uint8*)(psDevice->uiAddress + SD_REGISTERS);
	mpuiBounce = (uint8*)malloc(SD_SECTOR_SIZE);
	muiRCA = 0;
	muiError = KERNAL_SD_SUCCESS;

	//GPIO first: its latch isn't reset, and enabling the board turns its drivers on.
	SetGPIO(SD_CLK | SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
	muiFlags = SD_FLAG_ENABLE;
	mpuiRegisters[SD_FLAGS] = muiFlags;
	Clocks(80);

	SendCommand(0, 0);
	Clocks(8);

	if (!Command(8, 0x1AA, 48) || ((ResponseContent() & 0xFFF) != 0x1AA))
	{
		muiError = KERNAL_SD_NO_CARD;
		return muiError;
	}

	for (i = 0; i < SD_ACMD41_TRIES; i++)
	{
		if (!CommandR1(55, 0) || !Command(41, 0x40FF8000, 48))
		{
			return muiError;
		}
		if (ResponseContent() & 0x80000000)
		{
			break;
		}
	}
	if (i == SD_ACMD41_TRIES)
	{
		muiError = KERNAL_SD_NOT_READY;
		return muiError;
	}

	if (!Command(2, 0, 136) || !Command(3, 0, 48))
	{
		return muiError;
	}
	muiRCA = (uint16)(ResponseContent() >> 16);

	//CSD version 2: C_SIZE (bits 69 to 48) + 1 times 1024 sectors.
	if (!Command(9, (uint32)muiRCA << 16, 136))
	{
		return muiError;
	}
	uiSize = ((uint32)(mauiResponse[8] & 0x3F) << 16) | ((uint32)mauiResponse[9] << 8) | mauiResponse[10];
	muiMaxSector = ((uint64)uiSize + 1) * 1024;

	if (!CommandR1(7, (uint32)muiRCA << 16) || !CommandR1(55, (uint32)muiRCA << 16) || !CommandR1(6, 2))
	{
		return muiError;
	}
	return KERNAL_SD_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CKernalSDCard::Kill(void)
{
	mpuiRegisters[SD_FLAGS] = 0;
	free(mpuiBounce);
	mpuiBounce = NULL;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void CKernalSDCard::SetGPIO(uint8 uiValue)
{
	mpuiRegisters[SD_GPIO] = uiValue;
}


//////////////////////////////////////////////////////////////////////////
//
// Clocks with CMD driven high.
//
//////////////////////////////////////////////////////////////////////////
void CKernalSDCard::Clocks(uint16 uiClocks)
{
	uint16	i;

	for (i = 0; i < uiClocks; i++)
	{
		SetGPIO(SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		SetGPIO(SD_CLK | SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
	}
}


//////////////////////////////////////////////////////////////////////////
//
// The 48 bits, each put on CMD while CLK is low and taken by the card as
// CLK rises.  CLK is left high.
//
//////////////////////////////////////////////////////////////////////////
void CKernalSDCard::SendCommand(uint8 uiIndex, uint32 uiArgument)
{
	uint8	auiCommand[6];
	uint8	uiByte;
	uint8	uiLevel;
	uint16	i;
	uint8	uiBit;

	auiCommand[0] = (uint8)(0x40 | uiIndex);
	auiCommand[1] = (uint8)(uiArgument >> 24);
	auiCommand[2] = (uint8)(uiArgument >> 16);
	auiCommand[3] = (uint8)(uiArgument >> 8);
	auiCommand[4] = (uint8)uiArgument;
	auiCommand[5] = (uint8)((KernalCRC7(auiCommand, 5) << 1) | 1);

	for (i = 0; i < 6; i++)
	{
		uiByte = auiCommand[i];
		for (uiBit = 0; uiBit < 8; uiBit++)
		{
			uiLevel = (uiByte & 0x80) ? SD_CMD : 0;
			SetGPIO((uint8)(uiLevel | SD_DRIVE_CLK | SD_DRIVE_CMD));
			SetGPIO((uint8)(SD_CLK | uiLevel | SD_DRIVE_CLK | SD_DRIVE_CMD));
			uiByte = (uint8)(uiByte << 1);
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
// CMD is let go and the card clocked until it pulls CMD low (the start
// bit); then the rest of uiBits bits are read into mauiResponse.  The card
// changes CMD as CLK falls; it is read once CLK has risen.
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::ReadResponse(uint16 uiBits)
{
	uint16	i;
	uint8	uiByte;

	memset(mauiResponse, 0, sizeof(mauiResponse));
	for (i = 0; i < SD_RESPONSE_WAIT; i++)
	{
		SetGPIO(SD_DRIVE_CLK);
		SetGPIO(SD_CLK | SD_DRIVE_CLK);
		if ((mpuiRegisters[SD_GPIO] & SD_CMD) == 0)
		{
			break;
		}
	}
	if (i == SD_RESPONSE_WAIT)
	{
		muiError = KERNAL_SD_NO_RESPONSE;
		return false;
	}

	uiByte = 0;
	for (i = 1; i < uiBits; i++)
	{
		SetGPIO(SD_DRIVE_CLK);
		SetGPIO(SD_CLK | SD_DRIVE_CLK);
		uiByte = (uint8)(uiByte << 1);
		if (mpuiRegisters[SD_GPIO] & SD_CMD)
		{
			uiByte |= 1;
		}
		if ((i & 7) == 7)
		{
			mauiResponse[i >> 3] = uiByte;
			uiByte = 0;
		}
	}

	//A few clocks for the card to finish, with CMD driven high again.
	Clocks(8);
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::Command(uint8 uiIndex, uint32 uiArgument, uint16 uiResponseBits)
{
	SendCommand(uiIndex, uiArgument);
	return ReadResponse(uiResponseBits);
}


//////////////////////////////////////////////////////////////////////////
//
// A command with an R1 response, which must have no error bits.
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::CommandR1(uint8 uiIndex, uint32 uiArgument)
{
	if (!Command(uiIndex, uiArgument, 48))
	{
		return false;
	}
	if (ResponseContent() & SD_R1_ERRORS)
	{
		muiError = KERNAL_SD_COMMAND_ERROR;
		return false;
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// The 32 bits after a 48 bit response's index.
//
//////////////////////////////////////////////////////////////////////////
uint32 CKernalSDCard::ResponseContent(void)
{
	return ((uint32)mauiResponse[1] << 24) | ((uint32)mauiResponse[2] << 16) | ((uint32)mauiResponse[3] << 8) | mauiResponse[4];
}


//////////////////////////////////////////////////////////////////////////
//
// CMD17, then CLK let go and Read Start: the board clocks the block into
// the bounce buffer and raises its interrupt flag.
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::ReadSector(uint32 uiSector, void* pvData)
{
	uint32	uiAddress;
	uint16	i;

	uiAddress = (uint32)mpuiBounce;
	mpuiRegisters[SD_ADDRESS_LOW] = (uint8)uiAddress;
	mpuiRegisters[SD_ADDRESS_HIGH] = (uint8)(uiAddress >> 8);
	mpuiRegisters[SD_ADDRESS_BANK] = (uint8)(uiAddress >> 16);
	mpuiRegisters[SD_DATA_COUNT_LOW] = (uint8)SD_SECTOR_SIZE;
	mpuiRegisters[SD_DATA_COUNT_HIGH] = (uint8)(SD_SECTOR_SIZE >> 8);
	mpuiRegisters[SD_CRC_COUNT_LOW] = SD_CRC_BYTES;
	mpuiRegisters[SD_CRC_COUNT_HIGH] = 0;
	mpuiRegisters[SD_FLAGS] = (uint8)(muiFlags | SD_FLAG_INTERRUPT);

	if (!CommandR1(17, uiSector))
	{
		mpuiRegisters[SD_FLAGS] = muiFlags;
		return false;
	}
	SetGPIO(SD_CMD | SD_DRIVE_CMD);
	mpuiRegisters[SD_READ_START] = 0;
	for (i = 0; i < SD_READ_WAIT; i++)
	{
		if (mpuiRegisters[SD_FLAGS] & SD_FLAG_INTERRUPT)
		{
			break;
		}
	}
	mpuiRegisters[SD_FLAGS] = muiFlags;
	SetGPIO(SD_CLK | SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
	if (i == SD_READ_WAIT)
	{
		muiError = KERNAL_SD_READ_TIMEOUT;
		return false;
	}
	memcpy(pvData, mpuiBounce, SD_SECTOR_SIZE);
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// CMD24, then the DAT stream through Write Data, then the card's CRC
// status on DAT0 (010 is accepted) and its busy (DAT0 low) clocked out.
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::WriteSector(uint32 uiSector, void* pvData)
{
	uint8*	puiData;
	uint16	auiCRC[4];
	uint16	i;
	uint8	uiNybble;
	uint8	uiLine;
	uint16	uiIn;
	uint8	uiPrevious;
	uint8	auiTail[18];
	uint8	uiStatus;
	uint8	uiBit;
	volatile uint8*	puiWrite;

	if (!CommandR1(24, uiSector))
	{
		return false;
	}
	SetGPIO(SD_CMD | SD_DRIVE_CMD);

	//The four lines' CRC16s, over the nybbles high first.
	puiData = (uint8*)pvData;
	auiCRC[0] = auiCRC[1] = auiCRC[2] = auiCRC[3] = 0;
	for (i = 0; i < SD_SECTOR_SIZE * 2; i++)
	{
		uiNybble = (i & 1) ? (uint8)(puiData[i >> 1] & 0xF) : (uint8)(puiData[i >> 1] >> 4);
		for (uiLine = 0; uiLine < 4; uiLine++)
		{
			uiIn = (uint16)(((uiNybble >> uiLine) ^ (auiCRC[uiLine] >> 15)) & 1);
			auiCRC[uiLine] = (uint16)(auiCRC[uiLine] << 1);
			if (uiIn)
			{
				auiCRC[uiLine] ^= 0x1021;
			}
		}
	}

	//The stream is one nybble behind the data: the start nybble (0) first, the end nybble (F) last.
	puiWrite = &mpuiRegisters[SD_WRITE_DATA];
	uiPrevious = 0;
	for (i = 0; i < SD_SECTOR_SIZE; i++)
	{
		*puiWrite = (uint8)((uiPrevious << 4) | (puiData[i] >> 4));
		uiPrevious = (uint8)(puiData[i] & 0xF);
	}
	//What's left: the last data nybble, the 16 CRC nybbles and the end nybble, 9 bytes.
	auiTail[0] = uiPrevious;
	for (i = 0; i < 16; i++)
	{
		uiNybble = 0;
		for (uiLine = 0; uiLine < 4; uiLine++)
		{
			if (auiCRC[uiLine] & (0x8000 >> i))
			{
				uiNybble |= (uint8)(1 << uiLine);
			}
		}
		auiTail[1 + i] = uiNybble;
	}
	auiTail[17] = 0xF;
	for (i = 0; i < 18; i += 2)
	{
		*puiWrite = (uint8)((auiTail[i] << 4) | auiTail[i + 1]);
	}

	//CRC status: a start bit (0), three status bits and an end bit; then busy until DAT0 is high.
	for (i = 0; i < 16; i++)
	{
		SetGPIO(SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		SetGPIO(SD_CLK | SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		if ((mpuiRegisters[SD_GPIO] & SD_DAT0) == 0)
		{
			break;
		}
	}
	uiStatus = 0;
	for (uiBit = 0; uiBit < 4; uiBit++)
	{
		SetGPIO(SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		SetGPIO(SD_CLK | SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		uiStatus = (uint8)((uiStatus << 1) | ((mpuiRegisters[SD_GPIO] & SD_DAT0) ? 1 : 0));
	}
	for (i = 0; i < 1000; i++)
	{
		SetGPIO(SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		SetGPIO(SD_CLK | SD_CMD | SD_DRIVE_CLK | SD_DRIVE_CMD);
		if (mpuiRegisters[SD_GPIO] & SD_DAT0)
		{
			break;
		}
	}
	if ((uiStatus >> 1) != 0x2)
	{
		muiError = KERNAL_SD_WRITE_REJECTED;
		return false;
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::Read(uint64 uiSector, void* pvData)
{
	if (uiSector >= muiMaxSector)
	{
		muiError = KERNAL_SD_OUT_OF_RANGE;
		return false;
	}
	return ReadSector((uint32)uiSector, pvData);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::Read(uint64 uiSector, uint32 uiNumSectors, void* pvData)
{
	uint32	i;

	for (i = 0; i < uiNumSectors; i++)
	{
		if (!Read(uiSector + i, (uint8*)pvData + (lsize)i * SD_SECTOR_SIZE))
		{
			return false;
		}
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::Write(uint64 uiSector, void* pvData)
{
	if (uiSector >= muiMaxSector)
	{
		muiError = KERNAL_SD_OUT_OF_RANGE;
		return false;
	}
	return WriteSector((uint32)uiSector, pvData);
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::Write(uint64 uiSector, uint32 uiNumSectors, void* pvData)
{
	uint32	i;

	for (i = 0; i < uiNumSectors; i++)
	{
		if (!Write(uiSector + i, (uint8*)pvData + (lsize)i * SD_SECTOR_SIZE))
		{
			return false;
		}
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
// SD cards don't need erasing before a write.
//
//////////////////////////////////////////////////////////////////////////
bool CKernalSDCard::Erase(uint64 uiStartSector, uint64 uiStopSectorInclusive)
{
	return true;
}


//////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
uint16 CKernalSDCard::GetError(void)
{
	return muiError;
}
