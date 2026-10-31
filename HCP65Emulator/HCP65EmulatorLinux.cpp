#include "BaseLib/Logger.h"
#include "BaseLib/TypeNames.h"
#include "BaseLib/TypeConverter.h"
#include "BaseLib/GlobalMemory.h"
#include "BaseLib/GlobalDataTypesIO.h"
#include "StandardLib/Objects.h"
#include "InputLib/Input.h"
#include "X11Lib/X11WindowFactory.h"
#include "X11Lib/X11Input.h"
#include "HCP65Emulator.h"


//////////////////////////////////////////////////////////////////////////
//
// The emulator on Linux: X11 windows, their keyboard and mouse events
// given to the input as Windows raw input events (X11Input.h).
//
//////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
	gcLogger.Init();
	TypesInit();
	TypeConverterInit();
	MemoryInit();
	DataIOInit();
	ObjectsInit();

	bool bResult;

	{
		CX11WindowFactory	cNativeFactory;
		CInput				cInput;
		CX11Input			cX11Input;

		bResult = cNativeFactory.Init(&gcMemoryAllocator, NULL);
		if (bResult)
		{
			//The devices are described by the XML so they are added after it is read.
			cInput.Init(NULL);
			cInput.Read("../../commonlibraries/WinInputLib/xml");
			cX11Input.Init();
			cInput.AddNativeInput(&cX11Input);
			cNativeFactory.SetInput(&cX11Input);

			bResult = HCP65EmulatorRun(&cNativeFactory, &cInput);

			cNativeFactory.SetInput(NULL);
			cInput.Kill();
			cX11Input.Kill();
		}
		cNativeFactory.Kill();
	}

	ObjectsFlush();
	ObjectsKill();
	DataIOKill();
	MemoryKill();
	TypeConverterKill();
	TypesKill();

	gcLogger.Kill();

	return bResult ? 0 : 1;
}
