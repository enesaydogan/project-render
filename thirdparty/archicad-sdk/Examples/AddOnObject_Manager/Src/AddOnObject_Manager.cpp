// *****************************************************************************
// Source code for the AddOnObject Manager Add-On
// *****************************************************************************

#define	_ADDONOBJECT_MANAGER_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h

#include	"CustomerData.hpp"

#include	"DGModule.hpp"
#include	"StringConversion.hpp"
#include	"MemoryOChannel32.hpp"
#include	"MemoryIChannel32.hpp"
#include	"FixArray.hpp"


// ---------------------------------- Types ------------------------------------

typedef GS::HashTable<short, GS::UniString> UserIdToUserNameTable;
// ---------------------------------- Variables --------------------------------
static const short CustomerManagerDialogResourceId				= 32500;
static const short MenuStringResourceId							= 32500;

static const short ClientOnlyObjectMenuStringResourceId			= 32502;

static GS::UniString AddOnObjectName ("CustomerInfo");
static GS::UniString AddOnClientOnlyObject ("ClientOnlyObject");
static GS::FixArray<GS::UniString, 5> UniqueAddOnObjectNames ({ "Settings1", "Settings2", "Settings3", "Settings4", "Settings5" });

// ---------------------------------- Prototypes -------------------------------
extern bool ModifyAddOnData						(IAddOnData& addOnData, CloseReason& closeReason);
extern bool IsModifyButtonReservationDependent	(void);

// =============================================================================
//
//
//
// =============================================================================

static bool	IsTeamworkProject (short* currentUserId)
{
	API_ProjectInfo	pi = {};

	ACAPI_ProjectOperation_Project (&pi);

	if (currentUserId != nullptr) {
		*currentUserId = (pi.teamwork ? pi.userId : 0);
	}

	return pi.teamwork != 0;
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


class CustomerManagerDialog : public DG::ModalDialog,
							  public DG::CompoundItemObserver,
							  public DG::PanelObserver,
							  public DG::SplitButtonObserver,
							  public DG::ListBoxObserver
{
	DG::Button					okButton;
	DG::Button					cancelButton;
	DG::SingleSelListBox		customerListBox;
	DG::Button					newButton;
	DG::Button					modifyButton;
	DG::Button					deleteButton;
	DG::Button					reserveButton;
	DG::Button					releaseButton;
	DG::SplitButton				grantButton;
	DG::Button					requestButton;
	DG::SingleSelListBox		uniqueSettingsListBox;
	DG::SplitButton				newUniqueButton;
	DG::Button					modifyUniqueButton;
	DG::Button					deleteUniqueButton;
	DG::Button					reserveUniqueButton;
	DG::Button					releaseUniqueButton;
	DG::SplitButton				grantUniqueButton;
	DG::Button					requestUniqueButton;

	bool						isTeamwork;
	short						currentUserId;

	GS::Array<GS::Guid>			twOperationGuids;
	GS::UniString				newUniqueName;

	CloseReason					closeReason;

	short						grantUserId;

	bool						isUpdateItemsEnabled;

	enum {
		OkButtonId				= 1,
		CancelButtonId			= 2,
		CustomerListId			= 3,
		NewButtonId				= 5,
		ModifyButtonId			= 6,
		DeleteButtonId			= 7,
		ReserveButtonId			= 8,
		ReleaseButtonId			= 9,
		GrantButtonId			= 10,
		RequestButtonId			= 11,

		UniqueListId			= 13,
		NewUniqueButtonId		= 15,
		ModifyUniqueButtonId	= 16,
		DeleteUniqueButtonId	= 17,
		ReserveUniqueButtonId	= 18,
		ReleaseUniqueButtonId	= 19,
		GrantUniqueButtonId		= 20,
		RequestUniqueButtonId	= 21
	};

public:
	CustomerManagerDialog (GSResModule dialModule, short resId, GSResModule dialIconModule);
	~CustomerManagerDialog ();

	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;
	virtual void	SplitButtonPopupChanged (const DG::SplitButtonChangeEvent& ev) override;

			void	HandlePanelAccepted (IAddOnData* addOnDataToHandle);
	virtual void	PanelClosed (const DG::PanelCloseEvent& ev) override;

	virtual void	ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev) override;

	CloseReason		GetCloseReason (void) const;
	short			GetGrantUserId (void) const;
	void			GetTWOperationGuids (GS::Array<GS::Guid>* twOperationGuidArray) const;
	GS::UniString	GetNewUniqueName () const;

	void			UpdateListbox (DG::SingleSelListBox& listBox, const UserIdToUserNameTable& userIdToUserNameTable);
	void			UpdateItems ();

private:
	void			SetTabData (short customerListTabFieldCount, short uniqueSettingsListTabFieldCount);
	void			Init (void);
	void			SetListItemText (GS::UniString& addOnObjectName, short index, const UserIdToUserNameTable& userIdToUserNameTable);
};


CustomerManagerDialog::CustomerManagerDialog (GSResModule dialResModule, short resId, GSResModule dialIconResModule):
	DG::ModalDialog (dialResModule, resId, dialIconResModule),
	okButton (GetReference (), OkButtonId),
	cancelButton (GetReference (), CancelButtonId),
	customerListBox (GetReference (), CustomerListId),
	newButton (GetReference (), NewButtonId),
	modifyButton (GetReference (), ModifyButtonId),
	deleteButton (GetReference (), DeleteButtonId),
	reserveButton (GetReference (), ReserveButtonId),
	releaseButton (GetReference (), ReleaseButtonId),
	grantButton (GetReference (), GrantButtonId),
	requestButton (GetReference (), RequestButtonId),
	uniqueSettingsListBox (GetReference (), UniqueListId),
	newUniqueButton (GetReference (), NewUniqueButtonId),
	modifyUniqueButton (GetReference (), ModifyUniqueButtonId),
	deleteUniqueButton (GetReference (), DeleteUniqueButtonId),
	reserveUniqueButton (GetReference (), ReserveUniqueButtonId),
	releaseUniqueButton (GetReference (), ReleaseUniqueButtonId),
	grantUniqueButton (GetReference (), GrantUniqueButtonId),
	requestUniqueButton (GetReference (), RequestUniqueButtonId),
	closeReason (CloseReason_Nothing),
	grantUserId (-1),
	isUpdateItemsEnabled (true)
{
	isTeamwork = IsTeamworkProject (&currentUserId);

	AttachToAllItems (*this);
	Init ();
	this->Attach (*this);
}


CustomerManagerDialog::~CustomerManagerDialog ()
{
}


void	CustomerManagerDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	} else if (ev.GetSource () == &newButton) {
		CustomerData* customerData = new CustomerData ();
		if (customerData != nullptr) {
			customerData->guid.Generate ();
			customerData->GenerateRandomContent ();
			customerData->created = true;
			customerListBox.AppendItem ();
			customerListBox.SetItemValue (DG::ListBox::BottomItem, reinterpret_cast<DGUserData> (customerData));
			UpdateItems ();
		}
	} else if (ev.GetSource () == &modifyButton) {
		short listItem = customerListBox.GetSelectedItem ();
		if (listItem > 0) {
			CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (listItem));
			if (!customerData->markedAsDeleted && !customerData->created) {
				CustomerData customerDataCopy (*customerData);
				CloseReason currCloseReason = CloseReason_Nothing;
				if (ModifyAddOnData (customerDataCopy, currCloseReason)) {
					*customerData = customerDataCopy;
					customerData->modified = true;
					UpdateItems ();
					customerListBox.SetItemColor (listItem, Gfx::Color::Blue);
					if (currCloseReason != CloseReason_Nothing) {
						twOperationGuids.Clear ();
						twOperationGuids.Push (customerData->guid);
						closeReason = currCloseReason;
						if (closeReason != CloseReason_Nothing)
							PostCloseRequest (DG::ModalDialog::Accept);
					}
				}
			}
		}
	} else if (ev.GetSource () == &deleteButton) {
		short listItem = customerListBox.GetSelectedItem ();
		if (listItem > 0) {
			CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (listItem));
			if (customerData->created) {
				delete customerData;
				customerListBox.DeleteItem (listItem);
			} else {
				customerData->modified = false;
				customerData->markedAsDeleted = true;
				customerListBox.SetItemColor (listItem, Gfx::Color::Red);
			}
			UpdateItems ();
		}
	} else if (ev.GetSource () == &modifyUniqueButton) {
		short listItem = uniqueSettingsListBox.GetSelectedItem ();
		if (listItem > 0) {
			SettingData* settingData = reinterpret_cast<SettingData*> (uniqueSettingsListBox.GetItemValue (listItem));
			if (!settingData->markedAsDeleted && !settingData->created) {
				SettingData settingDataCopy (*settingData);
				CloseReason currCloseReason = CloseReason_Nothing;
				if (ModifyAddOnData (settingDataCopy, currCloseReason)) {
					*settingData = settingDataCopy;
					settingData->modified = true;
					UpdateItems ();
					uniqueSettingsListBox.SetItemColor (listItem, Gfx::Color::Blue);
					if (currCloseReason != CloseReason_Nothing) {
						twOperationGuids.Clear ();
						twOperationGuids.Push (settingData->guid);
						closeReason = currCloseReason;
						if (closeReason != CloseReason_Nothing)
							PostCloseRequest (DG::ModalDialog::Accept);
					}
				}
			}
		}
	} else if (ev.GetSource () == &deleteUniqueButton) {
		short listItem = uniqueSettingsListBox.GetSelectedItem ();
		if (listItem > 0) {
			SettingData* settingData = reinterpret_cast<SettingData*> (uniqueSettingsListBox.GetItemValue (listItem));
			if (settingData->created) {
				delete settingData;
				uniqueSettingsListBox.DeleteItem (listItem);
			} else {
				settingData->modified = false;
				settingData->markedAsDeleted = true;
				uniqueSettingsListBox.SetItemColor (listItem, Gfx::Color::Red);
			}
			UpdateItems ();
		}
	} else if (ev.GetSource () == &reserveButton || ev.GetSource () == &reserveUniqueButton ||
			   ev.GetSource () == &releaseButton || ev.GetSource () == &releaseUniqueButton ||
			   ev.GetSource () == &requestButton || ev.GetSource () == &requestUniqueButton ||
			   ev.GetSource () == &grantButton || ev.GetSource () == &grantUniqueButton)
	{
		IAddOnData* addOnData = nullptr;
		short listItem = 0;
		if (ev.GetSource () == &reserveButton || ev.GetSource () == &releaseButton || ev.GetSource () == &requestButton || ev.GetSource () == &grantButton) {
			listItem = customerListBox.GetSelectedItem ();
			if (listItem > 0) {
				addOnData = reinterpret_cast<IAddOnData*> (customerListBox.GetItemValue (listItem));
			}
		} else if (ev.GetSource () == &reserveUniqueButton || ev.GetSource () == &releaseUniqueButton || ev.GetSource () == &requestUniqueButton || ev.GetSource () == &grantUniqueButton) {
			listItem = uniqueSettingsListBox.GetSelectedItem ();
			if (listItem > 0) {
				addOnData = reinterpret_cast<IAddOnData*> (uniqueSettingsListBox.GetItemValue (listItem));
			}
		} else {
			DBBREAK ();
		}
		if (listItem > 0) {
			twOperationGuids.Clear ();
			twOperationGuids.Push (addOnData->guid);
			if (ev.GetSource () == &reserveButton || ev.GetSource () == &reserveUniqueButton) {
				closeReason = CloseReason_Reserve;
			} else if (ev.GetSource () == &releaseButton || ev.GetSource () == &releaseUniqueButton) {
				closeReason = CloseReason_Release;
			} else if (ev.GetSource () == &requestButton || ev.GetSource () == &requestUniqueButton) {
				closeReason = CloseReason_Request;
			} else if (ev.GetSource () == &grantButton || ev.GetSource () == &grantUniqueButton) {
				closeReason = CloseReason_Grant;
			} else {
				DBBREAK ();
			}
			PostCloseRequest (DG::ModalDialog::Accept);
		}
	}
}


void	CustomerManagerDialog::SplitButtonPopupChanged (const DG::SplitButtonChangeEvent& ev)
{
	if (ev.GetSource () == &grantButton) {
		UIndex grantUserItem = grantButton.GetSelectedItem ();
		GS::UniString grantUserString = grantButton.GetItemText ((short)grantUserItem);

		IAddOnData* addOnData = nullptr;
		short listItem = customerListBox.GetSelectedItem ();
		if (listItem > 0) {
			addOnData = reinterpret_cast<IAddOnData*> (customerListBox.GetItemValue (listItem));
		} else {
			listItem = uniqueSettingsListBox.GetSelectedItem ();
			addOnData = reinterpret_cast<IAddOnData*> (uniqueSettingsListBox.GetItemValue (listItem));
		}

		if (listItem > 0) {
			twOperationGuids.Clear ();
			twOperationGuids.Push (addOnData->guid);
			closeReason = CloseReason_Grant;

			UserIdToUserNameTable userTable;
			GetUserIdToUserNameTable (&userTable);
			for (auto it = userTable.EnumeratePairs (); it != nullptr; ++it) {
				short userId = it->key;
				GS::UniString userName = it->value;
				if (userName == grantUserString) {
					grantUserId = userId;
				}
			}

			PostCloseRequest (DG::ModalDialog::Accept);
		}
	} else if (ev.GetSource () == &newUniqueButton) {
		UIndex uniqueIndex = newUniqueButton.GetSelectedItem ();
		newUniqueName = UniqueAddOnObjectNames[uniqueIndex - 1];
		closeReason = CloseReason_CreateUnique;
		PostCloseRequest (DG::ModalDialog::Accept);
	}
}


void CustomerManagerDialog::HandlePanelAccepted (IAddOnData* addOnDataToHandle)
{
	if (addOnDataToHandle->markedAsDeleted) {
		bool exists = false;
		if (DBVERIFY (ACAPI_AddOnObject_ExistsObject (GSGuid2APIGuid (addOnDataToHandle->guid), &exists) == NoError && exists)) {
			ACAPI_AddOnObject_DeleteObject (GSGuid2APIGuid (addOnDataToHandle->guid));
		}
	} else if (addOnDataToHandle->modified) {
		bool exists = false;
		if (DBVERIFY (ACAPI_AddOnObject_ExistsObject (GSGuid2APIGuid (addOnDataToHandle->guid), &exists) == NoError && exists)) {
			GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
			if (addOnDataToHandle->Write (memChannel) == NoError) {
				GSHandle content = nullptr;
				BMPtrToHandle (memChannel.GetDestination (), &content, memChannel.GetDataSize ());
				ACAPI_AddOnObject_ModifyObject (GSGuid2APIGuid (addOnDataToHandle->guid), nullptr, &content);
				BMKillHandle (&content);
			}
		}
	}
}


void	CustomerManagerDialog::PanelClosed (const DG::PanelCloseEvent& ev)
{
	isUpdateItemsEnabled = false;

	for (short listItem = 1; listItem <= customerListBox.GetItemCount (); ++listItem) {
		CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (listItem));
		if (ev.IsAccepted ()) {
			if (customerData->created) {
				GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
				if (customerData->Write (memChannel) == NoError) {
					GSHandle content = nullptr;
					BMPtrToHandle (memChannel.GetDestination (), &content, memChannel.GetDataSize ());
					API_Guid objectId;
					ACAPI_AddOnObject_CreateObject (AddOnObjectName, content, &objectId);

					BMKillHandle (&content);

					GS::Guid gsObjectId = APIGuid2GSGuid (objectId);
					twOperationGuids.Replace (customerData->guid, gsObjectId);
				}
			} else {
				HandlePanelAccepted (customerData);
			}
		}
		delete customerData;
	}

	for (short listItem = 1; listItem <= uniqueSettingsListBox.GetItemCount (); ++listItem) {
		SettingData* settingData = reinterpret_cast<SettingData*> (uniqueSettingsListBox.GetItemValue (listItem));
		if (ev.IsAccepted ()) {
			HandlePanelAccepted (settingData);
		}
		delete settingData;
	}
}


void CustomerManagerDialog::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& /*ev*/)
{
	UpdateItems ();
}


void CustomerManagerDialog::SetTabData (short customerListTabFieldCount, short uniqueSettingsListTabFieldCount)
{
	if (customerListBox.GetTabFieldCount () != customerListTabFieldCount) {
		DG::Rect lBox = customerListBox.GetRect ();
		lBox.SetRight (lBox.GetRight () - 44);
		short width = (short)(lBox.GetRight () - lBox.GetLeft ());

		short columnWidth = (short)(width / customerListTabFieldCount);

		customerListBox.SetTabFieldCount (customerListTabFieldCount);
		for (short i = 1; i <= customerListTabFieldCount; ++i) {
			customerListBox.SetTabFieldProperties (i, (i - 1) * columnWidth, i * columnWidth, DG::ListBox::Left, DG::ListBox::EndTruncate, false, true);
		}
	}

	if (uniqueSettingsListBox.GetTabFieldCount () != uniqueSettingsListTabFieldCount) {
		DG::Rect lBox = uniqueSettingsListBox.GetRect ();
		lBox.SetRight (lBox.GetRight () - 44);
		short width = (short)(lBox.GetRight () - lBox.GetLeft ());

		short columnWidth = (short)(width / uniqueSettingsListTabFieldCount);

		uniqueSettingsListBox.SetTabFieldCount (uniqueSettingsListTabFieldCount);
		for (short i = 1; i <= uniqueSettingsListTabFieldCount; ++i) {
			uniqueSettingsListBox.SetTabFieldProperties (i, (i - 1) * columnWidth, i * columnWidth, DG::ListBox::Left, DG::ListBox::EndTruncate, false, true);
		}
	}
}


void CustomerManagerDialog::UpdateListbox (DG::SingleSelListBox& listBox, const UserIdToUserNameTable& userIdToUserNameTable)
{
	short selectedItem = listBox.GetSelectedItem ();
	if (selectedItem == 0 && listBox.GetItemCount () > 0) {
		listBox.SelectItem (1);
		selectedItem = 1;
	}

	for (short listItem = 1; listItem <= listBox.GetItemCount (); ++listItem) {
		GS::UniString addOnObjectName = AddOnObjectName;
		if (&listBox == &uniqueSettingsListBox) {
			SettingData* settingData = reinterpret_cast<SettingData*> (uniqueSettingsListBox.GetItemValue (listItem));
			addOnObjectName = settingData->name;
			DBASSERT (UniqueAddOnObjectNames.Contains (addOnObjectName));
		}

		SetListItemText (addOnObjectName, listItem, userIdToUserNameTable);
		if (selectedItem > 0 && selectedItem == listItem) {
			if (isTeamwork) {
				IAddOnData* addOnData = nullptr;
				if (&listBox == &customerListBox) {
					addOnData = reinterpret_cast<IAddOnData*> (customerListBox.GetItemValue (listItem));
				} else if (&listBox == &uniqueSettingsListBox) {
					addOnData = reinterpret_cast<IAddOnData*> (uniqueSettingsListBox.GetItemValue (listItem));
				} else {
					DBBREAK ();
					return;
				}

				GSErrCode err = NoError;
				short ownerId = 0;
				if (addOnData->created) {
					ownerId = currentUserId;
				} else {
					err = ACAPI_AddOnObject_GetTeamworkOwnerId (GSGuid2APIGuid (addOnData->guid), &ownerId);
				}
				if (err == NoError) {
					if (&listBox == &customerListBox) {
						reserveButton.SetStatus (ownerId == 0);
						releaseButton.SetStatus (ownerId == currentUserId);
						grantButton.SetStatus (ownerId == currentUserId && userIdToUserNameTable.GetSize () > 1);
						requestButton.SetStatus (ownerId != 0 && ownerId != currentUserId);
						deleteButton.SetStatus (ownerId == currentUserId);
						if (IsModifyButtonReservationDependent ()) {
							modifyButton.SetStatus (ownerId == currentUserId);
						}
					} else if (&listBox == &uniqueSettingsListBox) {
						reserveUniqueButton.SetStatus (ownerId == 0);
						releaseUniqueButton.SetStatus (ownerId == currentUserId);
						grantUniqueButton.SetStatus (ownerId == currentUserId && userIdToUserNameTable.GetSize () > 1);
						requestUniqueButton.SetStatus (ownerId != 0 && ownerId != currentUserId);
						deleteUniqueButton.SetStatus (ownerId == currentUserId);
						if (IsModifyButtonReservationDependent ()) {
							modifyUniqueButton.SetStatus (ownerId == currentUserId);
						}
					}
				} else {
					if (&listBox == &customerListBox) {
						reserveButton.Disable ();
						releaseButton.Disable ();
						grantButton.Disable ();
						requestButton.Disable ();
						deleteButton.Disable ();
						modifyButton.Disable ();
					} else if (&listBox == &uniqueSettingsListBox) {
						reserveUniqueButton.Disable ();
						releaseUniqueButton.Disable ();
						grantUniqueButton.Disable ();
						requestUniqueButton.Disable ();
						deleteUniqueButton.Disable ();
						modifyUniqueButton.Disable ();
					}
				}
			}
		}
	}
}


void	CustomerManagerDialog::UpdateItems (void)
{
	if (!isUpdateItemsEnabled)
		return;

	SetTabData (isTeamwork ? 5 : 4, isTeamwork ? 3 : 2);

	UserIdToUserNameTable userIdToUserNameTable;
	GetUserIdToUserNameTable (&userIdToUserNameTable);

	UpdateListbox (customerListBox, userIdToUserNameTable);
	UpdateListbox (uniqueSettingsListBox, userIdToUserNameTable);

	reserveButton.SetVisibility (isTeamwork);
	releaseButton.SetVisibility (isTeamwork);
	grantButton.SetVisibility (isTeamwork);
	requestButton.SetVisibility (isTeamwork);
	reserveUniqueButton.SetVisibility (isTeamwork);
	releaseUniqueButton.SetVisibility (isTeamwork);
	grantUniqueButton.SetVisibility (isTeamwork);
	requestUniqueButton.SetVisibility (isTeamwork);

	if (isTeamwork) {
		grantButton.DeleteAllItems ();
		for (auto it = userIdToUserNameTable.EnumeratePairs (); it != nullptr; ++it) {
			if (it->key == currentUserId) {
				continue;
			}

			grantButton.AppendItem (it->value);
		}
	}
}


void CustomerManagerDialog::SetListItemText (GS::UniString& addOnObjectName, short index, const UserIdToUserNameTable& userIdToUserNameTable)
{
	if (addOnObjectName == AddOnObjectName) {
		CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (index));
		customerListBox.SetTabItemText (index, 1, addOnObjectName);
		customerListBox.SetTabItemText (index, 2, customerData->name);
		customerListBox.SetTabItemText (index, 3, customerData->city);
		customerListBox.SetTabItemText (index, 4, customerData->country);
		if (isTeamwork) {
			short ownerId = 0;
			ACAPI_AddOnObject_GetTeamworkOwnerId (GSGuid2APIGuid (customerData->guid), &ownerId);
			GS::UniString itemString;
			if (userIdToUserNameTable.ContainsKey (ownerId)) {
				itemString = userIdToUserNameTable[ownerId];
			}
			customerListBox.SetTabItemText (index, 5, itemString);
		}
	} else if (UniqueAddOnObjectNames.Contains (addOnObjectName)) {
		SettingData* settingData = reinterpret_cast<SettingData*> (uniqueSettingsListBox.GetItemValue (index));
		uniqueSettingsListBox.SetTabItemText (index, 1, addOnObjectName);
		uniqueSettingsListBox.SetTabItemText (index, 2, GS::ValueToUniString (settingData->value));
		if (isTeamwork) {
			short ownerId = 0;
			ACAPI_AddOnObject_GetTeamworkOwnerId (GSGuid2APIGuid (settingData->guid), &ownerId);
			GS::UniString itemString;
			if (userIdToUserNameTable.ContainsKey (ownerId)) {
				itemString = userIdToUserNameTable[ownerId];
			}
			uniqueSettingsListBox.SetTabItemText (index, 3, itemString);
		}
	} else {
		DBBREAK_STR ("Unknown object");
	}
}


void CustomerManagerDialog::Init (void)
{
	GS::Array<API_Guid> objects;
	ACAPI_AddOnObject_GetObjectList (&objects);

	for (GS::Array<API_Guid>::ConstIterator it = objects.Enumerate (); it != nullptr; ++it) {
		API_Guid objectGuid = *it;
		GS::UniString objectName;
		GSHandle content = nullptr;
		if (ACAPI_AddOnObject_GetObjectContent (objectGuid, &objectName, &content) == NoError) {
			if (objectName == AddOnObjectName) {
				CustomerData* customerData = new CustomerData ();
				customerData->guid = APIGuid2GSGuid (objectGuid);
				if (content != nullptr) {
					GS::MemoryIChannel32 mic (*content, BMhGetSize (content));
					customerData->Read (mic);
				}
				customerListBox.AppendItem ();
				customerListBox.SetItemValue (DG::ListBox::BottomItem, reinterpret_cast<DGUserData> (customerData));
			} else if (UniqueAddOnObjectNames.Contains (objectName)) {
				SettingData* settingData = new SettingData ();
				settingData->name = objectName;
				settingData->guid = APIGuid2GSGuid (objectGuid);
				if (content != nullptr) {
					GS::MemoryIChannel32 mic (*content, BMhGetSize (content));
					settingData->Read (mic);
				}
				uniqueSettingsListBox.AppendItem ();
				uniqueSettingsListBox.SetItemValue (DG::ListBox::BottomItem, reinterpret_cast<DGUserData> (settingData));
			} else {
				DBBREAK_STR ("Unknown name.");
			}
		}
		BMKillHandle (&content);
	}

	newUniqueButton.DeleteAllItems ();
	for (short i = 0; i < static_cast<short> (UniqueAddOnObjectNames.GetSize ()); ++i) {
		char locStr[3200];
		ACAPI_Resource_GetLocStr (locStr, 32501, 2 + i);
		GS::UniString localisedName (locStr);
		newUniqueButton.AppendItem (localisedName);
	}

	UpdateItems ();
}


CloseReason	CustomerManagerDialog::GetCloseReason (void) const
{
	return closeReason;
}


short	CustomerManagerDialog::GetGrantUserId (void) const
{
	return grantUserId;
}


void	CustomerManagerDialog::GetTWOperationGuids (GS::Array<GS::Guid>* twOperationGuidArray) const
{
	*twOperationGuidArray = twOperationGuids;
}


GS::UniString CustomerManagerDialog::GetNewUniqueName () const
{
	return newUniqueName;
}


class ClientOnlyManagerDialog:	public DG::ModalDialog,
								public DG::CompoundItemObserver,
								public DG::PanelObserver,
								public DG::SplitButtonObserver,
								public DG::ListBoxObserver {

	DG::Button					okButton;
	DG::Button					cancelButton;
	DG::SingleSelListBox		customerListBox;
	DG::Button					newButton;
	DG::Button					modifyButton;
	DG::Button					deleteButton;

	bool						isUpdateItemsEnabled;

	enum {
		OkButtonId = 1,
		CancelButtonId = 2,
		CustomerListId = 3,
		NewButtonId = 5,
		ModifyButtonId = 6,
		DeleteButtonId = 7,
	};

public:
	ClientOnlyManagerDialog (GSResModule dialModule, short resId, GSResModule dialIconModule);
	~ClientOnlyManagerDialog ();

	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;

	void	HandlePanelAccepted (IAddOnData* addOnDataToHandle);
	virtual void	PanelClosed (const DG::PanelCloseEvent& ev) override;

	void			UpdateListbox (DG::SingleSelListBox& listBox, const UserIdToUserNameTable& userIdToUserNameTable);
	void			UpdateItems ();

private:
	void			SetTabData (short customerListTabFieldCount);
	void			Init (void);
	void			SetListItemText (GS::UniString& addOnObjectName, short index, const UserIdToUserNameTable& userIdToUserNameTable);
};


ClientOnlyManagerDialog::ClientOnlyManagerDialog (GSResModule dialResModule, short resId, GSResModule dialIconResModule) :
	DG::ModalDialog (dialResModule, resId, dialIconResModule),
	okButton (GetReference (), OkButtonId),
	cancelButton (GetReference (), CancelButtonId),
	customerListBox (GetReference (), CustomerListId),
	newButton (GetReference (), NewButtonId),
	modifyButton (GetReference (), ModifyButtonId),
	deleteButton (GetReference (), DeleteButtonId),
	isUpdateItemsEnabled (true)
{
	AttachToAllItems (*this);
	Init ();
	this->Attach (*this);
}


void ClientOnlyManagerDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	} else if (ev.GetSource () == &newButton) {
		CustomerData* customerData = new CustomerData ();
		if (customerData != nullptr) {
			customerData->guid.Generate ();
			customerData->GenerateRandomContent ();
			customerData->created = true;
			customerListBox.AppendItem ();
			customerListBox.SetItemValue (DG::ListBox::BottomItem, reinterpret_cast<DGUserData> (customerData));
			UpdateItems ();
		}
	} else if (ev.GetSource () == &modifyButton) {
		short listItem = customerListBox.GetSelectedItem ();
		if (listItem > 0) {
			CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (listItem));
			if (!customerData->markedAsDeleted && !customerData->created) {
				CustomerData customerDataCopy (*customerData);
				customerDataCopy.GenerateRandomContent ();
				*customerData = customerDataCopy;
				customerData->modified = true;
				UpdateItems ();
				customerListBox.SetItemColor (listItem, Gfx::Color::Blue);
			}
		}
	} else if (ev.GetSource () == &deleteButton) {
		short listItem = customerListBox.GetSelectedItem ();
		if (listItem > 0) {
			CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (listItem));
			if (customerData->created) {
				delete customerData;
				customerListBox.DeleteItem (listItem);
			} else {
				customerData->modified = false;
				customerData->markedAsDeleted = true;
				customerListBox.SetItemColor (listItem, Gfx::Color::Red);
			}
			UpdateItems ();
		}
	}
}


void ClientOnlyManagerDialog::UpdateItems (void)
{
	if (!isUpdateItemsEnabled)
		return;

	SetTabData (4);

	UserIdToUserNameTable userIdToUserNameTable;
	GetUserIdToUserNameTable (&userIdToUserNameTable);

	UpdateListbox (customerListBox, userIdToUserNameTable);
}


void ClientOnlyManagerDialog::SetTabData (short customerListTabFieldCount)
{
	if (customerListBox.GetTabFieldCount () != customerListTabFieldCount) {
		DG::Rect lBox = customerListBox.GetRect ();
		lBox.SetRight (lBox.GetRight () - 44);
		short width = (short)(lBox.GetRight () - lBox.GetLeft ());

		short columnWidth = (short)(width / customerListTabFieldCount);

		customerListBox.SetTabFieldCount (customerListTabFieldCount);
		for (short i = 1; i <= customerListTabFieldCount; ++i) {
			customerListBox.SetTabFieldProperties (i, (i - 1) * columnWidth, i * columnWidth, DG::ListBox::Left, DG::ListBox::EndTruncate, false, true);
		}
	}

}


void ClientOnlyManagerDialog::UpdateListbox (DG::SingleSelListBox& listBox, const UserIdToUserNameTable& userIdToUserNameTable)
{
	short selectedItem = listBox.GetSelectedItem ();
	if (selectedItem == 0 && listBox.GetItemCount () > 0) {
		listBox.SelectItem (1);
		selectedItem = 1;
	}

	for (short listItem = 1; listItem <= listBox.GetItemCount (); ++listItem) {
		GS::UniString addOnObjectName = AddOnClientOnlyObject;

		SetListItemText (AddOnClientOnlyObject, listItem, userIdToUserNameTable);
	}
}


void ClientOnlyManagerDialog::SetListItemText (GS::UniString& addOnObjectName, short index, const UserIdToUserNameTable& /*userIdToUserNameTable*/)
{
	if (addOnObjectName == AddOnClientOnlyObject) {
		CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (index));
		customerListBox.SetTabItemText (index, 1, addOnObjectName);
		customerListBox.SetTabItemText (index, 2, customerData->name);
		customerListBox.SetTabItemText (index, 3, customerData->city);
		customerListBox.SetTabItemText (index, 4, customerData->country);
	}
}


void ClientOnlyManagerDialog::HandlePanelAccepted (IAddOnData* addOnDataToHandle)
{
	if (addOnDataToHandle->markedAsDeleted) {
		bool exists = false;
		if (DBVERIFY (ACAPI_AddOnObject_ExistsObject (GSGuid2APIGuid (addOnDataToHandle->guid), &exists) == NoError && exists)) {
			ACAPI_AddOnObject_DeleteObject (GSGuid2APIGuid (addOnDataToHandle->guid));
		}
	} else if (addOnDataToHandle->modified) {
		bool exists = false;
		if (DBVERIFY (ACAPI_AddOnObject_ExistsObject (GSGuid2APIGuid (addOnDataToHandle->guid), &exists) == NoError && exists)) {
			GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
			if (addOnDataToHandle->Write (memChannel) == NoError) {
				GSHandle content = nullptr;
				BMPtrToHandle (memChannel.GetDestination (), &content, memChannel.GetDataSize ());
				ACAPI_AddOnObject_ModifyObject (GSGuid2APIGuid (addOnDataToHandle->guid), nullptr, &content);
				BMKillHandle (&content);
			}
		}
	}
}


void ClientOnlyManagerDialog::PanelClosed (const DG::PanelCloseEvent& ev)
{
	isUpdateItemsEnabled = false;

	for (short listItem = 1; listItem <= customerListBox.GetItemCount (); ++listItem) {
		CustomerData* customerData = reinterpret_cast<CustomerData*> (customerListBox.GetItemValue (listItem));
		if (ev.IsAccepted ()) {
			if (customerData->created) {
				GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
				if (customerData->Write (memChannel) == NoError) {
					GSHandle content = nullptr;
					BMPtrToHandle (memChannel.GetDestination (), &content, memChannel.GetDataSize ());
					API_Guid objectId;
					ACAPI_AddOnObject_CreateClientOnlyObject (AddOnClientOnlyObject, content, &objectId);

					BMKillHandle (&content);

				}
			} else {
				HandlePanelAccepted (customerData);
			}
		}
		delete customerData;
	}

}


void ClientOnlyManagerDialog::Init (void)
{
	GS::Array<API_Guid> objects;
	ACAPI_AddOnObject_GetObjectList (&objects);

	for (GS::Array<API_Guid>::ConstIterator it = objects.Enumerate (); it != nullptr; ++it) {
		API_Guid objectGuid = *it;
		GS::UniString objectName;
		GSHandle content = nullptr;
		if (ACAPI_AddOnObject_GetObjectContent (objectGuid, &objectName, &content) == NoError) {
			if (objectName == AddOnClientOnlyObject) {
				CustomerData* customerData = new CustomerData ();
				customerData->guid = APIGuid2GSGuid (objectGuid);
				if (content != nullptr) {
					GS::MemoryIChannel32 mic (*content, BMhGetSize (content));
					customerData->Read (mic);
				}
				customerListBox.AppendItem ();
				customerListBox.SetItemValue (DG::ListBox::BottomItem, reinterpret_cast<DGUserData> (customerData));
			}
		}
		BMKillHandle (&content);
	}

	UpdateItems ();
}


ClientOnlyManagerDialog::~ClientOnlyManagerDialog ()
{

}


GSErrCode ReservationChangedHandler (const GS::HashTable<API_Guid, short>&	reserved,
											     const GS::HashSet<API_Guid>&			released,
												 const GS::HashSet<API_Guid>&			/*deleted*/)
{
	if (!DGIsDialogOpen (CustomerManagerDialogResourceId))
		return NoError;

	bool addOnObjectFound = false;
	for (auto it = reserved.EnumeratePairs (); it != nullptr && !addOnObjectFound; ++it) {
		API_Guid objectGuid = it->key;
		bool exists = false;
		if (ACAPI_AddOnObject_ExistsObject (objectGuid, &exists) == NoError && exists)
			addOnObjectFound = true;
	}

	for (auto it = released.Enumerate (); it != nullptr && !addOnObjectFound; ++it) {
		API_Guid objectGuid = *it;
		bool exists = false;
		if (ACAPI_AddOnObject_ExistsObject (objectGuid, &exists) == NoError && exists)
			addOnObjectFound = true;
	}

	DG::Dialog* dialog = DG::GetDialogFromId (CustomerManagerDialogResourceId);
	if (dialog != nullptr) {
		CustomerManagerDialog* customerManagerDialog = dynamic_cast<CustomerManagerDialog*> (dialog);
		if (DBVERIFY (customerManagerDialog != nullptr))
			customerManagerDialog->UpdateItems ();
	}

	return NoError;
}

// -----------------------------------------------------------------------------
// DoCommand
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
	if (params->menuItemRef.itemIndex == 1) {
		bool invokeDialog = true;
		while (invokeDialog) {
			invokeDialog = false;

			CustomerManagerDialog customerManagerDialog (ACAPI_GetOwnResModule (), CustomerManagerDialogResourceId,  ACAPI_GetOwnResModule ());
			bool result = customerManagerDialog.Invoke ();

			if (result && customerManagerDialog.GetCloseReason () != CloseReason_Nothing) {
				GS::Array<API_Guid> objectGuids;
				GS::Array<GS::Guid> twOperationGuids;
				customerManagerDialog.GetTWOperationGuids (&twOperationGuids);
				for (auto it = twOperationGuids.Enumerate (); it != nullptr; ++it)
					objectGuids.Push (GSGuid2APIGuid (*it));

				if (customerManagerDialog.GetCloseReason () == CloseReason_Reserve) {
					ACAPI_AddOnObject_ReserveObjects (objectGuids, nullptr/*&conflicts*/);
					invokeDialog = true;
				} else if (customerManagerDialog.GetCloseReason () == CloseReason_Release) {
					ACAPI_AddOnObject_ReleaseObjects (objectGuids);
					invokeDialog = true;
				} else if (customerManagerDialog.GetCloseReason () == CloseReason_Request) {
					ACAPI_AddOnObject_RequestObjects (objectGuids, GS::UniString ());
					invokeDialog = true;
				} else if (customerManagerDialog.GetCloseReason () == CloseReason_Grant) {
					short currentUserId = 0;
					IsTeamworkProject (&currentUserId);

					short toUserId = customerManagerDialog.GetGrantUserId ();
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
						ACAPI_AddOnObject_GrantObjects (objectGuids, toUserId);
					}

					invokeDialog = true;
				} else if (customerManagerDialog.GetCloseReason () == CloseReason_CreateUnique) {
					GS::UniString uniqueName = customerManagerDialog.GetNewUniqueName ();
					ACAPI_AddOnObject_CreateUniqueObject (uniqueName, nullptr);
					invokeDialog = true;
				}
			}
		}
	} else {
		if (params->menuItemRef.itemIndex == 2) {
			ClientOnlyManagerDialog clientOnlyManagerDialog (ACAPI_GetOwnResModule (), ClientOnlyObjectMenuStringResourceId, ACAPI_GetOwnResModule ());
			/*bool result = */clientOnlyManagerDialog.Invoke ();
		}
	}

	return NoError;
}		// MenuCommandHandler


// -----------------------------------------------------------------------------
// AddOnObject Merge event handler
// -----------------------------------------------------------------------------

static void	CopyAPIAddOnObject (API_AddonObject& target, const API_AddonObject& source)
{
	target	  = {};
	target.id = source.id;
	if (source.name != nullptr) {
		target.name = new GS::UniString (*source.name);
	}
	if (source.data != nullptr) {
		BMHandleToHandle (source.data, &target.data);
	}
	target.type = source.type; //you might want to handle type==UniqueAddOnObject when saving old version
}


static bool EqualAddOnObjects (const GS::UniString& name1, GSConstHandle data1, const GS::UniString& name2, GSConstHandle data2)
{
	// id is ignored here
	if (name1 != name2) {
		return false;
	}

	if ((data1 == nullptr) != (data2 == nullptr)) {
		return false;
	}
	if (data1 != nullptr) {
		GS::MemoryIChannel32 mic1 (*data1, BMhGetSize (data1));
		GS::MemoryIChannel32 mic2 (*data2, BMhGetSize (data2));

		if (name1 == AddOnObjectName) {
			CustomerData customerData1;
			CustomerData customerData2;
			customerData1.Read (mic1);
			customerData2.Read (mic2);
			return (customerData1.name == customerData2.name &&
					customerData1.city == customerData2.city &&
					customerData1.country == customerData2.country);
		} else if (UniqueAddOnObjectNames.Contains (name2)) {
			SettingData settingData1;
			SettingData settingData2;
			settingData1.Read (mic1);
			settingData2.Read (mic2);
			return (settingData1.value == settingData2.value);
		} else {
			DBBREAK_STR ("Unknown name.");
			return false;
		}
	}
	DBBREAK ();
	return false;
}


GSErrCode APIAddonObjectMergeHandler (const GS::Array<API_AddonObject>&	sourceObjects)
{
	GS::Array<API_Guid> targetObjects;
	GSErrCode err = ACAPI_AddOnObject_GetObjectList (&targetObjects);

	for (auto it = sourceObjects.Enumerate (); it != nullptr && err == NoError; ++it) {
		const API_AddonObject& sourceObject = *it;
		bool found = false;
		for (auto it2 = targetObjects.Enumerate (); it2 != nullptr && !found && err == NoError; ++it2) {
			GS::UniString objectName;
			GSHandle objectContent = nullptr;
			err = ACAPI_AddOnObject_GetObjectContent (*it2, &objectName, &objectContent);
			if (err == NoError) {
				found = EqualAddOnObjects (*sourceObject.name, sourceObject.data, objectName, objectContent);
				if (!found) {
					if (UniqueAddOnObjectNames.Contains (*sourceObject.name) && *sourceObject.name == objectName) {
						found = true; //we chose not to resolve UniqueAddOnObject conflicts, so objects with an already existing name will be dropped
					}
				}
			}
			BMhKill (&objectContent);
		}
		if (found || err != NoError) {
			continue;
		}

		API_Guid guid;
		if (*sourceObject.name == AddOnObjectName) {
			err = ACAPI_AddOnObject_CreateObject (*sourceObject.name, sourceObject.data, &guid);
		} else if (UniqueAddOnObjectNames.Contains (*sourceObject.name)) {
			err = ACAPI_AddOnObject_CreateUniqueObject (*sourceObject.name, &guid); //these objects could be collected and created at once with ACAPI_AddOnObject_CreateUniqueObjectMore() function
			if (err != NoError && IsTeamworkProject (nullptr)) {
				err = ACAPI_AddOnObject_ReserveObjects ({ guid }, nullptr);
			}
			if (err != NoError) {
				err = ACAPI_AddOnObject_ModifyObject (guid, nullptr, &sourceObject.data);
			}
			if (err != NoError && IsTeamworkProject (nullptr)) {
				err = ACAPI_AddOnObject_ReleaseObjects ({ guid });
			}
		} else {
			DBBREAK_STR ("Unknown name.");
		}
	}

	return err;
}		// APIAddonObjectMergeHandler


// -----------------------------------------------------------------------------
// AddOnObject Save into old format project file
// -----------------------------------------------------------------------------

GSErrCode APIAddonObjectSaveOldFormatHandler (API_FTypeID							/*planFileType*/,
																const GS::Array<API_AddonObject>&	originalInfo,
																GS::Array<API_AddonObject>&			destInfo)
{
	for (auto it = originalInfo.Enumerate (); it != nullptr; ++it) {
		API_AddonObject tmp;
		CopyAPIAddOnObject (tmp, *it);
		destInfo.Push (tmp);
	}

	return NoError;
}		// APIAddonObjectSaveOldFormatHandler

// -----------------------------------------------------------------------------
// AddOnObject convert into new format project file
// -----------------------------------------------------------------------------

GSErrCode APIAddonObjectConvertNewFormatHandler (API_FTypeID						/*planFileType*/,
																  const GS::Array<API_AddonObject>&	originalInfo,
																  GS::Array<API_AddonObject>&		destInfo)

{
	for (auto it = originalInfo.Enumerate (); it != nullptr; ++it) {
		API_AddonObject tmp;
		CopyAPIAddOnObject (tmp, *it);
		destInfo.Push (tmp);
	}

	return NoError;
}		// APIAddonObjectConvertNewFormatHandler

// =============================================================================
//
// Required functions
//
// =============================================================================


//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	ACAPI_MenuItem_RegisterMenu (MenuStringResourceId, 0, MenuCode_UserDef, MenuFlag_Default);
	ACAPI_AddOnObject_RegisterAddOnObjectHandler ();

	return NoError;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (MenuStringResourceId, MenuCommandHandler);
	if (err == NoError)
		err = ACAPI_Notification_CatchElementReservationChange (ReservationChangedHandler);
	if (err == NoError)
		err = ACAPI_AddOnObject_InstallAddOnObjectMergeHandler (APIAddonObjectMergeHandler);
	if (err == NoError)
		err = ACAPI_AddOnObject_InstallAddOnObjectSaveOldFormatHandler (APIAddonObjectSaveOldFormatHandler);
	if (err == NoError)
		err = ACAPI_AddOnObject_InstallAddOnObjectConvertNewFormatHandler (APIAddonObjectConvertNewFormatHandler);

	char str[3200];
	ACAPI_Resource_GetLocStr (str, 32501, 1);
	GS::UniString localisedName (str);
	ACAPI_AddOnObject_SetUIProperties (nullptr, &AddOnObjectName, localisedName, MenuStringResourceId);
	ACAPI_AddOnObject_SetUIProperties (nullptr, &AddOnObjectName, localisedName, ClientOnlyObjectMenuStringResourceId);

	for (short i = 0; i < static_cast<short> (UniqueAddOnObjectNames.GetSize ()); ++i) {
		char locStr[3200];
		ACAPI_Resource_GetLocStr (locStr, 32501, 2 + i);
		GS::UniString localisedName (locStr);
		ACAPI_AddOnObject_SetUIProperties (nullptr, &UniqueAddOnObjectNames[i], localisedName, MenuStringResourceId);
	}

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
