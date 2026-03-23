// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn SelectLTypeIndDialog class for
//					managing the line type index dialog
// *****************************************************************************

#include	"APIEnvir.h"
#include	"ACAPinc.h"
#include	"SelectLTypeIndDialog.h"
#include	"LineTypeIndexes.h"

SelectLTypeIndDialog::SelectLTypeIndDialog () :
	DG::ModalDialog (ACAPI_GetOwnResModule (), 32500, ACAPI_GetOwnResModule ()),
	okButton (GetReference (), OKButtonID),
	cancelButton (GetReference (), CancelButtonID),
	basicLine (GetReference (), BasicLeftTextID),
	hiddenLine (GetReference (), HiddenLeftTextID),
	basicLineTypePickerPushCheck (GetReference (), BasicLineTypePickerID),
	hiddenLineTypePickerPushCheck (GetReference (), HiddenLineTypePickerID)
{
	InitUserControl (basicLineTypePicker, BasicLineTypePickerID);
	InitUserControl (hiddenLineTypePicker, HiddenLineTypePickerID);

	basicLineTypePicker->SetSelectedAttributeIndex (LineTypeIndexes::GetInstance ().GetBasicLineType ());
	hiddenLineTypePicker->SetSelectedAttributeIndex (LineTypeIndexes::GetInstance ().GetHiddenLineType ());

	AttachToAllItems (*this);
	Attach (*this);
}


void SelectLTypeIndDialog::InitUserControl (GS::Owner<API_AttributePicker>& attributePicker, short controlId)
{
	API_AttributePickerParams app;
	app.dialogID = 32500;
	app.pushCheckAppearance = API_AttributePickerParams::PushCheckAppearance::ArrowIconAndText;
	app.type = APIUserControlType_SymbolLine;
	app.itemID = controlId;
	ACAPI_Dialog_CreateAttributePicker (app, attributePicker);
}


void SelectLTypeIndDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &cancelButton)
		PostCloseRequest (Cancel);
	else if (ev.GetSource () == &okButton)
		PostCloseRequest (Accept);
}


void SelectLTypeIndDialog::CheckItemChanged (const DG::CheckItemChangeEvent& ev)
{
	if (ev.GetSource () == &basicLineTypePickerPushCheck)
		basicLineTypePicker->Invoke ();
	else if (ev.GetSource () == &hiddenLineTypePickerPushCheck)
		hiddenLineTypePicker->Invoke ();
}


void SelectLTypeIndDialog::PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* /*accepted*/)
{
	if (ev.IsAccepted ()) {
		LineTypeIndexes::SetLineTypes (basicLineTypePicker->GetSelectedAttributeIndex (), hiddenLineTypePicker->GetSelectedAttributeIndex ());
	}
}
