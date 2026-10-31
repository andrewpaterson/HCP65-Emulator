#ifndef __HCP65_H__
#define __HCP65_H__
#include "BaseLib/PrimitiveTypes.h"
#include "MicroprocessorLib/Fast65C816.h"


//The HCP65 computer: a CFast65C816 and the devices on its bus.  There are no devices yet, so every address reads
//as an open bus (0xFF) and writes go nowhere.
class CHCP65
{
protected:
	CFast65C816		mcMPU;

public:
	void			Init(void);
	void			Kill(void);

	void			Reset(void);
	CFast65C816*	GetMPU(void);

protected:
	static uint8	ReadByte(void* pvContext, uint32 uiAddress);
	static void		WriteByte(void* pvContext, uint32 uiAddress, uint8 uiValue);
};


#endif // __HCP65_H__
