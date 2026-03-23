// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include "APIEnvir.h"
#define	_NAVIGATOR_DIALOG_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"NavigatorDialog.hpp"

#include	"ACAPinc.h"
#include	"Array.hpp"
#include	"DGModule.hpp"
#include	"AddRenameViewPointItemDialog.hpp"
#include	"NavigatorUtility.hpp"


// ---------------------------------- Types ------------------------------------

typedef GS::HashTable<short, GS::UniString> UserIdToUserNameTable;


// ---------------------------------- Static -----------------------------------

static bool	IsTeamworkProject (short* currentUserId)
{
	API_ProjectInfo	pi = {};

	GSErrCode err = ACAPI_ProjectOperation_Project (&pi);

	if (err == NoError && currentUserId != nullptr)
		*currentUserId = pi.teamwork ? pi.userId : 0;

	return pi.teamwork != 0;
//	return false;
}


static GSErrCode	GetUserIdToUserNameTable (UserIdToUserNameTable* userIdToUserNameTable)
{
	userIdToUserNameTable->Clear ();

	API_SharingInfo	sharingInfo = {};
	GSErrCode err = ACAPI_Teamwork_ProjectSharing (&sharingInfo);
	if (err == NoError && sharingInfo.users != nullptr) {
		for (Int32 i = 0; i < sharingInfo.nUsers; i++)
			userIdToUserNameTable->Add ((*sharingInfo.users)[i].userId, GS::UniString ((*sharingInfo.users)[i].loginName));
	}
	if (sharingInfo.users != nullptr)
		BMhKill (reinterpret_cast<GSHandle*>(&sharingInfo.users));

	return err;
}


static GSErrCode TeamWorkPanelParentUIRefreshNeeded (short dialogId, API_ReservationInterfaceStatus reservationStatus)
{
	DG::Dialog* dialog = DG::GetDialogFromId (dialogId);
	if (DBERROR (dialog == nullptr))
		return Error;

	NavigatorDialog* navigatorDialog = dynamic_cast<NavigatorDialog*> (dialog);
	if (DBERROR (navigatorDialog == nullptr))
		return Error;

	navigatorDialog->TeamWorkPanelParentUIRefreshNeeded (reservationStatus);

	return NoError;
}


static GSErrCode TeamWorkPanelParentDataSaveNeeded  (short dialogId, API_ReservationInterfaceStatus reservationStatus)
{
	DG::Dialog* dialog = DG::GetDialogFromId (dialogId);
	if (DBERROR (dialog == nullptr))
		return Error;

	NavigatorDialog* navigatorDialog = dynamic_cast<NavigatorDialog*> (dialog);
	if (DBERROR (navigatorDialog == nullptr))
		return Error;

	navigatorDialog->TeamWorkPanelParentDataSaveNeeded (reservationStatus);

	return NoError;
}


// --- NavigatorAPI_TestDialog -------------------------------------------------

const short NavigatorDialog::NavigatorDialogId			= 32600;
const short NavigatorDialog::NavigatorTeamworkTabPageId = 32601;
const short NavigatorDialog::NavigatorTeamworkTextId	= 32604;

NavigatorDialog::NavigatorDialog (GSResModule dialResModule, GSResModule dialIconResModule, const GS::Array<AddOnNavigatorViewPointData>& _navigatorViewPointList, API_Guid selectedItemGuid):
	DG::ModalDialog				(dialResModule, NavigatorDialogId, dialIconResModule),
	dialogResModule				(dialResModule),
	okButton					(GetReference (), OkButtonId),
	cancelButton				(GetReference (), CancelButtonId),
	navigatorItemOnTabIdEdit	(GetReference (), NavigatorItemOnTabIdEditId),
	navigatorItemOnTabNameEdit	(GetReference (), NavigatorItemOnTabNameEditId),
	navigatorItemList			(GetReference (), NavigatorItemListId),
	newButton					(GetReference (), NewButtonId),
	deleteButton				(GetReference (), DeleteButtonId),
	elementTypeText				(GetReference (), ElementTypeTextId),
	elementTypePopup			(GetReference (), ElementTypePopupId),
	numberOfElementsText		(GetReference (), NumberOfElementsTextId),
	numberOfElementsEdit		(GetReference (), NumberOfElementsEditId),
	separator					(GetReference (), SeparatorId),
	teamworkTab					(GetReference (), TeamworkTabId),

	navigatorViewPointList		(_navigatorViewPointList),
	origNavigatorViewPointList	(_navigatorViewPointList),
	prevSelectedVPGuid			(APINULLGuid),
	isTWTabInitialized			(false),
	twCloseReason				(TWCloseReason_NoTWOperation)
{
	AttachToAllItems (*this);
	Attach (*this);

	InitListAndOnTabItems ();
	SelectGivenViewPointGuid (selectedItemGuid);
	if (navigatorItemList.GetItemCount () > 0 && GetSelectedViewPointGuid () == APINULLGuid) {
		navigatorItemList.SelectItem (1);
		ShowOnTabListItems ();
	}

	RefreshTWTab (APINULLGuid, GetSelectedViewPointGuid ());	// RefreshTWTab calls InitTWTab AND TeamWorkPanelParentUIRefreshNeeded as well
	UpdatePopups ();
	prevSelectedVPGuid = GetSelectedViewPointGuid ();
}


NavigatorDialog::~NavigatorDialog ()
{
	DeleteTWTab (GetSelectedViewPointGuid ());
}


// ... NavigatorAPI_TestDialog - Public methods ........................................................................

void NavigatorDialog::TeamWorkPanelParentUIRefreshNeeded (API_ReservationInterfaceStatus reservationStatus)
{
	short ownerId = 0;
	short currentUserId = 0;
	bool isTeamwork = IsTeamworkProject (&currentUserId);
	bool enableTWInterface = isTeamwork && (ACAPI_AddOnObject_GetTeamworkOwnerId (GetSelectedViewPointGuid (), &ownerId) == NoError && ownerId == currentUserId);

	bool isModifyingEnabled = !isTeamwork || (enableTWInterface && reservationStatus == APIReservationInterfaceStatus_Editable);
	deleteButton.SetStatus (isModifyingEnabled);
	navigatorItemOnTabIdEdit.SetStatus (isModifyingEnabled);
	navigatorItemOnTabNameEdit.SetStatus (isModifyingEnabled);
	elementTypePopup.SetStatus (isModifyingEnabled);
	numberOfElementsEdit.SetStatus (isModifyingEnabled);
}


void	NavigatorDialog::TeamWorkPanelParentDataSaveNeeded (API_ReservationInterfaceStatus status)
{
	if (status == APIReservationInterfaceStatus_Editable) {
		twCloseReason = TWCloseReason_Release;
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (status == APIReservationInterfaceStatus_Reserved) {
		twCloseReason = TWCloseReason_Grant;
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (status == APIReservationInterfaceStatus_Free){
		twCloseReason = TWCloseReason_Reserve;
		PostCloseRequest (DG::ModalDialog::Accept);
	} else {
		twCloseReason = TWCloseReason_NoTWOperation;
		PostCloseRequest (DG::ModalDialog::Accept);
	}
}


NavigatorDialog::TWCloseReason	NavigatorDialog::GetTWCloseReason (void) const
{
	return twCloseReason;
}


GSErrCode	NavigatorDialog::HandlePanelAccepted (bool* invokeDialog, NavigatorDialog::TWCloseReason twCloseReason)
{
	if (DBERROR (invokeDialog == nullptr))
		return Error;

	GSErrCode err = NoError;

	// delete
	for (auto it = origNavigatorViewPointList.Enumerate (); it != nullptr; ++it) {
		bool found = false;
		API_Guid guid = it->GetGuid ();
		for (auto it2 = GetNavigatorViewPoints ().Enumerate (); it2 != nullptr; ++it2) {
			if (it2->GetGuid () == guid) {
				found = true;
				break;
			}
		}
		if (!found) {
			API_NavigatorAddOnViewPointData data = it->ConvertToAPINavigatorAddOnViewPointData ();
			if (data.itemType != API_NavigatorAddOnViewPointRootID) {
				err = ACAPI_Navigator_DeleteNavigatorVPItem (&guid);
			}
		}
	}

	short currentUserId = 0;
	bool isTeamwork = IsTeamworkProject (&currentUserId);
	// new, modify
	for (auto it = GetNavigatorViewPoints ().Enumerate (); it != nullptr; ++it) {
		AddOnNavigatorViewPointData addOnData = *it;
		if (addOnData.GetGuid () == APINULLGuid) {
			API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
			data.parentGuid = RootGuid;
			err = ACAPI_Navigator_CreateNavigatorVPItem (&data);
		} else if (!isTeamwork || !clientCreatedViewPointGuids.Contains (addOnData.GetGuid ())) {
			if (origNavigatorViewPointList.FindFirst (addOnData) != MaxUIndex) {
				API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
				err = ACAPI_Navigator_ChangeNavigatorVPItem (&data);
			}
		}
	}

	// the dialog was closed with the Reserver/Release/Grant button
	if (twCloseReason != NavigatorDialog::TWCloseReason_NoTWOperation) {
		GS::Array<API_Guid> twOperationObjectGuid;
		if (GetSelectedViewPointGuid () != APINULLGuid)
			twOperationObjectGuid.Push (GetSelectedViewPointGuid ());

		if (GetTWCloseReason () == NavigatorDialog::TWCloseReason_Reserve) {
			ACAPI_AddOnObject_ReserveObjects (twOperationObjectGuid, nullptr/*&conflicts*/);
			*invokeDialog = true;
		} else if (twCloseReason == NavigatorDialog::TWCloseReason_Release) {
			ACAPI_AddOnObject_ReleaseObjects (twOperationObjectGuid);
			*invokeDialog = true;
		} else if (twCloseReason == NavigatorDialog::TWCloseReason_Request) {
			ACAPI_AddOnObject_RequestObjects (twOperationObjectGuid, GS::UniString ());
			*invokeDialog = true;
		} else if (twCloseReason == NavigatorDialog::TWCloseReason_Grant) {
			short toUserId = GetGrantUserId ();
			if (toUserId == -1) {
				UserIdToUserNameTable userIdToNameTable;
				GetUserIdToUserNameTable (&userIdToNameTable);
				toUserId = -1;
				for (auto it = userIdToNameTable.EnumeratePairs (); it != nullptr && toUserId == -1; ++it) {
					if (it->key != currentUserId)
						toUserId = it->key;
				}
			}

			if (toUserId != -1) {
				ACAPI_AddOnObject_GrantObjects (twOperationObjectGuid, toUserId);
			}

			*invokeDialog = true;
		}
	}

	return err;
}


void	NavigatorDialog::HandleClientCreatedViewPointsOnCancel (void)
{
	// the client created viewpoints should be deleted from the Navigator
	for (auto it = origNavigatorViewPointList.Enumerate (); it != nullptr; ++it) {
		API_Guid guid = it->GetGuid ();
		if (clientCreatedViewPointGuids.Contains (guid)) {
			API_NavigatorAddOnViewPointData data = it->ConvertToAPINavigatorAddOnViewPointData ();
			if (data.itemType != API_NavigatorAddOnViewPointRootID) {
				ACAPI_Navigator_DeleteNavigatorVPItem (&guid);
			}
		}
	}
}


API_Guid	NavigatorDialog::GetSelectedViewPointGuid (void)
{
	short ind = navigatorItemList.GetSelectedItem () - 1;
	if (ind >= 0) {
		return navigatorViewPointList[ind].GetGuid ();
	} else {
		return APINULLGuid;
	}
}


// ... NavigatorAPI_TestDialog - Dialog and Item notifications .........................................................

void	NavigatorDialog::PanelResized (const DG::PanelResizeEvent& ev)
{
	short dh = ev.GetHorizontalChange ();
	short dv = ev.GetVerticalChange ();

	BeginMoveResizeItems ();
	okButton.Move (dh, dv);
	cancelButton.Move (dh, dv);
	navigatorItemOnTabNameEdit.Resize (dh, 0);
	navigatorItemList.Resize (dh, dv);
	navigatorItemList.SetTabFieldBeginEndPosition (NameTabItem, navigatorItemList.GetTabFieldBeginPosition (NameTabItem), navigatorItemList.GetItemWidth ());
	navigatorItemList.SetHeaderItemSize (NameTabItem, (navigatorItemList.GetWidth () - navigatorItemList.GetHeaderItemSize (IdTabItem)));
	newButton.Move (dh, 0);
	deleteButton.Move (dh, 0);
	elementTypeText.Move (0, dv);
	elementTypePopup.MoveAndResize (0, dv, dh, 0);
	numberOfElementsText.Move (0, dv);
	numberOfElementsEdit.MoveAndResize (0, dv, dh, 0);
	separator.MoveAndResize (0, dv, dh, 0);
	teamworkTab.MoveAndResize (0, dv, dh, 0);
	EndMoveResizeItems ();
}


void	NavigatorDialog::PanelResizeExited (const DG::PanelResizeEvent& /*ev*/)
{
	navigatorItemList.SetTabFieldBeginEndPosition (NameTabItem, navigatorItemList.GetTabFieldBeginPosition (NameTabItem), navigatorItemList.GetItemWidth ());
	navigatorItemList.SetHeaderItemSize (NameTabItem, (navigatorItemList.GetWidth () - navigatorItemList.GetHeaderItemSize (IdTabItem)));
}


void	NavigatorDialog::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev)
{
	if (ev.GetSource () == &navigatorItemList) {
		RefreshTWTab (prevSelectedVPGuid, GetSelectedViewPointGuid ());
		prevSelectedVPGuid = GetSelectedViewPointGuid ();
		ShowOnTabListItems ();
	}
}


void	NavigatorDialog::TextEditChanged (const DG::TextEditChangeEvent& ev)
{
	if (ev.GetSource () == &navigatorItemOnTabIdEdit) {
		UInt32 selInd = navigatorItemList.GetSelectedItem () - 1;
		if (selInd < navigatorViewPointList.GetSize ()) {
			short currentUserId = 0;
			if (IsTeamworkProject (&currentUserId) && clientCreatedViewPointGuids.Contains (GetSelectedViewPointGuid ())) {
				AddOnNavigatorViewPointData addOnData = navigatorViewPointList[selInd];
				if (origNavigatorViewPointList.FindFirst (addOnData) != MaxUIndex) {
					API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
					data.displayId = navigatorItemOnTabIdEdit.GetText ();
					/*err =*/ ACAPI_Navigator_ChangeNavigatorVPItem (&data);
				}
			}
			navigatorViewPointList[selInd].SetId (navigatorItemOnTabIdEdit.GetText ());
			FillList ();
		}
	} else if (ev.GetSource () == &navigatorItemOnTabNameEdit) {
		UInt32 selInd = navigatorItemList.GetSelectedItem () - 1;
		if (selInd < navigatorViewPointList.GetSize ()) {
			short currentUserId = 0;
			if (IsTeamworkProject (&currentUserId) && clientCreatedViewPointGuids.Contains (GetSelectedViewPointGuid ())) {
				AddOnNavigatorViewPointData addOnData = navigatorViewPointList[selInd];
				if (origNavigatorViewPointList.FindFirst (addOnData) != MaxUIndex) {
					API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
					data.displayName = navigatorItemOnTabNameEdit.GetText ();
					/*err =*/ ACAPI_Navigator_ChangeNavigatorVPItem (&data);
				}
			}
			navigatorViewPointList[selInd].SetName (navigatorItemOnTabNameEdit.GetText ());
			FillList ();
		}
	}
}


void	NavigatorDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	} else if (ev.GetSource () == &newButton) {
		AddRenameViewPointItemDialog newItemDialog (dialogResModule, AddRenameViewPointItemDialog::NewItemDialogType);
		if (newItemDialog.Invoke ()) {
			AddOnNavigatorViewPointData newViewPontData (newItemDialog.GetViewPointID (), newItemDialog.GetViewPointName (), APINULLGuid, PopupItemToElemType (elementTypePopup.GetSelectedItem ()), numberOfElementsEdit.GetValue ());
			short currentUserId = 0;
			if (IsTeamworkProject (&currentUserId)) {
				API_NavigatorAddOnViewPointData newNavigatorData = newViewPontData.ConvertToAPINavigatorAddOnViewPointData ();
				newNavigatorData.parentGuid = RootGuid;
				if (ACAPI_Navigator_CreateNavigatorVPItem (&newNavigatorData) == NoError) {
					newViewPontData.ConvertFromAPINavigatorAddOnViewPointData (newNavigatorData);
					origNavigatorViewPointList.Push (newViewPontData);
					clientCreatedViewPointGuids.Push (newViewPontData.GetGuid ());
				}
			}
			navigatorViewPointList.Push (newViewPontData);
			FillList (newViewPontData.GetName ());
			RefreshTWTab (prevSelectedVPGuid, GetSelectedViewPointGuid ());
			prevSelectedVPGuid = GetSelectedViewPointGuid ();
		}
	} else if (ev.GetSource () == &deleteButton) {
		UInt32 itemToDelete = navigatorItemList.GetSelectedItem () - 1;
		if (itemToDelete < navigatorViewPointList.GetSize ()) {
			short currentUserId = 0;
			if (IsTeamworkProject (&currentUserId) && clientCreatedViewPointGuids.Contains (GetSelectedViewPointGuid ())) {
				AddOnNavigatorViewPointData addOnData = navigatorViewPointList[itemToDelete];
				if (origNavigatorViewPointList.FindFirst (addOnData) != MaxUIndex) {
					API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
					if (data.itemType != API_NavigatorAddOnViewPointRootID) {
						if (ACAPI_Navigator_DeleteNavigatorVPItem (&data.guid) == NoError) {
							origNavigatorViewPointList.DeleteFirst (addOnData);
						}
					}
				}
				clientCreatedViewPointGuids.DeleteFirst (GetSelectedViewPointGuid ());
			}
			navigatorViewPointList.Delete (itemToDelete);
			if (itemToDelete == (UInt32) navigatorItemList.GetItemCount () - 1)
				SelectGivenViewPointGuid (navigatorViewPointList[0].GetGuid ());
			FillList ();
			RefreshTWTab (prevSelectedVPGuid, GetSelectedViewPointGuid ());
			prevSelectedVPGuid = GetSelectedViewPointGuid ();
		}
	}
}


void	NavigatorDialog::PopUpChanged (const DG::PopUpChangeEvent& ev)
{
	if (ev.GetSource () == &elementTypePopup) {
		UInt32 selInd = navigatorItemList.GetSelectedItem () - 1;
		if (selInd < navigatorViewPointList.GetSize ()) {
			short elemInd = elementTypePopup.GetSelectedItem ();
			if (elemInd > 0) {
				short currentUserId = 0;
				if (IsTeamworkProject (&currentUserId) && clientCreatedViewPointGuids.Contains (GetSelectedViewPointGuid ())) {
					AddOnNavigatorViewPointData addOnData = navigatorViewPointList[selInd];
					if (origNavigatorViewPointList.FindFirst (addOnData) != MaxUIndex) {
						addOnData.SetElemType (PopupItemToElemType (elemInd));
						API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
						/*err =*/ ACAPI_Navigator_ChangeNavigatorVPItem (&data);
					}
				}
				navigatorViewPointList[selInd].SetElemType (PopupItemToElemType (elemInd));
			}
		}
	}
}


void NavigatorDialog::IntEditChanged (const DG::IntEditChangeEvent& ev)
{
	if (ev.GetSource () == &numberOfElementsEdit) {
		UInt32 selInd = navigatorItemList.GetSelectedItem () - 1;
		if (selInd < navigatorViewPointList.GetSize ()) {
			short currentUserId = 0;
			if (IsTeamworkProject (&currentUserId) && clientCreatedViewPointGuids.Contains (GetSelectedViewPointGuid ())) {
				AddOnNavigatorViewPointData addOnData = navigatorViewPointList[selInd];
				if (origNavigatorViewPointList.FindFirst (addOnData) != MaxUIndex) {
					addOnData.SetElemNum (numberOfElementsEdit.GetValue ());
					API_NavigatorAddOnViewPointData data = addOnData.ConvertToAPINavigatorAddOnViewPointData ();
					/*err =*/ ACAPI_Navigator_ChangeNavigatorVPItem (&data);
				}
			}
			navigatorViewPointList[selInd].SetElemNum (numberOfElementsEdit.GetValue ());
		}
	}
}


// ... NavigatorAPI_TestDialog - Private methods .......................................................................

void	NavigatorDialog::InitTWTab (API_Guid objectId)
{
	if (isTWTabInitialized)
		DeleteTWTab (objectId);

	short currentUserId = 0;
	if (IsTeamworkProject (&currentUserId) && (objectId != APINULLGuid)) {
 		ACAPI_RegisterTeamworkReserveInterface (objectId,
												GetId (),
												TeamworkTabId,
												NavigatorDialog::NavigatorTeamworkTabPageId,
												ACAPI_GetOwnResModule (),
												ACAPI_GetOwnResModule (),
												::TeamWorkPanelParentUIRefreshNeeded,
												::TeamWorkPanelParentDataSaveNeeded,
												RSGetIndString (NavigatorDialog::NavigatorTeamworkTextId, 1, ACAPI_GetOwnResModule ()));
		isTWTabInitialized = true;
		teamworkTab.Show ();
		okButton.Hide ();
		cancelButton.Hide ();
	} else {
		teamworkTab.Hide ();
		okButton.Show ();
		cancelButton.Show ();
	}
}


void	NavigatorDialog::DeleteTWTab (API_Guid objectId)
{
	if (!isTWTabInitialized)
		return;

	ACAPI_UnregisterTeamworkReserveInterface (objectId, GetId ());
	isTWTabInitialized = false;
}


void	NavigatorDialog::RefreshTWTab (API_Guid oldGuid, API_Guid newGuidd)
{
	if (oldGuid != APINULLGuid)
		DeleteTWTab (oldGuid);
	UpdatePopups ();
	InitTWTab (newGuidd);
 	ACAPI_RefreshTeamworkReserveInterface (newGuidd, GetId (), true);
}


short	NavigatorDialog::GetGrantUserId (void) const
{
	return grantUserId;
}


void	NavigatorDialog::InitListAndOnTabItems (void)
{
	navigatorItemOnTabIdEdit.Hide ();
	navigatorItemOnTabNameEdit.Hide ();

	// initialize the listbox headeritem
	navigatorItemList.SetHeaderSynchronState (false);
	navigatorItemList.SetHeaderItemCount (2);

	navigatorItemList.SetHeaderItemText (IdTabItem, ::RSGetIndString (32501, 1, dialogResModule));
	navigatorItemList.SetHeaderItemText (NameTabItem, ::RSGetIndString (32501, 2, dialogResModule));

	navigatorItemList.SetHeaderItemSizeableFlag (IdTabItem, true);
	navigatorItemList.SetHeaderItemSizeableFlag (NameTabItem, true);
	navigatorItemList.SetHeaderItemStyle (IdTabItem, DG::ListBox::DefaultJust, DG::ListBox::EndTruncate);
	navigatorItemList.SetHeaderItemStyle (NameTabItem, DG::ListBox::DefaultJust, DG::ListBox::EndTruncate);

//	navigatorItemList.SetHeaderItemMinSize (IdTabItem, navigatorItemList.GetItemWidth () / 2);
//	navigatorItemList.SetHeaderItemMinSize (NameTabItem, navigatorItemList.GetItemWidth () / 2);
	navigatorItemList.SetHeaderItemMinSize (IdTabItem, 12);
	navigatorItemList.SetHeaderItemMinSize (NameTabItem, 12);
	navigatorItemList.SetItemHeight (navigatorItemOnTabNameEdit.GetHeight () + 2);
	navigatorItemList.SetHeaderItemSize (IdTabItem, navigatorItemList.GetItemWidth () / 2);
	navigatorItemList.SetHeaderItemSize (NameTabItem, (navigatorItemList.GetItemWidth () - navigatorItemOnTabIdEdit.GetWidth () - 1));

	// initializing the listbox tabfields
	navigatorItemList.SetTabFieldCount (2);

	navigatorItemList.SetTabFieldJustification (IdTabItem, DG::ListBox::Left);
	navigatorItemList.SetTabFieldTruncation (IdTabItem, DG::ListBox::EndTruncate);
	navigatorItemList.SetTabFieldJustification (NameTabItem, DG::ListBox::Left);
	navigatorItemList.SetTabFieldTruncation (NameTabItem, DG::ListBox::EndTruncate);

	navigatorItemList.SetTabFieldBeginEndPosition (IdTabItem, 0, navigatorItemList.GetItemWidth () / 2);
	navigatorItemList.SetTabFieldBeginEndPosition (NameTabItem, navigatorItemList.GetItemWidth () / 2 + 1, navigatorItemList.GetItemWidth ());

	navigatorItemOnTabIdEdit.SetWidth (navigatorItemList.GetItemWidth () / 2 - 1);
	navigatorItemOnTabNameEdit.SetWidth (navigatorItemList.GetItemWidth () - navigatorItemOnTabIdEdit.GetWidth () - 1);

	FillList ();
}


void	NavigatorDialog::FillList (GS::UniString viewPointToSelect/* = GS::UniString ()*/)
{
	API_Guid selectedItemGuid = GetSelectedViewPointGuid ();

	short selectedIndex = -1;
	navigatorItemList.DisableDraw ();
	navigatorItemList.DeleteItem (DG::ListBox::AllItems);
	for (UInt32 i = 0; i < navigatorViewPointList.GetSize (); ++i) {
		navigatorItemList.AppendItem ();
		short itemIndex = navigatorItemList.GetItemCount ();
		navigatorItemList.SetTabItemText (itemIndex, IdTabItem, navigatorViewPointList[i].GetId ());
		navigatorItemList.SetTabItemText (itemIndex, NameTabItem, navigatorViewPointList[i].GetName ());
		navigatorItemList.SetItemValue (itemIndex, navigatorViewPointList[i].GetElemType ());
		if (!viewPointToSelect.IsEmpty () && navigatorViewPointList[i].GetName () == viewPointToSelect) {
			selectedIndex = itemIndex;
		}
	}

	if (!viewPointToSelect.IsEmpty () && selectedIndex > 0 && selectedIndex <= navigatorItemList.GetItemCount ()) {
		navigatorItemList.SelectItem (selectedIndex);
		ShowOnTabListItems ();
	} else if (selectedItemGuid != APINULLGuid) {
		SelectGivenViewPointGuid (selectedItemGuid);
	}

//	if (navigatorItemList.GetItemCount () > 0) {
//		navigatorItemList.SelectItem (1);
//	}

	navigatorItemList.EnableDraw ();
	navigatorItemList.Redraw ();

	UpdatePopups ();
}


short	NavigatorDialog::SelectGivenViewPointGuid (API_Guid selectedGuid)
{
	short listItemIndex = -1;
	if (selectedGuid == APINULLGuid)
		return listItemIndex;

	// selecting the given viewPoint by APIGuid
	for (UIndex i = 0; i < navigatorViewPointList.GetSize (); ++i) {
		if (navigatorViewPointList[i].GetGuid () == selectedGuid) {
			listItemIndex = (short) i + 1;
			break;
		}
	}
	if (listItemIndex > 0) {
		navigatorItemList.SelectItem (listItemIndex);
		ShowOnTabListItems ();
	}

	return listItemIndex;
}


bool	NavigatorDialog::ShowOnTabListItems (void)
{
	if (navigatorItemList.GetSelectedCount () > 0) {
		navigatorItemList.SetOnTabItem (IdTabItem, navigatorItemOnTabIdEdit);
		navigatorItemList.SetOnTabItem (NameTabItem, navigatorItemOnTabNameEdit);
		navigatorItemOnTabIdEdit.SetText (navigatorItemList.GetTabItemText (navigatorItemList.GetSelectedItem (), IdTabItem));
		navigatorItemOnTabNameEdit.SetText (navigatorItemList.GetTabItemText (navigatorItemList.GetSelectedItem (), NameTabItem));
		return true;
	}
	navigatorItemOnTabIdEdit.SetText (GS::UniString ());
	navigatorItemOnTabNameEdit.SetText (GS::UniString ());
	return false;
}


void NavigatorDialog::UpdatePopups (void)
{
	UInt32 ind = navigatorItemList.GetSelectedItem () - 1;
	if (ind < navigatorViewPointList.GetSize ()) {
		elementTypePopup.SelectItem (ElemTypeToPopupIndex (navigatorViewPointList[ind].GetElemType ()));
		numberOfElementsEdit.SetValue (navigatorViewPointList[ind].GetElemNum ());
	}
}


API_ElemTypeID	NavigatorDialog::PopupItemToElemType (short popupItem)
{
	switch (popupItem) {
		case 1:	return API_LineID;
		case 2: return API_ArcID;
		case 3: return API_PolyLineID;
		case 4: return API_HatchID;
	}

	return API_ZombieElemID;
}


short	NavigatorDialog::ElemTypeToPopupIndex (API_ElemTypeID elemType)
{
	switch (elemType) {
		case API_LineID:		return 1;
		case API_ArcID:			return 2;
		case API_PolyLineID:	return 3;
		case API_HatchID:		return 4;
	}

	return API_ZombieElemID;
}
