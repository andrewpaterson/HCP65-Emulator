#include "HCP65User.h"


//In globals so the compiler can't work the answers out itself.
uint16	guiA = 300;
uint16	guiB = 200;
uint32	guiProduct;


//////////////////////////////////////////////////////////////////////////
//
// The test's boot.exe: runs in user mode, multiplies and divides (with the
// math tables the kernal maps in at $400000), gives the kernal a value and
// exits with 42.
//
//////////////////////////////////////////////////////////////////////////
int main(void)
{
	guiProduct = (uint32)guiA * guiB;
	KernalPutValue((uint16)(guiProduct / 3));
	return 42;
}
