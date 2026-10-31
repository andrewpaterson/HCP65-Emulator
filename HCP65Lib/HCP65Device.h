#ifndef __HCP65_DEVICE_H__
#define __HCP65_DEVICE_H__
#include "BaseLib/PrimitiveTypes.h"


#define HCP65_NEVER				0xFFFFFFFFFFFFFFFFULL	//A NextEvent that will not happen.
#define HCP65_SMALL_SLOT_SIZE	0x8000					//Devices 0 to 7.
#define HCP65_LARGE_SLOT_SIZE	0x20000					//Devices 16 to 19.


class CHCP65;


//A board in one of the HCP65's device sockets (or the processor board's own registers in slot 0).
//
//Read and Write are given the offset within the device's slot.  A device that changes over time keeps
//its own time: CatchUp is called before every access with the CPU cycle being accessed, and at the end of
//every run chunk.  NextEvent says when the device's interrupt will next change on its own, so that the
//computer can stop the processor then and raise it on time.
class CHCP65Device
{
protected:
	CHCP65*		mpcHCP65;
	uint32		muiMinimumSlotSize;
	bool		mbHeldInReset;

public:
	void			Init(uint32 uiMinimumSlotSize);
	virtual void	Kill(void);

	void			Attach(CHCP65* pcHCP65);
	uint32			GetMinimumSlotSize(void);

	virtual uint8	Read(uint32 uiOffset) = 0;
	virtual void	Write(uint32 uiOffset, uint8 uiValue) = 0;

	virtual void	Reset(void);
	virtual void	SetHeldInReset(bool bHeld);
	bool			IsHeldInReset(void);

	virtual void	CatchUp(uint64 uiCycle);
	virtual uint64	NextEvent(void);
	virtual bool	IsInterrupting(void);
};


#endif // __HCP65_DEVICE_H__
