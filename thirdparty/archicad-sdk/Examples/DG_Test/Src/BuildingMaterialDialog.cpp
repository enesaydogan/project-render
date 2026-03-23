// *****************************************************************************
// Source code for the BuildingMaterial Dialog in DG Test Add-On
// *****************************************************************************

#include	"BuildingMaterialDialog.hpp"
#include	"DrawDialogItems.hpp"


//---------------------------- Class BuildingMaterialDialog -----------------------

BuildingMaterialDialog::BuildingMaterialDialog (GSResModule dialResModule, short resId):
	DG::ModalDialog		(dialResModule, resId, dialResModule),

	buildMatList		(GetReference (), ListBoxId),
	buildingMatControl	(GetReference (), BuildingMatControlId),
	penControl			(GetReference (), PenControlId),
	okButton			(GetReference (), OkButtonId)
{
	API_UCCallbackType	ucb;

	// initialize building material user controls
	ucb = {};
	ucb.dialogID = resId;
	ucb.type     = APIUserControlType_BuildingMaterial;
	ucb.itemID   = BuildingMatControlId;
	ACAPI_Dialog_SetUserControlCallback (&ucb);
	buildingMatControl.Hide ();

	// initialize pen user controls
	ucb = {};
	ucb.dialogID = resId;
	ucb.type     = APIUserControlType_Pen;
	ucb.itemID   = PenControlId;
	ACAPI_Dialog_SetUserControlCallback (&ucb);
	penControl.Hide ();

	const short width = buildMatList.GetItemWidth ();
	const short	PenControlTab_SIZE			= penControl.GetWidth ();
	const short	PenIndexTab_SIZE			= PenControlTab_SIZE;
	const short	BuildingMatControlTab_SIZE	= buildingMatControl.GetWidth ();
	const short	BuildingMatDrawTab_SIZE		= 50;
	const short	BuildingMatNameTab_SIZE		= width - BuildingMatDrawTab_SIZE - BuildingMatControlTab_SIZE - PenIndexTab_SIZE - PenControlTab_SIZE;

	// initialize the listbox
	buildMatList.SetTabFieldCount (NTabFields);
	buildMatList.SetHeaderSynchronState (false);

	buildMatList.SetHeaderItemSize (BuildingMatDrawTab,		0);
	buildMatList.SetHeaderItemSize (BuildingMatNameTab,		buildMatList.GetItemWidth () - penControl.GetWidth () * 2);
	buildMatList.SetHeaderItemSize (BuildingMatControlTab,	0);
	buildMatList.SetHeaderItemSize (PenIndexTab,			penControl.GetWidth () * 2);
	buildMatList.SetHeaderItemSize (PenControlTab,			0);

	short pos = 0;
	buildMatList.SetTabFieldProperties (BuildingMatDrawTab,		pos, pos + BuildingMatDrawTab_SIZE,		DG::ListBox::Center,	DG::ListBox::NoTruncate, false);
	pos += BuildingMatDrawTab_SIZE;
	buildMatList.SetTabFieldProperties (BuildingMatNameTab,		pos, pos + BuildingMatNameTab_SIZE,		DG::ListBox::Left,		DG::ListBox::NoTruncate, false);
	pos += BuildingMatNameTab_SIZE;
	buildMatList.SetTabFieldProperties (BuildingMatControlTab,	pos, pos + BuildingMatControlTab_SIZE,	DG::ListBox::Center,	DG::ListBox::EndTruncate, true);
	pos += BuildingMatControlTab_SIZE;
	buildMatList.SetTabFieldProperties (PenIndexTab,			pos, pos + PenIndexTab_SIZE,			DG::ListBox::Left,		DG::ListBox::NoTruncate, false);
	pos += PenIndexTab_SIZE;
	buildMatList.SetTabFieldProperties (PenControlTab,			pos, width,								DG::ListBox::Center,	DG::ListBox::EndTruncate, false);

	// set texts:
	buildMatList.SetHeaderItemText (BuildingMatNameTab,	"Building Material");
	buildMatList.SetHeaderItemText (PenIndexTab,		"cutFillPen");

	// to catch ListBoxTabFieldUpdate event:
	buildMatList.SetTabFieldOwnerDrawFlag (PenControlTab,		true);
	buildMatList.SetTabFieldOwnerDrawFlag (BuildingMatDrawTab,	true);

	short nItem = 0;
	const short maxBuildingMat = 50;
	ACAPI_Attribute_EnumerateAttributesByType (API_BuildingMaterialID, [&] (API_Attribute& attribute) {
		if (maxBuildingMat <= nItem)	// add only the first 50 building materials to the listbox
			return;

		buildMatList.InsertItem (nItem);	//conversion is not problem, because max value is 50.

		buildMatIndexList.Push (attribute.header.index);
		penIndexList.Push (attribute.buildingMaterial.cutFillPen);

		buildMatList.SetTabItemText (nItem, BuildingMatNameTab, attribute.header.name);
		buildMatList.SetTabItemText (nItem, PenIndexTab, GS::UniString::Printf ("%d", attribute.buildingMaterial.cutFillPen));
		nItem++;
	});

	buildMatList.SetTabItemColor (1, 2, Gfx::Color::Blue);
	Gfx::Color color;
	buildMatList.GetTabItemColor (1, 2, &color);
	buildMatList.SetTabItemBackgroundColor (2, 2, color);

	buildMatList.SetTabItemBackgroundColor (3, 2, Gfx::Color::Red);
	buildMatList.GetTabItemBackgroundColor (3, 2, &color);
	buildMatList.SetTabItemColor (4, 2, color);
}


BuildingMaterialDialog::~BuildingMaterialDialog ()
{
}


//-------------------------- Class BuildingMaterialObserver -----------------------

BuildingMaterialObserver::BuildingMaterialObserver (BuildingMaterialDialog* testDialog):
	dialog 	 (testDialog)
{
	dialog->Attach (*this);
	AttachToAllItems (*dialog);
}


BuildingMaterialObserver::~BuildingMaterialObserver ()
{
	dialog->Detach (*this);
	DetachFromAllItems (*dialog);
}


void BuildingMaterialObserver::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
	dialog->SetClientSize (dialog->GetOriginalClientWidth (), dialog->GetOriginalClientHeight ());
}


void BuildingMaterialObserver::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &dialog->okButton) {
		dialog->PostCloseRequest (DG::ModalDialog::Accept);
	}
}


void BuildingMaterialObserver::UserControlChanged (const DG::UserControlChangeEvent& ev)
{
	short listItemIndex = dialog->buildMatList.GetSelectedItem ();

	if (ev.GetSource () == &dialog->buildingMatControl) {
		Int32 fIndex = dialog->buildingMatControl.GetValue ();

		API_Attribute attr;
		Attribute_Get (&attr, API_BuildingMaterialID, ACAPI_CreateAttributeIndex (fIndex));

		dialog->buildMatIndexList[listItemIndex - 1] = attr.header.index;
		dialog->penIndexList[listItemIndex - 1] = attr.buildingMaterial.cutFillPen;

		dialog->buildMatList.SetTabItemText (listItemIndex, dialog->BuildingMatNameTab, attr.header.name);
		// change pen control
		dialog->penControl.SetValue (dialog->penIndexList[listItemIndex - 1]);
	}
	else if (ev.GetSource () == &dialog->penControl) {
		Int32 fIndex = dialog->penControl.GetValue ();
		dialog->penIndexList[listItemIndex - 1] = (short) fIndex;

		API_Attribute attr;
		Attribute_Get (&attr, API_BuildingMaterialID, dialog->buildMatIndexList[listItemIndex - 1]);
		attr.buildingMaterial.cutFillPen = (short) fIndex;
		ACAPI_Attribute_Modify (&attr, nullptr);
	}

	dialog->buildMatList.SetTabItemText (listItemIndex, dialog->PenIndexTab, GS::UniString::Printf ("%d", dialog->penIndexList[listItemIndex - 1]));

	dialog->buildMatList.RedrawTabItem (listItemIndex, BuildingMaterialDialog::BuildingMatDrawTab);
	dialog->buildMatList.RedrawTabItem (listItemIndex, BuildingMaterialDialog::PenControlTab);
}


void BuildingMaterialObserver::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev)
{
	if (ev.GetSource () == &dialog->buildMatList) {
		short listItemIndex = dialog->buildMatList.GetSelectedItem ();

		if (listItemIndex <= dialog->buildMatList.GetItemCount () && listItemIndex > 0) {
			// change building material control
			dialog->buildingMatControl.SetValue (dialog->buildMatIndexList[listItemIndex - 1].ToInt32_Deprecated ());
			// change pen control
			dialog->penControl.SetValue (dialog->penIndexList[listItemIndex - 1]);
			// show controls
			dialog->buildMatList.SetOnTabItem (BuildingMaterialDialog::BuildingMatControlTab,	dialog->buildingMatControl);
			dialog->buildMatList.SetOnTabItem (BuildingMaterialDialog::PenControlTab,			dialog->penControl);
		}
	}
}


void BuildingMaterialObserver::UpdateBuildingMatDrawTab (const DG::ListBoxTabItemUpdateEvent& ev)
{
	short item = ev.GetListItem ();
	if (item < 1)
		return;

	NewDisplay::ListBoxUpdateEventContext context (ev);

	short width   = ev.GetWidth ();
	short height  = ev.GetHeight ();

	DrawBuildingMaterial (context, DG::Rect (1, 1, width - 1, height - 1), dialog->buildMatIndexList[item - 1]);
}


void BuildingMaterialObserver::PanelResized (const DG::PanelResizeEvent& ev)
{
	short vGrow = ev.GetVerticalChange ();
	if (vGrow != 0) {
		dialog->BeginMoveResizeItems ();

		dialog->okButton.Move			(0, vGrow);
		dialog->penControl.Move			(0, vGrow);
		dialog->buildingMatControl.Move	(0, vGrow);
		dialog->buildMatList.Resize		(0, vGrow);

		dialog->EndMoveResizeItems ();
	}
}


void BuildingMaterialObserver::UpdatePenControlTab (const DG::ListBoxTabItemUpdateEvent& ev)
{
	short item = ev.GetListItem ();
	if (item < 1)
		return;

	NewDisplay::ListBoxUpdateEventContext context (ev);

	short width   = ev.GetWidth ();
	short height  = ev.GetHeight ();

	DrawPen (context, DG::Rect (2, 2, width - 2, height - 2), dialog->penIndexList[item - 1]);
}


void BuildingMaterialObserver::ListBoxTabFieldUpdate (const DG::ListBoxTabItemUpdateEvent& ev)
{
	short item = ev.GetListItem ();
	if (item < 1)
		return;

	if (ev.GetSource () == &dialog->buildMatList) {
		switch (ev.GetTabFieldIndex ()) {
			case BuildingMaterialDialog::BuildingMatDrawTab:
				UpdateBuildingMatDrawTab (ev);
				break;
			case BuildingMaterialDialog::PenControlTab:
				UpdatePenControlTab (ev);
				break;
			default:
				break;
		}
	}
}