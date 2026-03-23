// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#ifndef	NAVIGATORPALETTE_TEST_HPP
#define	NAVIGATORPALETTE_TEST_HPP

#pragma once

#include	"APIEnvir.h"
#include	"ACAPinc.h"
#include	"DG.h"


class NavigatorPaletteTestGlobals
{
private:
	short						navigatorDialID;
	API_NavigatorMapID			navigatorMapID;
	GS::PagedArray<API_Guid>	navigatorItems;
	GS::HashSet<API_Guid>		openedItems;

private:
	NavigatorPaletteTestGlobals () : navigatorDialID (0), navigatorMapID (API_ProjectMap) {}

public:
	~NavigatorPaletteTestGlobals () { navigatorDialID = 0; navigatorMapID = API_ProjectMap; navigatorItems.Clear (); }

	static NavigatorPaletteTestGlobals& GetInstance () {
		static NavigatorPaletteTestGlobals navigatorPaletteTestGlobals;
		return navigatorPaletteTestGlobals;
	}

	void				SetNavigatorDialID (short dialID) { navigatorDialID = dialID; }
	short				GetNavigatorDialID () { return navigatorDialID; }
	void				SetNavigatorMapID (API_NavigatorMapID mapID) { navigatorMapID = mapID; }
	API_NavigatorMapID	GetNavigatorMapID () { return navigatorMapID; }
	UIndex				AddNavigatorItem (API_Guid item) { navigatorItems.Push (item); return (navigatorItems.GetSize () - 1); }
	void				DeleteNavigatorItem (UIndex itemIndex) { if (itemIndex < navigatorItems.GetSize ()) { navigatorItems.Delete (itemIndex); } }
	API_Guid			GetNavigatorItem (UIndex itemIndex) { if (itemIndex < navigatorItems.GetSize ()) { return navigatorItems[itemIndex]; } else { return APINULLGuid; } }

	void				SetNavigatorItemOpen (UIndex itemIndex, bool open)
	{
		if (itemIndex < navigatorItems.GetSize ()) {
			if (open)
				openedItems.Add (navigatorItems[itemIndex]);
			else
				openedItems.Delete (navigatorItems[itemIndex]);
		}
	}
	bool				IsNavigatorItemOpen (UIndex itemIndex) { if (itemIndex < navigatorItems.GetSize ()) { return openedItems.Contains (navigatorItems[itemIndex]); } else { return false; } }
	void				ResetOpened (void) { openedItems.Clear (); }
};

class NavigatorPalette {
public:
	static short DGCALLBACK APINavigator_Handler (	short			message,
													short 			dialogID,
													short			item,
													DGUserData		/*userData*/,
													DGMessageData	msgData);
};

#endif