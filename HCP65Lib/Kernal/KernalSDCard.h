#ifndef __KERNAL_SD_CARD_H__
#define __KERNAL_SD_CARD_H__
#include "BaseLib/FileDrive.h"
#include "KernalDevices.h"


#define KERNAL_SD_CARD_NAME			"HCP65 SD Card Board"

#define KERNAL_SD_SUCCESS			0
#define KERNAL_SD_NO_CARD			1		//No response to CMD8.
#define KERNAL_SD_NOT_READY			2		//ACMD41 never finished initialising.
#define KERNAL_SD_NO_RESPONSE		3
#define KERNAL_SD_COMMAND_ERROR		4		//An R1 with an error bit set.
#define KERNAL_SD_READ_TIMEOUT		5
#define KERNAL_SD_WRITE_REJECTED	6
#define KERNAL_SD_OUT_OF_RANGE		7


//An SD card on the HCP65 SD Card Board, used as a drive for the FAT32 volume.  Commands are bit banged through the
//board's GPIO register; a sector is read by the board straight into a bounce buffer in main memory (the board only
//reaches main memory) and copied from there; a sector is written as the 521 byte DAT stream (start nybble, data, the
//four lines' CRC16s, end nybble) through Write Data.  Card socket 0, 4 bit bus, block addressing (SDHC/SDXC).
class CKernalSDCard : public CFileDrive
{
protected:
	volatile uint8*	mpuiRegisters;
	uint8			muiFlags;
	uint16			muiRCA;
	uint8*			mpuiBounce;		//512 bytes in main memory.
	uint8			mauiResponse[17];
	uint16			muiError;

public:
	uint16	Init(SKernalDevice* psDevice);
	void	Kill(void) override;

	bool	Read(uint64 uiSector, void* pvData) override;
	bool	Read(uint64 uiSector, uint32 uiNumSectors, void* pvData) override;
	bool	Write(uint64 uiSector, void* pvData) override;
	bool	Write(uint64 uiSector, uint32 uiNumSectors, void* pvData) override;
	bool	Erase(uint64 uiStartSector, uint64 uiStopSectorInclusive) override;

	uint16	GetError(void);

protected:
	void	SetGPIO(uint8 uiValue);
	void	Clocks(uint16 uiClocks);
	void	SendCommand(uint8 uiIndex, uint32 uiArgument);
	bool	ReadResponse(uint16 uiBits);
	bool	Command(uint8 uiIndex, uint32 uiArgument, uint16 uiResponseBits);
	bool	CommandR1(uint8 uiIndex, uint32 uiArgument);
	uint32	ResponseContent(void);
	bool	ReadSector(uint32 uiSector, void* pvData);
	bool	WriteSector(uint32 uiSector, void* pvData);
};


uint8	KernalCRC7(uint8* puiData, uint16 uiLength);


#endif // __KERNAL_SD_CARD_H__
