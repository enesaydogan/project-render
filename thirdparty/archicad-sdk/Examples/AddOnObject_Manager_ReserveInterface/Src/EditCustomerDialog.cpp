// *****************************************************************************
// Source code for the AddOnObject Manager Add-On
// *****************************************************************************

#define	_EDITCUSTOMERDIALOG_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"DGModule.hpp"
#include	"EditCustomerDialog.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h

// ---------------------------------- Types ------------------------------------
static const short EditCustomerDialogResourceId					= 32501;
static const short EditCustomerDialogTeamworkTabPageResourceId  = 32502;
static const short EditCustomerDialogTeamworkTextResourceId		= 32503;


// ---------------------------------- Prototypes -------------------------------


// =============================================================================
//
//
//
// =============================================================================

static bool	IsTeamworkProject (short* currentUserId)
{
	API_ProjectInfo	pi = {};

	ACAPI_ProjectOperation_Project (&pi);

	if (currentUserId != nullptr)
		*currentUserId = pi.teamwork ? pi.userId : 0;

	return pi.teamwork != 0;
}


EditCustomerDialog::EditCustomerDialog (GSResModule dialResModule, short resId, GSResModule dialIconResModule, IAddOnData* addonData):
	DG::ModalDialog (dialResModule, resId, dialIconResModule),
	okButton (GetReference (), OkButtonId),
	cancelButton (GetReference (), CancelButtonId),
	dataText (GetReference (), DataTextId),
	modifyButton (GetReference (), ModifyButtonId),
	teamworkTab (GetReference (), TeamworkTabId),
	separator (GetReference (), SeparatorId),
	addonData (addonData),
	twCloseReason (CloseReason_Nothing),
	isTWTabInitialized (false)
{
	SetDataText ();

	short currentUserId = 0;
	bool isTeamwork = IsTeamworkProject (&currentUserId);

	InitTWTab (isTeamwork);

	AttachToAllItems (*this);
	Attach (*this);

	short ownerId = 0;
	bool enableModifyButton = !isTeamwork || (ACAPI_AddOnObject_GetTeamworkOwnerId (GSGuid2APIGuid (addonData->guid), &ownerId) == NoError && ownerId == currentUserId);
	modifyButton.SetStatus (enableModifyButton);
}


EditCustomerDialog::~EditCustomerDialog ()
{
	DeleteTWTab ();
}


void	EditCustomerDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	} else if (ev.GetSource () == &modifyButton) {
		addonData->GenerateRandomContent ();
		addonData->modified = true;
		SetDataText ();
	}
}


void EditCustomerDialog::TeamWorkPanelParentUIRefreshNeeded (API_ReservationInterfaceStatus reservationStatus)
{
	modifyButton.SetStatus (reservationStatus == APIReservationInterfaceStatus_Editable);
}


void EditCustomerDialog::TeamWorkPanelParentDataSaveNeeded (API_ReservationInterfaceStatus status)
{
	if (status == APIReservationInterfaceStatus_Editable) {
		twCloseReason = CloseReason_Release;
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (status == APIReservationInterfaceStatus_Reserved) {
		twCloseReason = CloseReason_Grant;
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (status == APIReservationInterfaceStatus_Free){
		twCloseReason = CloseReason_Reserve;
		PostCloseRequest (DG::ModalDialog::Accept);
	} else {
		twCloseReason = CloseReason_Nothing;
		PostCloseRequest (DG::ModalDialog::Accept);
	}
}


CloseReason	EditCustomerDialog::GetTWCloseReason (void) const
{
	return twCloseReason;
}


static GSErrCode TeamWorkPanelParentUIRefreshNeeded (short dialogId, API_ReservationInterfaceStatus reservationStatus)
{
	DG::Dialog* dialog = DG::GetDialogFromId (dialogId);
	if (DBERROR (dialog == nullptr))
		return Error;

	EditCustomerDialog* editCustomerDialog = dynamic_cast<EditCustomerDialog*> (dialog);
	if (DBERROR (editCustomerDialog == nullptr))
		return Error;

	editCustomerDialog->TeamWorkPanelParentUIRefreshNeeded (reservationStatus);

	return NoError;
}


static GSErrCode TeamWorkPanelParentDataSaveNeeded  (short dialogId, API_ReservationInterfaceStatus reservationStatus)
{
	DG::Dialog* dialog = DG::GetDialogFromId (dialogId);
	if (DBERROR (dialog == nullptr))
		return Error;

	EditCustomerDialog* editCustomerDialog = dynamic_cast<EditCustomerDialog*> (dialog);
	if (DBERROR (editCustomerDialog == nullptr))
		return Error;

	editCustomerDialog->TeamWorkPanelParentDataSaveNeeded (reservationStatus);

	return NoError;
}


void	EditCustomerDialog::InitTWTab (bool isTeamwork)
{
	if (isTWTabInitialized)
		DeleteTWTab ();


	GS::Guid objectId = addonData->guid;

	if (isTeamwork && !objectId.IsNull ()) {
		ACAPI_RegisterTeamworkReserveInterface (GSGuid2APIGuid (objectId),
												GetId (),
												TeamworkTabId,
												EditCustomerDialogTeamworkTabPageResourceId,
												ACAPI_GetOwnResModule (),
												ACAPI_GetOwnResModule (),
												::TeamWorkPanelParentUIRefreshNeeded,
												::TeamWorkPanelParentDataSaveNeeded,
												RSGetIndString (EditCustomerDialogTeamworkTextResourceId, 1, ACAPI_GetOwnResModule ()));
		isTWTabInitialized = true;
		teamworkTab.Show ();
		okButton.Hide ();
		cancelButton.Hide ();
	} else {
		teamworkTab.Hide ();
	}
}


void	EditCustomerDialog::DeleteTWTab (void)
{
	if (!isTWTabInitialized)
		return;

	GS::Guid objectId = addonData->guid;
	ACAPI_UnregisterTeamworkReserveInterface (GSGuid2APIGuid (objectId), GetId ());
	isTWTabInitialized = false;
}


void	EditCustomerDialog::SetDataText (void)
{
	dataText.SetText (addonData->ToString ());
}


bool	ModifyAddOnData (IAddOnData& addonData, CloseReason& closeReason)
{
	EditCustomerDialog editCustomerDialog (ACAPI_GetOwnResModule (), EditCustomerDialogResourceId, ACAPI_GetOwnResModule (), &addonData);
	bool result = editCustomerDialog.Invoke ();
	closeReason = editCustomerDialog.GetTWCloseReason ();
	return result;
}


bool IsModifyButtonReservationDependent (void)
{
	return false;
}
