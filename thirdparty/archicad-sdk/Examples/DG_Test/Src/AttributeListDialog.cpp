// *****************************************************************************
// Source code for the AttributeList Example Dialog in DG Test Add-On
// *****************************************************************************

#include	"AttributeListDialog.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

//---------------------------- Class AttributeListDialog -----------------------

AttributeListDialog::AttributeListDialog ():
	DG::ModalDialog		(ACAPI_GetOwnResModule (), ATTRIBUTE_LIST_DIALOG_RESID, ACAPI_GetOwnResModule ()),

	okButton			(GetReference (), OKButtonId),
	attributeList		(GetReference (), AttributeListId)
{
	Attach (*this);
	AttachToAllItems (*this);
	penItem.Attach (*this);
	fillItem.Attach (*this);
	fillBackgroundPenItem.Attach (*this);
	fillForegroundPenItem.Attach (*this);
	fillContourPenItem.Attach (*this);
	checkItem.Attach (*this);
	selectionItem.Attach (*this);
	materialItem.Attach (*this);
	lineTypeItem.Attach (*this);
	textItem.Attach (*this);
	angleItem.Attach (*this);
	buttonItem.Attach (*this);
	compositeBackgroundPenItem.Attach (*this);
	compositeForegroundPenItem.Attach (*this);
	compositeSeparatorPenItem.Attach (*this);
	compositeContourPenItem.Attach (*this);

	attributeList.DisableUpdate ();

	// Init attribute list items
	firstGroup.SetName ("CUT ELEMENTS - FIRST GROUP");

	{
		selectionItem.SetName ("Fill Cut Surfaces with:");

		ACAPI::UI::AttributeListSelectionItem::Item item;

		item.text = "Uniform Pen Color";
		item.icon = DG::Icon (ACAPI_GetOwnResModule (), 27101);
		selectionItem.AddItem (item);

		item.text = "Own Material Color";
		item.icon = DG::Icon (ACAPI_GetOwnResModule (), 27102);
		selectionItem.AddItem (item);

		item.text = "Own Shaded Material Color";
		item.icon = DG::Icon (ACAPI_GetOwnResModule (), 27103);
		selectionItem.AddItem (item);

		firstGroup.AddItem (selectionItem);
	}

	checkItem.SetName ("Capture Material From 3D Cutting Planes Settings");
	checkItem.SetChecked (true);
	firstGroup.AddItem (checkItem);

	materialItem.SetName ("Uniform Material");
	materialItem.SetMaterial (ACAPI_CreateAttributeIndex (56) /* Brick - Red */);
	firstGroup.AddItem (materialItem);

	penItem.SetName ("Cut Line Pen");
	penItem.SetIcon (DG::Icon (ACAPI_GetOwnResModule (), 27104));
	penItem.SetPen (24);
	firstGroup.AddItem (penItem);

	secondGroup.SetName ("BOUNDARY CONTOURS - SECOND GROUP");

	lineTypeItem.SetName ("Boundary Line Type");
	lineTypeItem.SetIcon (DG::Icon (ACAPI_GetOwnResModule (), 27106));
	lineTypeItem.SetLineType (ACAPI_CreateAttributeIndex (9) /* Zigzag */);
	secondGroup.AddItem (lineTypeItem);

	{ // fillGroup
		fillGroup.SetName ("Fill Attribute Group");

		fillItem.SetName ("Cut Fill");
		fillItem.SetIcon (DG::Icon (ACAPI_GetOwnResModule (), 27105));
		fillItem.SetFill (ACAPI_CreateAttributeIndex (56) /* Block - Stack Bond */);
		fillGroup.AddItem (fillItem);

		fillForegroundPenItem.SetName ("Foreground color");
		fillForegroundPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPen);
		fillForegroundPenItem.SetPen (3);
		fillGroup.AddItem (fillForegroundPenItem);

		fillBackgroundPenItem.SetName ("Background color");
		fillBackgroundPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPenWNull);
		fillBackgroundPenItem.SetPen (4);
		fillGroup.AddItem (fillBackgroundPenItem);

		fillContourPenItem.SetName ("Frame color");
		fillContourPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPenWNull);
		fillContourPenItem.SetPen (5);
		fillGroup.AddItem (fillContourPenItem);

		fillItem.SetForegroundPen (fillForegroundPenItem.GetPen ());
		fillItem.SetBackgroundPen (fillBackgroundPenItem.GetPen ());
		fillItem.SetContourPen (fillContourPenItem.GetPen ());
	}
	{ // compositeGroup
		compositeGroup.SetName ("Composite Attribute Group");

		compositeItem.SetName ("My Composite");
		compositeItem.SetSubType (ACAPI::UI::AttributeListCompositeItem::AllComposites);
		compositeItem.SetComposite (ACAPI_CreateAttributeIndex (2));
		compositeGroup.AddItem (compositeItem);

		compositeForegroundPenItem.SetName ("Foreground color");
		compositeForegroundPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPen);
		compositeForegroundPenItem.SetPen (10);
		compositeGroup.AddItem (compositeForegroundPenItem);

		compositeBackgroundPenItem.SetName ("Background color");
		compositeBackgroundPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPenWNull);
		compositeBackgroundPenItem.SetPen (0);
		compositeGroup.AddItem (compositeBackgroundPenItem);

		compositeContourPenItem.SetName ("Frame color");
		compositeContourPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPenWNull);
		compositeContourPenItem.SetPen (12);
		compositeGroup.AddItem (compositeContourPenItem);

		compositeSeparatorPenItem.SetName ("Separator color");
		compositeSeparatorPenItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPenWNull);
		compositeSeparatorPenItem.SetPen (40);
		compositeGroup.AddItem (compositeSeparatorPenItem);

		compositeItem.SetForegroundPen (compositeForegroundPenItem.GetPen ());
		compositeItem.SetBackgroundPen (compositeBackgroundPenItem.GetPen ());
		compositeItem.SetContourPen (compositeContourPenItem.GetPen ());
		compositeItem.SetSeparatorPen (compositeSeparatorPenItem.GetPen ());
	}

	// Adding groups to the AttributeList
	attributeList.AddListItem (firstGroup);
	attributeList.AddListItem (secondGroup);
	attributeList.AddListItem (fillGroup);
	attributeList.AddListItem (compositeGroup);

	attributeList.AddListItem (separatorItem);

	textItem.SetName ("Text Selection Item");
	// Custom will be the last item on the TextItem
	textItem.SetText ("Custom text");
	// The string items will be before the Custom
	textItem.AddStringItem ("First text");
	textItem.AddStringItem ("Second text");
	textItem.AddStringItem ("Third text");
	attributeList.AddListItem (textItem);

	angleItem.SetName ("Sun Azimuth");
	angleItem.SetIcon (DG::Icon (ACAPI_GetOwnResModule (), 27107));
	angleItem.SetValue (60);
	attributeList.AddListItem (angleItem);

	buttonItem.SetName ("Button Item");
	buttonItem.SetButtonText ("Click here!");
	attributeList.AddListItem (buttonItem);

	// Setting attributes
	selectionItem.SelectItem (1);
	materialItem.SetVisibility (selectionItem.GetSelected () == 1);
	penItem.SetSubType (ACAPI::UI::AttributeListPenItem::GMPen);
	fillItem.SetSubType (ACAPI::UI::AttributeListFillItem::ACPolyFill);
	lineTypeItem.SetSubType (ACAPI::UI::AttributeListLineTypeItem::ACSymbolLine);

	// Update
	attributeList.EnableUpdate ();
	attributeList.FullRefreshList ();
}


AttributeListDialog::~AttributeListDialog ()
{
	attributeList.DisableUpdate ();

	Detach (*this);
	DetachFromAllItems (*this);
	penItem.Detach (*this);
	fillItem.Detach (*this);
	checkItem.Detach (*this);
	selectionItem.Detach (*this);
	materialItem.Detach (*this);
	lineTypeItem.Detach (*this);
	textItem.Detach (*this);
	angleItem.Detach (*this);
	buttonItem.Detach (*this);
}


void AttributeListDialog::PanelResized (const DG::PanelResizeEvent& ev)
{
	short hGrow = ev.GetHorizontalChange ();
	short vGrow = ev.GetVerticalChange ();
	if (hGrow != 0 || vGrow != 0) {
		BeginMoveResizeItems ();
		okButton.MoveAndResize (0, vGrow, hGrow, 0);
		attributeList.Resize (hGrow, vGrow);
		EndMoveResizeItems ();
	}
}


void AttributeListDialog::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& /*ev*/)
{
}


void	AttributeListDialog::PenChanged (const ACAPI::UI::AttributeListPenItem& source)
{
	WriteReport ("Pen changed to: %d", source.GetPen ());

	if (&source == &fillBackgroundPenItem) {
		fillItem.SetBackgroundPen (fillBackgroundPenItem.GetPen ());
	} else if (&source == &fillForegroundPenItem) {
		fillItem.SetForegroundPen (fillForegroundPenItem.GetPen ());
	} else if (&source == &fillContourPenItem) {
		fillItem.SetContourPen (fillContourPenItem.GetPen ());
	} else if (&source == &compositeBackgroundPenItem) {
		compositeItem.SetBackgroundPen (compositeBackgroundPenItem.GetPen ());
	} else if (&source == &compositeForegroundPenItem) {
		compositeItem.SetForegroundPen (compositeForegroundPenItem.GetPen ());
	} else if (&source == &compositeContourPenItem) {
		compositeItem.SetContourPen (compositeContourPenItem.GetPen ());
	} else if (&source == &compositeSeparatorPenItem) {
		compositeItem.SetSeparatorPen (compositeSeparatorPenItem.GetPen ());
	}
}


void	AttributeListDialog::FillChanged (const ACAPI::UI::AttributeListFillItem& source)
{
	if (&source == &fillItem) {
		WriteReport ("Fill changed to: %d", source.GetFill ());
	}
}


void	AttributeListDialog::CheckChanged (const ACAPI::UI::AttributeListCheckItem& source)
{
	if (&source == &checkItem) {
		WriteReport ("Check changed to: %s", source.IsChecked () ? "true" : "false");
	}
}


void	AttributeListDialog::SelectionChanged (const ACAPI::UI::AttributeListSelectionItem& source)
{
	if (&source == &selectionItem) {
		WriteReport ("Selection changed to: %d", source.GetSelected ());
	}
}


void	AttributeListDialog::MaterialChanged (const ACAPI::UI::AttributeListMaterialItem& source)
{
	if (&source == &materialItem) {
		WriteReport ("Material changed to: %d", source.GetMaterial ());
	}
}


void	AttributeListDialog::LineTypeChanged (const ACAPI::UI::AttributeListLineTypeItem& source)
{
	if (&source == &lineTypeItem) {
		WriteReport ("LineType changed to: %d", source.GetLineType ());
	}
}


void	AttributeListDialog::TextChanged (const ACAPI::UI::AttributeListTextItem& source)
{
	if (&source == &textItem) {
		WriteReport ("Text changed to: %s", source.GetText ().ToCStr ().Get ());
	}
}


void	AttributeListDialog::ValueChanged (const ACAPI::UI::AttributeListRealItem& source)
{
	if (&source == &angleItem) {
		WriteReport ("Value changed to: %f", source.GetValue ());
	}
}


void	AttributeListDialog::ButtonClicked (const ACAPI::UI::AttributeListButtonItem& source)
{
	if (&source == &buttonItem) {
		WriteReport ("Button clicked");
	}
}


void	AttributeListDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (Accept);
	}
}
