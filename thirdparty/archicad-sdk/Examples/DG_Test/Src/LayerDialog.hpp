// *****************************************************************************
// Header file for the Layer Dialog in DG Test Add-On
// *****************************************************************************

#if !defined (LAYERDIALOG_H)
#define LAYERDIALOG_H

#pragma once

#include	"DGModule.hpp"
#include 	"UCModule.hpp"

enum ConvertStatus {
	Convert 		= 0,
	DontConvert 	= 1,
	LoadBearing 	= 2,
	NonLoadBearing 	= 3
};

struct LayerStatus {
	GS::UniString	layerName;
	short			layerFlags;
	ConvertStatus	convStatus;
};

struct ListBoxRow {
	GS::UniString	layerName;
	GS::UniString	flagName;
	GS::UniString	convStatName;
};

enum TabId {
	LayerNameTab		= 1,
	StatusTab			= 2,
	FilterStatusTab		= 3,
	PopUpTab 			= 4
};


inline GSErrCode	Read (GS::IChannel& ic, ConvertStatus& cs)
{
	GSErrCode  err = ic.Read ((int&) cs);
	return err;
}


inline GSErrCode	Write (GS::OChannel& oc, const ConvertStatus& cs)
{
	GSErrCode  err = oc.Write ((int&) cs);
	return err;
}


// --- LayerDialog -------------------------------------------------

class LayerDialog: public DG::ModalDialog
{
friend class	LayerObserver;

protected:
	static const short NTabFields = PopUpTab;
	short						returnHotKey;
	short						enterHotKey;



	static const TabId		layerNameTab	= LayerNameTab;
	static const TabId		statusTab		= StatusTab;
	static const TabId		filterStatusTab	= FilterStatusTab;
	static const TabId		popUpTab		= PopUpTab;
	TabId&					lastSortedTab;
	bool&					lastDirectionUp;			//false=Down, true=Up

	GS::Array<ListBoxRow> 							sortedList;
	GS::HashTable<GS::UniString, ConvertStatus>&	statusTable;
	DG::MultiSelListBox								layerList;

	GS::UniString			FlagsToText (short layerFlags) const;
	GS::UniString			StatusToText (ConvertStatus filterStatus) const;


	void					SortListBy (const TabId sortby);
	void					RebuildListContent ();

private:
	static const short LAYER_DIALOG_RESID = 32591;
	static const short LAYER_DIALOG_CONV_STRINGS_RESID = 32591;
	static const short LAYER_DIALOG_FLAG_STRINGS_RESID = 32592;
	static const short LAYER_DIALOG_LIST_BOX_HEADER_STRINGS_RESID = 32593;

	enum {
		OkButtonId				= 1,
		CancelButtonId			= 2,
		searchEditId			= 4,
		SearchButtonId			= 5,
		FilterPopUpId			= 6,
		ListBoxId				= 7
	};


	DG::Button					okButton;
	DG::Button					cancelButton;
	DG::Button 					searchButton;
	DG::TextEdit 				searchEdit;
	UC::TextPopup	 			filterStatusPopup;



public:
	LayerDialog	(GSResModule dialResModule, GS::HashTable<GS::UniString, ConvertStatus>& statusTableIn, TabId& lastSortedTabIn, bool& lastDirectionUpIn);
	~LayerDialog ();


};

// --- LayerObserver -----------------------------------------------

class LayerObserver:	public		DG::PanelObserver,
						public		DG::ListBoxObserver,
						public		DG::ButtonItemObserver,
						public		DG::UserControlObserver,
						public		DG::CompoundItemObserver
{
private:
	LayerDialog*		dialog;

protected:
	virtual void	PanelOpened (const DG::PanelOpenEvent& ev) override;
	virtual void	PanelResized (const DG::PanelResizeEvent& ev) override;

	virtual void	ListBoxClicked (const DG::ListBoxClickEvent & ev) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;
	virtual void 	ListBoxHeaderItemClicked (const DG::ListBoxHeaderItemClickEvent& ev) override;
	virtual void	PanelHotkeyPressed (const DG::PanelHotKeyEvent& ev, bool* processed) override;
	virtual void	UserControlChanged (const DG::UserControlChangeEvent& ev) override;
	virtual void 	ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev) override;

	virtual void	Search ();
	virtual void	SetPopUpAfterSelection ();

public:
	explicit		LayerObserver (LayerDialog* testDialog);
					~LayerObserver ();
};

#endif // LAYERDIALOG_H
