// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#define	_ADDRENAMEVIEWPOINTITEMDIALOG_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"ACAPinc.h"
#include	"Array.hpp"
#include	"DGModule.hpp"
#include	"AddRenameViewPointItemDialog.hpp"


// --- AddRenameViewPointItemDialog

const short AddRenameViewPointItemDialog::NavigatorAPI_NewItemDialogId = 32602;
const short AddRenameViewPointItemDialog::NavigatorAPI_RenameItemDialogId = 32603;

AddRenameViewPointItemDialog::AddRenameViewPointItemDialog (GSResModule dialResModule, DialogType dialogType, GS::UniString oldID/* = GS::UniString ()*/, GS::UniString oldName/* = GS::UniString ()*/):
	DG::ModalDialog	(dialResModule, (dialogType == NewItemDialogType) ? NavigatorAPI_NewItemDialogId : NavigatorAPI_RenameItemDialogId, InvalidResModule),
	okButton		(GetReference (), OkButtonId),
	cancelButton	(GetReference (), CancelButtonId),
	idTextEdit		(GetReference (), IDTextEditId),
	nameTextEdit	(GetReference (), NameTextEditId)
{
	Attach (*this);
	okButton.Attach (*this);
	cancelButton.Attach (*this);

	if (dialogType == RenameItemDialogType) {
		idTextEdit.SetText (oldID);
		nameTextEdit.SetText (oldName);
	}
}


AddRenameViewPointItemDialog::~AddRenameViewPointItemDialog ()
{
}


GS::UniString	AddRenameViewPointItemDialog::GetViewPointName (void) const
{
	return nameTextEdit.GetText ();
}


GS::UniString	AddRenameViewPointItemDialog::GetViewPointID (void) const
{
	return idTextEdit.GetText ();
}


void	AddRenameViewPointItemDialog::PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted)
{
	if (nameTextEdit.GetText ().IsEmpty () && ev.IsAccepted ()) {
		*accepted = false;
	}
}


void	AddRenameViewPointItemDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	}
}