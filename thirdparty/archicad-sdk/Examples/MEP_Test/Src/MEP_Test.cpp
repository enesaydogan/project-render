#include "MEP_Test.hpp"
#include "Panels.hpp"
#include "SettingsHandler.hpp"
#include "Resources.hpp"

#include "QueryElementsAndDefaults.hpp"
#include "PlaceElements.hpp"
#include "PlacePolylineFlexibleSegments.hpp"
#include "ModifyElements.hpp"
#include "Preferences.hpp"
#include "Utility.hpp"
#include "LabelingElements.hpp"
#include "DistributionSystemsGraph.hpp"
#include "GraphCalculationInterface.hpp"

// API
#include "ACAPinc.h"

// MEPPlugin
#include "ACAPI/MEPAdapter.hpp"


// -----------------------------------------------------------------------------
//
// Global variables and definitions
//
// -----------------------------------------------------------------------------

MEPExample::TestSettingsPanel*		settingsPanel = nullptr;
MEPExample::TestSettingsHandler*	settingsHandler = nullptr;

#ifndef NO_MEP_FEATURES

const Int32 settingsPanelRefCon = 1;


// -----------------------------------------------------------------------------
// Create tabpage callback function
// -----------------------------------------------------------------------------


static GSErrCode CreatePageCallback (Int32 /*refCon*/, const void* tabControl, void* data, void** tabPage)
{
	bool success = false;
	const DG::TabControl* control = reinterpret_cast<const DG::TabControl*>(tabControl);
	DG::TabPage** page = reinterpret_cast<DG::TabPage**>(tabPage);

	if (settingsPanel != nullptr)
		success = settingsPanel->CreatePage (*control, reinterpret_cast<TBUI::IAPIToolUIData*>(data), page);

	return (success ? NoError : (GSErrCode) APIERR_GENERAL);
}


// -----------------------------------------------------------------------------
// Destroy tabpage callback function
// -----------------------------------------------------------------------------

static GSErrCode DestroyPageCallback (Int32 /*refCon*/, void* /*tabPage*/)
{
	if (settingsPanel != nullptr)
		settingsPanel->DestroyPage ();

	return NoError;
}


// -----------------------------------------------------------------------------
// Create settings callback
// -----------------------------------------------------------------------------

static GSErrCode CreateSettingsCallback (void* data)
{
	GSErrCode err = NoError;

	TBUI::IAPIToolUIData* uiData = reinterpret_cast<TBUI::IAPIToolUIData*> (data);
	if (uiData == nullptr)
		return APIERR_GENERAL;

	settingsHandler = new MEPExample::TestSettingsHandler (uiData);

	return err;
}


// -----------------------------------------------------------------------------
// Destroy settings callback
// -----------------------------------------------------------------------------

static GSErrCode DestroySettingsCallback (void* /*data*/)
{
	GSErrCode err = NoError;

	delete settingsHandler;
	settingsHandler = nullptr;

	return err;
}
#endif


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case MEP_TEST_MENU_STRINGS:
		{
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					return MEPExample::PlaceMEPElements ();
				case 2:
					return MEPExample::CopyPlaceSelectedTerminals ();
				case 3:
					break; // separator
				case 4:
					return MEPExample::PlaceTwoContinuousRoutes ();
				case 5:
					return MEPExample::PlaceTwoRoutesInTShape ();
				case 6:
					return MEPExample::PlaceThreeRoutesInTShape ();
				case 7:
					return MEPExample::PlaceConnectedElements ();
				case 8:
					return MEPExample::PlaceAndConnectRoutes ();
				case 9:
					return MEPExample::PlaceAnAccessoryBetweenTwoRoutes ();
				case 10:
					return MEPExample::PlacePolylineFlexibleSegments ();
				case 11:
					break; // separator
				case 12:
					return MEPExample::QuerySelectedElementDetails ();
				case 13:
					return MEPExample::QueryDefaultDetails ();
				case 14:
					break; // separator
				case 15:
					return MEPExample::ModifySelectedBranches ();
				case 16:
					return MEPExample::ModifySelectedRoutingElements ();
				case 17:
					return MEPExample::ModifySelectedFlexibleSegments ();
				case 18:
					return MEPExample::ModifySelectedTakeOff ();
				case 19:
					return MEPExample::ShiftAndReorientSelectedMEPElements ();
				case 20:
					return MEPExample::ModifySelectedElemGeneralParameters ();
				case 21: 
					return MEPExample::ModifySelectedPipeFittingGDLParameters ();
				case 22: 
					return MEPExample::ModifySelectedRoutingElemGDLParameters ();
				case 23: 
					return MEPExample::ModifyPipeFittingDefaultParameters ();
				case 24: 
					return MEPExample::ModifyRoutingElemDefaultParameters ();
				case 25:
					return MEPExample::ModifyLibraryPartOfSelectedPipeTerminal ();
				case 26:
					return MEPExample::ModifyLibraryPartOfDefaultPipeTerminalThenPlace ();
				case 27:
					return MEPExample::ModifyLibraryPartOfSelectedCableCarrierRoutesFirstRigidSegment ();
				case 28:
					return MEPExample::ModifyLibraryPartOfSelectedCableCarrierRoutesDefaultRigidSegment ();
				case 29:
					return MEPExample::ModifyLibraryPartOfDefaultCableCarrierRoutesRigidSegment ();
				case 30:
					break; // separator
				case 31: 
					return MEPExample::DeleteSelectedElements ();
				case 32:
					break; // separator
				case 33:
					return MEPExample::QueryMEPPreferences ();
				case 34:
					return MEPExample::ModifyDuctSegmentPreferences ();
				case 35:
					return MEPExample::ModifyPipeElbowPreferences ();
				case 36:
					return MEPExample::ModifyPipeBranchPreferences ();
				case 37:
					return MEPExample::ModifyDuctTransitionPreferences ();
				case 38:
					return MEPExample::ModifyPipeReferenceSet ();
				case 39:
					break; // separator
				case 40:
					return MEPExample::CreateElementSetFromSelectedElements ();
				case 41:
					return MEPExample::CreateElementLinksBetweenSelectedElements ();
				case 42:
					return MEPExample::LabelSubelementsOfSelectedRoute ();
				case 43:
					return MEPExample::LabelSelectedMEPElements ();
				case 44:
					return MEPExample::LabelConnectionsInGraph ();
				case 45:
					return MEPExample::UseCalculationInterface ();
			}
			break;
		}
	}

	return NoError;
}		// MenuCommandHandler


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
	RSGetIndString (&envir->addOnInfo.name,        MEP_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, MEP_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
#ifndef NO_MEP_FEATURES
	// Register Menu
	ERRCHK (ACAPI_MenuItem_RegisterMenu (MEP_TEST_MENU_STRINGS, MEP_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default));

	// Register Settings Panel
	API_ElemType mepRoutingType (ACAPI::MEP::PipingRoutingSegmentID);

	ERRCHK (ACAPI_AddOnIntegration_RegisterSettingsPanel (settingsPanelRefCon, mepRoutingType, IDS_SETTINGSPAGE_ICON, IDS_SETTINGSPAGE_NAME, SettingsPageId));
	ERRCHK (ACAPI_AddOnIntegration_RegisterSettingsObserver (mepRoutingType));

#endif

	return NoError;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
#ifndef NO_MEP_FEATURES
	ERRCHK (ACAPI_MenuItem_InstallMenuHandler (MEP_TEST_MENU_STRINGS, MenuCommandHandler));

	try {
		settingsPanel = new MEPExample::TestSettingsPanel (settingsPanelRefCon);
	}
	catch (...) {
		DBPrintf ("Panel_Test add-on: settingsPanel construction failed\n");
		settingsPanel = nullptr;
	}

	if (settingsPanel != nullptr) {
		ERRCHK (ACAPI_AddOnIntegration_InstallPanelHandler (settingsPanel->GetRefCon (), CreatePageCallback, DestroyPageCallback));
	}

	API_ElemType mepRoutingType (ACAPI::MEP::PipingRoutingSegmentID);
	ERRCHK (ACAPI_AddOnIntegration_InstallSettingsHandler (mepRoutingType, CreateSettingsCallback, DestroySettingsCallback));

#endif

	return NoError;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	if (settingsPanel != nullptr)
		delete settingsPanel;

	return NoError;
}
