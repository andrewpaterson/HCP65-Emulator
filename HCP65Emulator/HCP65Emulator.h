#ifndef __HCP65_EMULATOR_H__
#define __HCP65_EMULATOR_H__


//Relative to the emulator's directory (HCP65Emulator/HCP65Emulator), as Visual Studio runs it.  TestHCP65Lib's
//TestHCP65Kernal builds the ROM and the SD card image (boot.exe, modplay.exe, SpaceDebris.mod) into Input.
#define HCP65_EMULATOR_KERNAL_ROM		"Input/Kernal.rom"
#define HCP65_EMULATOR_SD_CARD_IMAGE	"Input/SDCard.img"
#define HCP65_EMULATOR_SD_SOCKET		2			//Socket 1 is the main memory channel.
#define HCP65_EMULATOR_AUDIO_SOCKET		16


class CNativeWindowFactory;
class CInput;


bool HCP65EmulatorRun(CNativeWindowFactory* pcNativeFactory, CInput* pcInput);


#endif // __HCP65_EMULATOR_H__
