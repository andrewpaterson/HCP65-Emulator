#ifndef __FILE_MENU_LISTENER_H__
#define __FILE_MENU_LISTENER_H__
#include "WindowLib/Window.h"
#include "WindowLib/MenuContainer.h"
#include "WindowLib/MenuListener.h"


//The File menu.  Exit stops the window's ticks, which closes it.
class CFileMenuListener : public CMenuListener
{
CONSTRUCTABLE(CFileMenuListener);
protected:
	Ptr<CWindow>	mpWindow;
	Ptr<CMenuItem>	mpExit;

public:
	void	Init(Ptr<CWindow> pWindow, Ptr<CMenuContainer> pFileMenu);
	void	Kill(void);

	void	MenuItemClicked(CUnknown* pcSource, void* pvContext) override;
};


#endif // __FILE_MENU_LISTENER_H__
