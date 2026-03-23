#include "Resources.hpp"

#include <queue>
#include <map>
#include <string>

// from GS
#include "Overloaded.hpp"

// from DG
#include "DG.h"
                       
// from API
#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APICommon.h"

// from ACAPI
#include "ACAPI/Result.hpp"
#include "ACAPI_Navigator.h"
#include "ACAPI/View.hpp"
#include "ACAPI/DesignOptionManager.hpp"

namespace {

using namespace ACAPI::DesignOptions::v2;

ACAPI::Result<std::vector<API_NavigatorItem>> GetLeafNavigatorItems (API_NavigatorMapID mapID)
{
	Int32 i = 0;
	API_NavigatorSet set = {};
	set.mapId = mapID;

	if (ACAPI_Navigator_GetNavigatorSet (&set, &i) != NoError) {
		return { ACAPI::Error (APIERR_GENERAL, "Failed to retrieve the list of items in the navigator."), ACAPI_GetToken () };
	}

	API_NavigatorItem rootItem = {};
	rootItem.guid = set.rootGuid;
	rootItem.mapId = mapID;

	std::queue<API_NavigatorItem> itemsToProcess;
	itemsToProcess.emplace (rootItem);

	std::vector<API_NavigatorItem> result;

	while (!itemsToProcess.empty ()) {
		GS::Array<API_NavigatorItem> childItems;
		API_NavigatorItem parentItem = itemsToProcess.front ();
		itemsToProcess.pop ();
		GSErrCode err = ACAPI_Navigator_GetNavigatorChildrenItems (&parentItem, &childItems);
		if (err != NoError) {
			continue;
		}

		for (const auto& childItem : childItems) {
			if (childItem.itemType == API_FolderNavItem || childItem.itemType == API_UndefinedNavItem) {
				itemsToProcess.emplace (childItem);
			} else {
				result.emplace_back (childItem);
			}
		}
	}

	return ACAPI::Ok (result);
}

void DumpDesignOptionCombinations ()
{
	const ACAPI::Result<std::vector<API_NavigatorItem>> mapItems = GetLeafNavigatorItems (API_PublicViewMap);
	if (mapItems.IsErr ()) {
		WriteReport ("Failed to get navigator map items.");
		return;
	}

	// ! [Get-View-Design-Option-Combination-Example]

	ACAPI::Result<ACAPI::DesignOptions::DesignOptionManager> manager = CreateDesignOptionManager ();
	if (manager.IsErr ()) {
		WriteReport ("Failed to get Design Option Manager.");
		return;
	}

	for (const API_NavigatorItem& item : mapItems.Unwrap ()) {
		const ACAPI::Result<ACAPI::ModelViews::View> view = ACAPI::ModelViews::FindViewByGuid (item.guid);
		if (view.IsErr ()) {
			continue;
		}

		const ACAPI::Result<ACAPI::DesignOptions::DesignOptionCombinationViewSettings> settings =
			manager->GetDesignOptionCombinationSettingsOf (view.Unwrap ());
		if (settings.IsErr ()) {
			continue;
		}

		const ACAPI::Result<ACAPI::DesignOptions::DesignOptionCombinationViewSettings::Status> status = settings->GetStatus ();
		if (status.IsErr ()) {
			continue;
		}

		const GS::UniString itemId (item.uiId);
		const GS::UniString itemIdPrefix = !itemId.IsEmpty () ? itemId + " " : GS::EmptyUniString;
		const GS::UniString itemName (item.uName);
		const GS::UniString fullName = itemIdPrefix + itemName;
		std::visit (GS::Overloaded {
			[&fullName] (const ACAPI::DesignOptions::DesignOptionCombinationViewSettings::MainModelOnly&) {
				WriteReport ("%s: Main Model Only.", fullName.ToCStr ().Get ());
			},
			[&fullName] (const ACAPI::DesignOptions::DesignOptionCombinationViewSettings::Standard& standard) {
				WriteReport ("%s: %s.", fullName.ToCStr ().Get (), standard.combination.GetName ().ToCStr ().Get ());
			},
			[&fullName] (const ACAPI::DesignOptions::DesignOptionCombinationViewSettings::Custom& /*custom*/) {
				WriteReport ("%s: Custom.", fullName.ToCStr ().Get ());
			},
			[&fullName] (const ACAPI::DesignOptions::DesignOptionCombinationViewSettings::Missing&) {
				WriteReport ("%s: Missing.", fullName.ToCStr ().Get ());
			},
		}, status.Unwrap ());
	}

	// ! [Get-View-Design-Option-Combination-Example]
}

void SetAllDesignOptionCombinationsToMainModelOnly ()
{
	// ! [Set-View-Design-Option-Combination-Example]

	const ACAPI::Result<std::vector<API_NavigatorItem>> mapItems = GetLeafNavigatorItems (API_PublicViewMap);
	if (mapItems.IsErr ()) {
		WriteReport ("Failed to get navigator map items.");
		return;
	}

	ACAPI::Result<ACAPI::DesignOptions::DesignOptionManager> manager = CreateDesignOptionManager ();
	if (manager.IsErr ()) {
		WriteReport ("Failed to get Design Option Manager.");
		return;
	}

	for (const API_NavigatorItem& item : mapItems.Unwrap ()) {
		const ACAPI::Result<ACAPI::ModelViews::View> view = ACAPI::ModelViews::FindViewByGuid (item.guid);
		if (view.IsErr ()) {
			continue;
		}

		ACAPI::Result<ACAPI::DesignOptions::DesignOptionCombinationViewSettings> settings =
			manager->GetDesignOptionCombinationSettingsOf (view.Unwrap ());
		if (settings.IsErr ()) {
			continue;
		}

		settings->Modify ([] (ACAPI::DesignOptions::DesignOptionCombinationViewSettings::Modifier& modifier) {
			modifier.SetMainModelOnly ();
		});
	}

	// ! [Set-View-Design-Option-Combination-Example]
}

void DumpAvailableDesignOptionCombinations ()
{
	// ! [Get-Design-Option-Combinations-Example]

	ACAPI::Result<ACAPI::DesignOptions::DesignOptionManager> manager = CreateDesignOptionManager ();
	if (manager.IsErr ()) {
		WriteReport ("Failed to get Design Option Manager.");
		return;
	}

	const auto combinations = manager->GetAvailableDesignOptionCombinations ();
	if (combinations.IsErr ()) {
		WriteReport ("Failed to get available design option combinations.");
		return;
	}

	for (const auto& combination : combinations.Unwrap ()) {
		WriteReport ("%s (%s)", combination.GetName ().ToCStr ().Get (), combination.GetGuid ().ToUniString ().ToCStr ().Get ());
	}

	// ! [Get-Design-Option-Combinations-Example]
}

std::vector<API_Guid>	GetSelectedElements ()
{
	GSErrCode            err;
	API_SelectionInfo    selectionInfo;
	GS::Array<API_Neig>  selNeigs;

	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	BMKillHandle ((GSHandle*)&selectionInfo.marquee.coords);
	if (err == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
		DGAlert (DG_ERROR, "Error", "Please select an element!", "", "Ok");
	}

	if (err != NoError) {
		return std::vector<API_Guid> ();
	}

	std::vector<API_Guid> guidArray;
	for (const API_Neig& neig : selNeigs) {
		guidArray.push_back (neig.guid);
	}

	return guidArray;
}

void DumpDesignOptionRelatedInfoOfElement (const API_Guid& elemGuid)
{
	static const std::map<std::string, API_Guid> designOptionProperties {
				{"Design_Option_Name",			APIGuidFromString ("114FC56B-160D-4CCF-91F1-EAD505051C9A")},
				{"Design_Option_ID",			APIGuidFromString ("84DD2D3F-50DD-4BF3-A810-6CAB81C37540")},
				{"Design_Option_Set_Name",		APIGuidFromString ("59634BB2-6D8C-4A4B-B349-172F4157974B")},
				{"Design_Option_Is_Missing",	APIGuidFromString ("5CC582DF-EC76-4A09-AFB9-B97FADA4F2EC")},
				{"Design_Option_Status",		APIGuidFromString ("5C3FB768-C349-49E7-B6E6-7675A26A2AEC")}};

	WriteReport ("Design Option Properties for element %s", APIGuidToString (elemGuid).ToCStr ().Get ());

	for (const auto& [name, id] : designOptionProperties) {
		GS::Array<API_Property> properties;
		GSErrCode err = ACAPI_Element_GetPropertyValuesByGuid (elemGuid, {id}, properties);
		if (err != NoError) {
			WriteReport ("Failed to get value for %s", name.c_str ());
			return;
		}
		GS::UniString valueInString;
		ACAPI_Property_GetPropertyValueString (properties.GetFirst (), &valueInString);
		WriteReport ("%s: %s", name.c_str (), valueInString.ToCStr ().Get ());
	}
}

void DumpDesignOptionRelatedInfoForSelectedElements ()
{
	for (const API_Guid& elemGuid : GetSelectedElements ()) {
		DumpDesignOptionRelatedInfoOfElement (elemGuid);
	}
}

}


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case DESIGNOPTIONS_TEST_MENU_STRINGS: {
			switch (menuParams->menuItemRef.itemIndex) {
				case DumpViewMapDesignOptionCombinationsID:
					DumpDesignOptionCombinations ();
					return NoError;
				case SetAllDesignOptionCombinationsToMainModelOnlyID:
					SetAllDesignOptionCombinationsToMainModelOnly ();
					return NoError;
				case DumpAvailableDesignOptionCombinationsID:
					DumpAvailableDesignOptionCombinations ();
					return NoError;
				case DumpDesignOptionRelatedInfoForSelectedElementsID:
					DumpDesignOptionRelatedInfoForSelectedElements ();
					return NoError;
				default:
					DBBREAK ();
					return Error;
			}
			break;
		}
	}

	return NoError;
}

// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name,        DESIGNOPTIONS_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, DESIGNOPTIONS_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface ()
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (DESIGNOPTIONS_TEST_MENU_STRINGS, DESIGNOPTIONS_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default);
	DBASSERT (err == NoError);
	return err;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize ()
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (DESIGNOPTIONS_TEST_MENU_STRINGS, MenuCommandHandler);
	DBASSERT (err == NoError);
	ACAPI_KeepInMemory (true);
	return err;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData ()
{
	return NoError;
}
