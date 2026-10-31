#include "HCP65User.h"


#define BOOT_PROGRAM	"\\modplay.exe"


//In globals so the compiler can't work the answers out itself.
uint16	guiA = 300;
uint16	guiB = 200;
uint32	guiProduct;


//////////////////////////////////////////////////////////////////////////
//
// The test's boot.exe: runs in user mode, multiplies and divides (with the
// math tables the kernal maps in at $400000) and gives the kernal a value,
// then runs modplay.exe with its own argument and exits with modplay's
// exit code (or 100 and why it couldn't run).
//
//////////////////////////////////////////////////////////////////////////
int main(void)
{
	uint16	uiResult;
	uint16	uiExitCode;

	guiProduct = (uint32)guiA * guiB;
	KernalPutValue((uint16)(guiProduct / 3));

	uiExitCode = 0;
	uiResult = KernalExecute(BOOT_PROGRAM, KernalGetArgument(), &uiExitCode);
	if (uiResult != KERNAL_RESULT_EXITED)
	{
		return 100 + uiResult;
	}
	return uiExitCode;
}
