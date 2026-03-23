// *****************************************************************************
// Source code for the Layer Dialog in DG Test Add-On
// *****************************************************************************

#include	"LayerDialog.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"

#include	"Algorithms.hpp"

//---------------------------- Class LayerDialog -----------------------

LayerDialog::LayerDialog (GSResModule dialResModule, GS::HashTable<GS::UniString, ConvertStatus>& statusTableIn, TabId& lastSortedTabIn, bool& lastDirectionUpIn):
	DG::ModalDialog		(dialResModule, LAYER_DIALOG_RESID, dialResModule),

	layerList			(GetReference (), ListBoxId),
	okButton			(GetReference (), OkButtonId),
	cancelButton		(GetReference (), CancelButtonId),
	searchEdit			(GetReference (), searchEditId),
	searchButton		(GetReference (), SearchButtonId),
	filterStatusPopup	(GetReference (), FilterPopUpId),

	lastSortedTab 		(lastSortedTabIn),
	lastDirectionUp 	(lastDirectionUpIn),
	statusTable 		(statusTableIn)
{
	const short width 			= layerList.GetItemWidth ();
	const short popUpTabWidth	= filterStatusPopup.GetWidth ();
	const short tabWidth 		= (width - popUpTabWidth) / 3;
	returnHotKey				= RegisterHotKey (DG::Key::Return);
	enterHotKey					= RegisterHotKey (DG::Key::Enter);
	// initialize the listbox
	layerList.SetTabFieldCount (NTabFields);

	short pos = 0;

	layerList.SetHeaderSynchronState (false);
	layerList.SetHeaderItemSize (layerNameTab,		tabWidth);
	layerList.SetHeaderItemSize (statusTab,			tabWidth);
	layerList.SetHeaderItemSize (filterStatusTab,	tabWidth + popUpTabWidth);
	layerList.SetHeaderItemSize (popUpTab,			0);

	layerList.SetTabFieldProperties (layerNameTab,		pos, pos + tabWidth,		DG::ListBox::Left,	DG::ListBox::NoTruncate, false);
	pos += tabWidth;
	layerList.SetTabFieldProperties (statusTab,			pos, pos + tabWidth,		DG::ListBox::Left,	DG::ListBox::NoTruncate, false);
	pos += tabWidth;
	layerList.SetTabFieldProperties (filterStatusTab,	pos, width - popUpTabWidth,	DG::ListBox::Left,	DG::ListBox::NoTruncate, false);
	pos = width - popUpTabWidth;
	layerList.SetTabFieldProperties (popUpTab,			pos, width,					DG::ListBox::Left,	DG::ListBox::EndTruncate, false);

	// set texts:
	GS::UniString	headerName;
	RSGetIndString (&headerName, LAYER_DIALOG_LIST_BOX_HEADER_STRINGS_RESID, 1, ACAPI_GetOwnResModule());
	layerList.SetHeaderItemText (layerNameTab,		headerName);

	RSGetIndString (&headerName, LAYER_DIALOG_LIST_BOX_HEADER_STRINGS_RESID, 2, ACAPI_GetOwnResModule());
	layerList.SetHeaderItemText (statusTab,			headerName);

	RSGetIndString (&headerName, LAYER_DIALOG_LIST_BOX_HEADER_STRINGS_RESID, 3, ACAPI_GetOwnResModule());
	layerList.SetHeaderItemText (filterStatusTab,	headerName);


	layerList.SetHeaderPushableButtons (true);

	ACAPI_Attribute_EnumerateAttributesByType (API_LayerID, [&] (API_Attribute& attr) {
		if (attr.header.index == APIApplicationLayerAttributeIndex)
			return;

		ListBoxRow	row;
		GS::UniString attrName = *attr.header.uniStringNamePtr;
		if (!statusTable.ContainsKey (attrName)) {
			statusTable.Add (attrName, Convert);

			row.layerName = attrName;
			row.flagName = FlagsToText (attr.header.flags);
			row.convStatName = StatusToText (Convert);
			sortedList.Push (row);
		} else {
			row.layerName = attrName;
			row.flagName = FlagsToText (attr.header.flags);
			row.convStatName = StatusToText (statusTable[attrName]);
			sortedList.Push (row);
		}
	});

	filterStatusPopup.Initialize (StatusToText (Convert), 100, 1);
	filterStatusPopup.AppendItem (StatusToText (Convert));
	filterStatusPopup.AppendItem (StatusToText (DontConvert));
	filterStatusPopup.AppendItem (StatusToText (LoadBearing));
	filterStatusPopup.AppendItem (StatusToText (NonLoadBearing));
	filterStatusPopup.Hide ();

	if (lastDirectionUp) {
		layerList.SetHeaderItemArrowType ((short) lastSortedTab, DG::ListBox::Down);
	} else {
		layerList.SetHeaderItemArrowType ((short) lastSortedTab, DG::ListBox::Up);
	}

	SortListBy (lastSortedTab);
}


LayerDialog::~LayerDialog ()
{
	UnregisterHotKey (returnHotKey);
	UnregisterHotKey (enterHotKey);
}


GS::UniString LayerDialog::FlagsToText(short layerFlags) const
{
	GS::UniString				lfText;
	GS::Array<GS::UniString>	lfTextsList;

	if (layerFlags & APILay_Hidden)
		lfTextsList.Push (RSGetIndString (LAYER_DIALOG_FLAG_STRINGS_RESID, 1, ACAPI_GetOwnResModule()));
	if (layerFlags & APILay_Locked)
		lfTextsList.Push (RSGetIndString (LAYER_DIALOG_FLAG_STRINGS_RESID, 2, ACAPI_GetOwnResModule()));
	if (layerFlags & APILay_NotMine)
		lfTextsList.Push (RSGetIndString (LAYER_DIALOG_FLAG_STRINGS_RESID, 3, ACAPI_GetOwnResModule()));
	if (layerFlags & APILay_Xref)
		lfTextsList.Push (RSGetIndString (LAYER_DIALOG_FLAG_STRINGS_RESID, 4, ACAPI_GetOwnResModule()));

	for (short i = 0; i < (short) lfTextsList.GetSize (); ++i) {
		if (!lfText.IsEmpty ())
			lfText += ", ";
		lfText += lfTextsList [i];
	}

	return lfText;
}


GS::UniString	LayerDialog::StatusToText (ConvertStatus filterStatus) const
{
	return RSGetIndString (LAYER_DIALOG_CONV_STRINGS_RESID, (short) filterStatus + 1, ACAPI_GetOwnResModule ());
}


void LayerDialog::RebuildListContent ()
{
	layerList.DisableDraw ();
	if (layerList.GetItemCount () != 0) {
		layerList.DeleteItem (DG::ListBox::AllItems);
	}
	for (short i = 1; i <= static_cast<short>(sortedList.GetSize ()); ++i) {
		const ListBoxRow& row = sortedList[i-1];
		layerList.AppendItem ();
		layerList.SetTabItemText (i, LayerDialog::layerNameTab, 	row.layerName);
		layerList.SetTabItemText (i, LayerDialog::statusTab, 		row.flagName);
		layerList.SetTabItemText (i, LayerDialog::filterStatusTab, 	row.convStatName);
	}
	layerList.EnableDraw ();
	layerList.Redraw ();
}


void LayerDialog::SortListBy (const TabId sortBy)
{
	class ElementSorter {
	private:
		TabId					sortBy;
		DG::ListBox::ArrowType	sortDirection;
	public:
		ElementSorter (GS::Array<ListBoxRow>& rows, TabId sortBy, DG::ListBox::ArrowType lastArrow) :
			sortBy (sortBy)
		{
			if (lastArrow == DG::ListBox::Up) {
				sortDirection = DG::ListBox::Down;
			} else {
				sortDirection = DG::ListBox::Up;
			}

			GS::Sort (rows.Begin (), rows.End (), *this);
		}

		bool operator () (const ListBoxRow& row1, const ListBoxRow& row2) {
			Int32 cmpRes = 0;
			GS::UniString::CompareResult result;
			GS::UniString name1, name2;
			switch (sortBy) {
				case LayerDialog::layerNameTab:
					name1 = row1.layerName;
					name2 = row2.layerName;
					break;
				case LayerDialog::statusTab:
					name1 = row1.flagName;
					name2 = row2.flagName;
					break;
				case LayerDialog::filterStatusTab:
					name1 = row1.convStatName;
					name2 = row2.convStatName;
					break;
				default:
					DBBREAK ();
					break;
			}

			result = name1.Compare (name2, GS::CaseInsensitive);

			GS::UniString::CompareResult whatIsLess = (sortDirection == DG::ListBox::Up ? GS::UniString::Less : GS::UniString::Greater);
			cmpRes = (result == GS::UniString::Equal) ? 0 : ((result == whatIsLess) ? -1 : 1);
			return cmpRes < 0;
		}

		DG::ListBox::ArrowType GetNewArrow () const {
			return sortDirection;
		}
	};

	ElementSorter sorter (sortedList, sortBy, layerList.GetHeaderItemArrowType ((short) sortBy));
	layerList.SetHeaderItemArrowType ((short) lastSortedTab, DG::ListBox::NoArrow);
	layerList.SetHeaderItemArrowType ((short) sortBy, sorter.GetNewArrow ());
	lastSortedTab = sortBy;
	if (layerList.GetHeaderItemArrowType ((short) sortBy) == DG::ListBox::Down) {
		lastDirectionUp = false;
	} else {
		lastDirectionUp = true;
	}

	RebuildListContent ();
}

//-------------------------- Class LayerObserver -----------------------

LayerObserver::LayerObserver (LayerDialog* testDialog):
	dialog 	 (testDialog)
{
	dialog->Attach (*this);
	AttachToAllItems (*dialog);
}


LayerObserver::~LayerObserver ()
{
	dialog->Detach (*this);
	DetachFromAllItems (*dialog);
}


void LayerObserver::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
	dialog->SetClientSize (dialog->GetOriginalClientWidth (), dialog->GetOriginalClientHeight ());
}


void LayerObserver::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &dialog->okButton) {
		dialog->PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &dialog->cancelButton) {
		dialog->PostCloseRequest (DG::ModalDialog::Cancel);
	} else if (ev.GetSource () == &dialog->searchButton) {
		Search();
	}
}

void LayerObserver::SetPopUpAfterSelection ()
{
	short 	selItem = 0;

	// sets the value of the popup to the first selected item's Filter Status
	selItem = dialog->layerList.GetSelectedItem (selItem + 1);
	GS::UniString layName = dialog->layerList.GetTabItemText (selItem, LayerDialog::layerNameTab);
	dialog->filterStatusPopup.SetValue ((short) dialog->statusTable[layName] + 1);

	dialog->layerList.SetOnTabItem (LayerDialog::popUpTab, dialog->filterStatusPopup);
}


void LayerObserver::Search ()
{
	dialog->layerList.RemoveOnTabItem (LayerDialog::popUpTab);
	dialog->layerList.DeselectItem (DG_ALL_ITEMS);

	GS::UniString	text 			= dialog->searchEdit.GetText ();
	short 			nLayers 		= dialog->layerList.GetItemCount();
	bool			noSelectedItems = true;
	text.SetToLowerCase();

	for (short j = 1; j <= nLayers; ++j) {
		GS::UniString	tabText = dialog->layerList.GetTabItemText (j, LayerDialog::layerNameTab);
		tabText.SetToLowerCase();

		if (tabText.Contains (text)) {
			if (noSelectedItems)
				noSelectedItems = false;

			dialog->layerList.SelectItem (j);
		}
	}
	if (!noSelectedItems) {
		SetPopUpAfterSelection ();
	}
}


void LayerObserver::PanelHotkeyPressed (const DG::PanelHotKeyEvent& ev, bool* processed)
{
	short keyId = ev.GetKeyId ();

	if (keyId == dialog->returnHotKey || keyId == dialog->enterHotKey) {
		*processed = false;

		if (dialog->searchEdit.IsFocused ()) {
			Search();
			dialog->layerList.SetFocus ();
			*processed = true;
		}
	}
}


void LayerObserver::ListBoxClicked (const DG::ListBoxClickEvent& /*ev*/)
{
}


void LayerObserver::PanelResized (const DG::PanelResizeEvent& ev)
{
	short vGrow = ev.GetVerticalChange ();
	if (vGrow != 0) {
		dialog->BeginMoveResizeItems ();

		dialog->okButton.Move		(0, vGrow);
		dialog->cancelButton.Move 	(0, vGrow);
		dialog->layerList.Resize	(0, vGrow);

		dialog->EndMoveResizeItems ();
	}
}


void LayerObserver::ListBoxHeaderItemClicked (const DG::ListBoxHeaderItemClickEvent& ev)
{
	if (ev.GetSource () == &dialog->layerList) {
		TabId clickedTab = (TabId) (ev.GetHeaderItem ());
		dialog->SortListBy (clickedTab);
	}
}


void LayerObserver::UserControlChanged (const DG::UserControlChangeEvent& ev)
{
	if (ev.GetSource () == &dialog->filterStatusPopup) {
		short	selRowsNr 			= DGListGetSelCount (LayerDialog::LAYER_DIALOG_RESID, LayerDialog::ListBoxId);
		short 	selItem 			= 0;
		ConvertStatus newConvStat 	= (ConvertStatus) (dialog->filterStatusPopup.GetValue () - 1);

		for (short i = 0; i < selRowsNr; ++i) {
			selItem = DGListGetSelected (LayerDialog::LAYER_DIALOG_RESID, LayerDialog::ListBoxId, selItem + 1);

			GS::UniString layName 							= dialog->layerList.GetTabItemText (selItem, LayerDialog::layerNameTab);
			dialog->statusTable[layName] 					= newConvStat;
			dialog->sortedList[selItem - 1].convStatName 	= dialog->filterStatusPopup.GetItemText (dialog->filterStatusPopup.GetValue ());
			dialog->layerList.SetTabItemText (selItem, LayerDialog::filterStatusTab, dialog->sortedList[selItem - 1].convStatName);
		}
	}
}


void LayerObserver::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev)
{
	if (ev.GetSource () == &dialog->layerList) {
		SetPopUpAfterSelection ();
	}
}
