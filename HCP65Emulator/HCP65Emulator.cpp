#include "BaseLib/Logger.h"
#include "BaseLib/TypeNames.h"
#include "BaseLib/TypeConverter.h"
#include "BaseLib/GlobalMemory.h"
#include "BaseLib/GlobalDataTypesIO.h"
#include "StandardLib/Objects.h"
#include "SupportLib/ImageColour.h"
#include "SupportLib/ImageColourConverter.h"
#include "SupportLib/TrueTypeFont.h"
#include "SupportLib/TrueTypeFontFactory.h"
#include "SupportLib/FontColours.h"
#include "InputLib/Input.h"
#include "WindowLib/Window.h"
#include "WindowLib/ClearCanvasDraw.h"
#include "WindowLib/GridContainer.h"
#include "WindowLib/Canvas.h"
#include "WindowLib/JewelColours.h"
#include "WindowLib/ComponentInput.h"
#include "WindowLib/JewelTheme.h"
#include "WindowLib/MenuBar.h"
#include "BaseLib/DiskFile.h"
#include "WindowLib/NativeWindowFactory.h"
#include "HCP65Lib/HCP65.h"
#include "HCP65Lib/HCP65Audio.h"
#include "HCP65Lib/HCP65SDCard.h"
#include "HostAudio.h"
#include "HCP65Emulator.h"
#include "HCP65EmulatorTick.h"
#include "FileMenuListener.h"


#define HCP65_EMULATOR_FONT_CHARACTERS	" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"


//////////////////////////////////////////////////////////////////////////
//
// Rasterises the glyphs from the TrueType font in white into an image in
// the window's colour order and makes a font from them.
//
//////////////////////////////////////////////////////////////////////////
Ptr<CFont> GenerateFont(const char* szFontFilename, const char* szName, float32 fPixelSize, EColourOrder eOrder)
{
	CTrueTypeFont			cTrueTypeFont;
	CTrueTypeFontFactory	cFactory;
	CImageColourRGB			cColour;
	CChars					szCharacters;
	Ptr<CFontDescription>	pDescription;
	Ptr<CImage>				pImage;
	Ptr<CFont>				pFont;

	cTrueTypeFont.Init();
	if (!cTrueTypeFont.Load((char*)szFontFilename))
	{
		cTrueTypeFont.Kill();
		gcLogger.Error2(__METHOD__, " Could not load font [", szFontFilename, "].", NULL);
		return NULL;
	}

	cColour.Init(1.0f, 1.0f, 1.0f);
	szCharacters.Init(HCP65_EMULATOR_FONT_CHARACTERS);
	cFactory.Init(&cTrueTypeFont, fPixelSize);
	pDescription = cFactory.Describe((char*)szName, szCharacters.Text());
	pImage = cFactory.Rasterise(pDescription, &cColour);
	if (pImage.IsNotNull())
	{
		if (pImage->GetColourOrder() != eOrder)
		{
			pImage = CImageColourConverter::ConvertColour(pImage, eOrder);
		}
		pFont = OMalloc<CFont>(pDescription, pImage);
	}
	cFactory.Kill();
	szCharacters.Kill();
	cTrueTypeFont.Kill();

	if (pFont.IsNull())
	{
		gcLogger.Error2(__METHOD__, " Could not make a font from [", szFontFilename, "].", NULL);
	}
	return pFont;
}


//////////////////////////////////////////////////////////////////////////
//
// The kernal ROM image szFilename into the HCP65's ROM.
//
//////////////////////////////////////////////////////////////////////////
bool LoadKernal(CHCP65* pcHCP65, const char* szFilename)
{
	CDiskFile	cFile;
	uint8*		puiROM;
	lsize		uiSize;
	bool		bResult;

	cFile.Init(szFilename);
	if (!cFile.Open(EFM_Read))
	{
		cFile.Kill();
		gcLogger.Error2(__METHOD__, " Could not open the kernal ROM [", szFilename, "].", NULL);
		return false;
	}
	uiSize = (lsize)cFile.Size();
	if (uiSize > HCP65_MPU_ROM_SIZE)
	{
		uiSize = HCP65_MPU_ROM_SIZE;
	}
	puiROM = (uint8*)malloc(uiSize);
	bResult = cFile.Read(puiROM, uiSize, 1) == 1;
	cFile.Close();
	cFile.Kill();
	if (bResult)
	{
		bResult = pcHCP65->LoadROM(puiROM, (size)uiSize);
	}
	free(puiROM);
	if (!bResult)
	{
		gcLogger.Error2(__METHOD__, " Could not load the kernal ROM [", szFilename, "].", NULL);
	}
	return bResult;
}


//////////////////////////////////////////////////////////////////////////
//
// The HCP65 (its kernal ROM, the sound board and an SD card board with
// Input/SDCard.img) in a window with a menu bar, run by the window's ticks
// until the window closes.  pcInput has the platform's keyboard and mouse.
//
//////////////////////////////////////////////////////////////////////////
bool HCP65EmulatorRun(CNativeWindowFactory* pcNativeFactory, CInput* pcInput)
{
	CHCP65						cHCP65;
	CHCP65Audio					cAudio;
	CHCP65SDCard				cSDCard;
	CHostAudio					cHostAudio;
	Ptr<CWindow>				pWindow;
	Ptr<CHCP65EmulatorTick>		pTick;
	Ptr<CGridContainer>			pMain;
	Ptr<CMenuBar>				pMenuBar;
	Ptr<CMenuContainer>			pFileMenu;
	Ptr<CCanvas>				pScreen;
	CFileMenuListener			cFileMenuListener;
	Ptr<CWindowTheme>			pTheme;
	Ptr<CFont>					pWhite;
	Ptr<CFontColours>			pFontColours;
	Ptr<CWindowInputSource>		pInputSource;
	CComponentInput				cComponentInput;
	bool						bResult;

	cHCP65.Init();
	cAudio.Init();
	cSDCard.Init();
	cHostAudio.Init();
	bResult = LoadKernal(&cHCP65, HCP65_EMULATOR_KERNAL_ROM);
	if (!cSDCard.InsertCard(HCP65_EMULATOR_SD_CARD_IMAGE, false))
	{
		gcLogger.Error2(__METHOD__, " Could not insert the SD card [", HCP65_EMULATOR_SD_CARD_IMAGE, "].", NULL);
		bResult = false;
	}
	cHCP65.SetDevice(HCP65_EMULATOR_SD_SOCKET, &cSDCard);
	cHCP65.SetDevice(HCP65_EMULATOR_AUDIO_SOCKET, &cAudio);
	cHCP65.Reset();

	if (bResult)
	{
		//Validating every object each time one changes is too slow for the text's glyphs.  They are validated once at the end.
		gcObjects.DisableValidation();

		//The window is in the root so that changing pointers into its components doesn't make the objects work out again which
		//of them are still on the stack.  Its tick is allocated, not on the stack, because stack objects can't be under the root.
		pTick = OMalloc<CHCP65EmulatorTick>(&cHCP65, &cAudio, &cHostAudio);
		pWindow = OMalloc<CWindow>("HCP65 Emulator", pcNativeFactory, pTick, OMalloc<CClearCanvasDraw>(JEWEL_PANEL), CFT_RGBX, CCO_BGR, CRGB_24bit, ARGB_8bit);
		ORoot()->Add(pWindow);

		//The font is white and coloured as it is drawn.
		pWhite = GenerateFont("Resources/InterVariable.ttf", "Inter 13px", 13.0f, pWindow->GetColourOrder());
		if (pWhite.IsNotNull())
		{
			pFontColours = OMalloc<CFontColours>(pWhite);
			pTheme = CJewelTheme::CreateDarkTheme(pWindow, pFontColours->GetFont(JEWEL_TEXT));

			//The menu bar along the top over the HCP65's screen, which is only a blank canvas so far.
			pMenuBar = OMalloc<CMenuBar>(pWindow, pTheme);
			pMenuBar->SetAlignment(CSH_Stretch, CSV_Stretch);
			pFileMenu = OMalloc<CMenuContainer>(pWindow, pTheme);
			pMenuBar->AddMenu("File", pFileMenu);
			pScreen = OMalloc<CCanvas>(pWindow, OMalloc<CClearCanvasDraw>(JEWEL_EDITOR));
			pScreen->SetAlignment(CSH_Stretch, CSV_Stretch);
			pMain = OMalloc<CGridContainer>(pWindow);
			pMain->AddComponent(pMenuBar, 0, 0, 1, 1, 1, 0);
			pMain->AddComponent(pScreen, 0, 1, 1, 1, 1, 1);
			pWindow->SetContainer(pMain);

			cFileMenuListener.Init(pWindow, pFileMenu);
			pMenuBar->AddListener(&cFileMenuListener);

			pInputSource = OMalloc<CWindowInputSource>(pcInput);
			cComponentInput.Init(&pInputSource, &pWindow);
			cComponentInput.AddKeyboards();
			cComponentInput.AddMice();
			pWindow->SetComponentInput(&cComponentInput);

			pWindow->SetWindowSize(1280, 800);
			pWindow->Show();

			pMenuBar->RemoveListener(&cFileMenuListener);
			cFileMenuListener.Kill();

			pWindow->SetComponentInput(NULL);
			cComponentInput.Kill();
			pInputSource = NULL;
		}
		else
		{
			bResult = false;
		}

		pScreen = NULL;
		pFileMenu = NULL;
		pMenuBar = NULL;
		pMain = NULL;
		pTheme = NULL;
		pFontColours = NULL;
		pWhite = NULL;
		ORoot()->Remove(pWindow);
		pWindow = NULL;
		pTick = NULL;
		ORoot()->RemoveAll();

		gcObjects.EnableValidation();
		gcObjects.ValidateObjectsConsistency();
	}

	cHostAudio.Kill();
	cHCP65.Kill();
	cSDCard.Kill();
	cAudio.Kill();
	return bResult;
}

