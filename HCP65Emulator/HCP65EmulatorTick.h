#ifndef __HCP65_EMULATOR_TICK_H__
#define __HCP65_EMULATOR_TICK_H__
#include "WindowLib/WindowTick.h"


class CHCP65;


//Paints the window each tick.  The emulator will run the HCP65 from here.
class CHCP65EmulatorTick : public CWindowTick
{
CONSTRUCTABLE(CHCP65EmulatorTick);
DESTRUCTABLE(CHCP65EmulatorTick);
protected:
	CHCP65*		mpcHCP65;

public:
	void	Init(CHCP65* pcHCP65);
	void	Class(void) override;
	void	Free(void) override;
	void	Tick(Ptr<CWindow> pWindow, int64 iUpdateTimeInMillieconds, int64 iTotalTimeInMillieconds) override;
};


#endif // __HCP65_EMULATOR_TICK_H__
