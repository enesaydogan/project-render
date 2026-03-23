#include "Property_Visibility_Test.hpp"

#include "Visibility_Data.hpp"
#include "Property_Visibility_Dialog.hpp"
#include "Classification_Visibility_Dialog.hpp"

#include "MemoryIChannel32.hpp"
#include "MemoryOChannel32.hpp"

// this is where the visibility information is stored
static const Int32 currentVersion = 1;
static VisibilityData classificationVisibilityData;
static VisibilityData propertyVisibilityData;

static GSErrCode LoadModulData ()
{
	API_ModulData modulData;
	GSErrCode err = ACAPI_ModulData_Get (&modulData);
	if (err != NoError) {
		return err;
	}

	GS::MemoryIChannel32 memChannel (*modulData.dataHdl, BMGetHandleSize (modulData.dataHdl));
	err = classificationVisibilityData.Read (memChannel);
	if (err != NoError) {
		BMKillHandle (&modulData.dataHdl);
		return err;
	}

	err = propertyVisibilityData.Read (memChannel);
	BMKillHandle (&modulData.dataHdl);
	return err;
}

static GSErrCode SaveModulData ()
{
	GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
	GSErrCode err = classificationVisibilityData.Write (memChannel);
	if (err != NoError) {
		return err;
	}
	err = propertyVisibilityData.Write (memChannel);
	if (err != NoError) {
		return err;
	}

	API_ModulData modulData;
	BNZeroMemory (&modulData, sizeof (API_ModulData));
	modulData.dataVersion = currentVersion;
	modulData.platformSign = GS::Act_Platform_Sign;

	GSHandle dataHandle = BMAllocateHandle (memChannel.GetDataSize (), ALLOCATE_CLEAR, 0);
	BNCopyMemory (*dataHandle, memChannel.GetDestination (), memChannel.GetDataSize ());
	modulData.dataHdl = dataHandle;

	err = ACAPI_ModulData_Store (&modulData);
	BMKillHandle (&dataHandle);
	return err;
}

static GSErrCode HandlePropertyChangeEvent ()
{
	// remove stored guids of deleted definitions

	GS::HashSet<GS::Guid> existingPropertyDefinitions;

	GS::Array<API_PropertyGroup> groups;
	GSErrCode err = ACAPI_Property_GetPropertyGroups (groups);
	if (DBERROR (err != NoError)) {
		return err;
	}
	for (UIndex groupIndex = 0; groupIndex < groups.GetSize (); ++groupIndex) {
		const API_PropertyGroup& group = groups[groupIndex];
		if (group.groupType != API_PropertyCustomGroupType) {
			continue;
		}
		GS::Array<API_PropertyDefinition> definitions;
		ACAPI_Property_GetPropertyDefinitions (group.guid, definitions);
		for (UIndex definitionIndex = 0; definitionIndex < definitions.GetSize (); ++definitionIndex) {
			existingPropertyDefinitions.Add (APIGuid2GSGuid (definitions[definitionIndex].guid));
		}
	}

	propertyVisibilityData.RemoveDeletedItems (existingPropertyDefinitions);
	return err;
}

static GSErrCode HandleClassificationChangeEvent ()
{
	// remove stored guids of deleted items

	GS::HashSet<GS::Guid> existingClassificationItems;

	GS::Array<API_ClassificationSystem> systems;
	GSErrCode err = ACAPI_Classification_GetClassificationSystems (systems);
	if (DBERROR (err != NoError)) {
		return err;
	}
	for (API_ClassificationSystem& system : systems) {
		GS::Array<API_ClassificationItem> items;
		ACAPI_Classification_GetClassificationSystemRootItems (system.guid, items);
		for (API_ClassificationItem& item : items) {
			existingClassificationItems.Add (APIGuid2GSGuid (item.guid));

			std::function<void (API_ClassificationItem item)> processItemRecursively = [&] (API_ClassificationItem item) {
				GS::Array<API_ClassificationItem> children;
				ACAPI_Classification_GetClassificationItemChildren (item.guid, children);
				for (API_ClassificationItem& child : children) {
					existingClassificationItems.Add (APIGuid2GSGuid (child.guid));
					processItemRecursively (child);
				}
			};

			processItemRecursively (item);
		}
	}

	classificationVisibilityData.RemoveDeletedItems (existingClassificationItems);
	return err;
}

static GSErrCode ProjectEventHandler (API_NotifyEventID notifID, Int32 /*param*/)
{
	switch (notifID) {
	case APINotify_New:
	case APINotify_NewAndReset:
		propertyVisibilityData.Clear ();
		classificationVisibilityData.Clear ();
		break;
	case APINotify_Open:
		propertyVisibilityData.Clear ();
		classificationVisibilityData.Clear ();
		LoadModulData ();
		break;

	case APINotify_PropertyVisibilityChanged:
	case APINotify_ClassificationVisibilityChanged:
		HandleClassificationChangeEvent ();
		HandlePropertyChangeEvent ();
		break;
	default:
		break;
	}

	return NoError;
}

static bool ClassificationVisibilityHandler (API_ClassificationVisibilityMode visibilityMode, const GS::Array<API_Guid>& classificationGuids, const API_Guid& classificationItemGuid)
{
	if (visibilityMode == APIClassVis_IsEnabled) {
		// returns true if the property visibility handling enabled, false otherwise
		return classificationVisibilityData.IsEnabled ();
	} else if (visibilityMode == APIClassVis_IsClassificationVisible) {
		// returns true if the property should be shown, false otherwise
		UNUSED_PARAMETER (classificationGuids);
		if (!classificationVisibilityData.IsEnabled ()) {
			return true;
		}
		GS::Guid gsClassificationGuid = APIGuid2GSGuid (classificationItemGuid);
		if (classificationVisibilityData.IsItemVisible (gsClassificationGuid)) {
			return true;
		}
		return false;
	}

	DBBREAK_STR ("Invalid classification visibility mode.");
	return true;
}

static bool PropertyVisibilityHandler (API_PropertyVisibilityMode visibilityMode, const GS::Array<API_Guid>& classificationGuids, const API_Guid& propertyDefinitionGuid)
{
	if (visibilityMode == APIPropVis_IsEnabled) {
		// returns true if the property visibility handling enabled, false otherwise
		return propertyVisibilityData.IsEnabled ();
	} else if (visibilityMode == APIPropVis_IsPropertyVisible) {
		// returns true if the property should be shown, false otherwise
		UNUSED_PARAMETER (classificationGuids);
		if (!propertyVisibilityData.IsEnabled ()) {
			return true;
		}
		GS::Guid gsPropertyGuid = APIGuid2GSGuid (propertyDefinitionGuid);
		if (propertyVisibilityData.IsItemVisible (gsPropertyGuid)) {
			return true;
		}
		return false;
	}

	DBBREAK_STR ("Invalid property visibility mode.");
	return true;
}

GSErrCode APIMenuCommandProc_Main (const API_MenuParams *menuParams)
{
	if (menuParams->menuItemRef.menuResID == 32500) {
		if (menuParams->menuItemRef.itemIndex == 1) {
			ClassificationVisibilityDialog dialog (ACAPI_GetOwnResModule (), classificationVisibilityData);
			bool okPressed = dialog.Invoke ();
			if (okPressed) {
				classificationVisibilityData = dialog.GetClassificationVisibilityData ();
				ACAPI_Notification_ClassificationVisibilityChanged ();
				SaveModulData ();
			}
		} else if (menuParams->menuItemRef.itemIndex == 2) {
			PropertyVisibilityDialog dialog (ACAPI_GetOwnResModule (), propertyVisibilityData);
			bool okPressed = dialog.Invoke ();
			if (okPressed) {
				propertyVisibilityData = dialog.GetPropertyVisibilityData ();
				ACAPI_Notification_PropertyVisibilityChanged ();
				SaveModulData ();
			}
		}
	}

	ACAPI_KeepInMemory (true);
	return NoError;
}

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	// it is important to use preloaded add-on
	return APIAddon_Preload;
}

GSErrCode RegisterInterface (void)
{
	// tell the program that this add-on wants to register a menu command
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (32500, 32600, MenuCode_UserDef, MenuFlag_Default);
	if (err != NoError) {
		return err;
	}

	// tell the program that this add-on wants to modify classification and property visibility
	err = ACAPI_AddOnIntegration_RegisterClassificationVisibilityHandler ();
	err = ACAPI_Property_RegisterPropertyVisibilityHandler ();
	if (err != NoError) {
		return err;
	}

	return NoError;
}


GSErrCode Initialize	(void)
{
	// install the menu handler
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, APIMenuCommandProc_Main);
	if (err != NoError) {
		return err;
	}

	// register handler for project events to clear visible property list
	err = ACAPI_ProjectOperation_CatchProjectEvent (	APINotify_New |
											APINotify_NewAndReset |
											APINotify_Open |
											APINotify_PropertyVisibilityChanged |
											APINotify_ClassificationVisibilityChanged, ProjectEventHandler);
	if (err != NoError) {
		return err;
	}

	// register the handler functions for visibility modification
	err = ACAPI_AddOnIntegration_InstallClassificationVisibilityHandler (ClassificationVisibilityHandler);
	if (err != NoError) {
		return err;
	}

	err = ACAPI_Property_InstallPropertyVisibilityHandler (PropertyVisibilityHandler);
	if (err != NoError) {
		return err;
	}

	// do not unload the add-on
	ACAPI_KeepInMemory (true);
	return NoError;
}

GSErrCode FreeData	(void)
{
	return NoError;
}
