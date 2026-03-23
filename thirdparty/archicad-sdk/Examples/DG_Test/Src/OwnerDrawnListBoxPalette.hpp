// *****************************************************************************
// Header file for the Owner Drawn ListBox Dialog in DG Test Add-On
// *****************************************************************************

#if !defined (OWNERDRAWNLISTBOXDIALOG_H)
#define OWNERDRAWNLISTBOXDIALOG_H

#pragma once

#include	"APIEnvir.h"
#include	"ACAPinc.h"

#include	"DGModule.hpp"
#include	"MessageLoopExecutor.hpp"


// --- OwnerDrawnListBoxDialog -------------------------------------------------

class OwnerDrawnListBoxPalette:	public DG::Palette,
								public DG::PanelObserver,
								public DG::ListBoxObserver,
								public DG::ButtonItemObserver,
								public DG::CompoundItemObserver
{
private:
	enum {
		Tab1		= 1,
		Tab2		= 2,
		Tab3		= 3,
		NTabFields	= Tab3
	};

	enum {
		OkButtonId				= 1,
		SwitchImagesButtonId	= 2,
		GetButtonId				= 3,
		ListBoxId				= 4
	};

	DG::Button					okButton;
	DG::Button					switchImagesButton;
	DG::Button					getButton;
	DG::SingleSelListBox		listBox;

	OwnerDrawnListBoxPalette ();

public:
	struct Data {
		unsigned short state;

		GS::Array<API_Guid>									elements;
		GS::HashTable<API_Guid, NewDisplay::NativeImage>	elementImages;

		Data () : state (1) {}
	};

protected:
	Data data;
	GS::MessageLoopExecutor executor;

	virtual void	PanelOpened (const DG::PanelOpenEvent& ev) override;
	virtual	void	PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted) override;

	virtual void	ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;

	virtual void	ListBoxTabFieldUpdate (const DG::ListBoxTabItemUpdateEvent& ev) override;
	virtual void	UpdateTab1 (const DG::ListBoxTabItemUpdateEvent& ev);
	virtual void	UpdateTab2 (const DG::ListBoxTabItemUpdateEvent& ev);
	virtual void	UpdateTab3 (const DG::ListBoxTabItemUpdateEvent& ev);

	void			GetObjects ();

public:
	virtual ~OwnerDrawnListBoxPalette ();

	static GSErrCode PaletteAPIControlCallBack (Int32 referenceID, API_PaletteMessageID messageID, GS::IntPtr param);
	static Int32			ODLBPaletteRefId ();
	static const GS::Guid&	ODLBPaletteGuid ();
	static OwnerDrawnListBoxPalette& GetInstance ();
	inline DG::SingleSelListBox&	 GetListBox ()
	{
		return listBox;
	};
};


#endif // OWNERDRAWNLISTBOXDIALOG_H
