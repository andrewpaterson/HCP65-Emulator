#ifndef __HCP65_USER_H__
#define __HCP65_USER_H__
#include "BaseLib/PrimitiveTypes.h"


//Kernal calls for a program the HCP65 kernal runs (UserCalls.asm).  A program exits by returning from main (or by
//calling exit).

//Gives the kernal a value it keeps in its status (for tests).
extern "C" void	KernalPutValue(uint16 uiValue);


#endif // __HCP65_USER_H__
