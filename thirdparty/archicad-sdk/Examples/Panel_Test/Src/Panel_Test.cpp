// *****************************************************************************
// Source code for the Panel Test Add-On
// *****************************************************************************

// --- Includes ----------------------------------------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APIdefs.h"

#include "Panel_Test_Resource.h"
#include "Panels.hpp"
#include "SettingsHandler.hpp"

#include "DGModule.hpp"

// -----------------------------------------------------------------------------
//
// Global variables and definitions
//
// -----------------------------------------------------------------------------

using namespace PanelTest;

TestSettingsPanel*	settingsPanel  = nullptr;
TestAttributePanel*	attributePanel = nullptr;
TestInfoBoxPanel*	infoBoxPanel   = nullptr;
TestSettingsHandler* settingsHandler = nullptr;

enum {
	SettingsPanelRefCon					= 1,
	AttributePanelRefCon				= 2,
	InfoBoxPanelRefCon					= 3,

	SettingsPanelRegistered				= 1 << SettingsPanelRefCon,
	AttributePanelRegistered			= 1 << AttributePanelRefCon,
	InfoBoxPanelRegistered				= 1 << InfoBoxPanelRefCon,

	SettingsObserverRegistered			= 1 << 4
};

int	registrationSuccess = 0;

// -----------------------------------------------------------------------------
// Create tabpage callback function
// -----------------------------------------------------------------------------

static	GSErrCode CreatePageCallback (Int32 refCon, const void* tabControl, void* data, void** tabPage)
{
	bool success = false;
	const DG::TabControl*	control = reinterpret_cast<const DG::TabControl*>(tabControl);
	DG::TabPage**			page = reinterpret_cast<DG::TabPage**>(tabPage);

	switch (refCon) {
		case SettingsPanelRefCon:		if (settingsPanel != nullptr)
											success = settingsPanel->CreatePage (*control, reinterpret_cast<TBUI::IAPIToolUIData*>(data), page);
										break;

		case AttributePanelRefCon:		if (attributePanel != nullptr)
											success = attributePanel->CreatePage (*control, reinterpret_cast<VBAD::IAPIAttrUIData*>(data), page);
										break;

		case InfoBoxPanelRefCon:		if (infoBoxPanel != nullptr)
											success = infoBoxPanel->CreatePage (*control, reinterpret_cast<TBUI::IAPIToolUIData*>(data), page);
										break;
	}

	return (success ? NoError : (GSErrCode) APIERR_GENERAL);
}


// -----------------------------------------------------------------------------
// Destroy tabpage callback function
// -----------------------------------------------------------------------------

static	GSErrCode DestroyPageCallback (Int32 refCon, void* /*tabPage*/)
{
	switch (refCon) {
		case SettingsPanelRefCon:		if (settingsPanel != nullptr)
											settingsPanel->DestroyPage ();
										break;

		case AttributePanelRefCon:		if (attributePanel != nullptr)
											attributePanel->DestroyPage ();
										break;

		case InfoBoxPanelRefCon:		if (infoBoxPanel != nullptr)
											infoBoxPanel->DestroyPage ();
										break;
	}

	return NoError;
}


// -----------------------------------------------------------------------------
// Create settings callback
// -----------------------------------------------------------------------------

static	GSErrCode CreateSettingsCallback (void* data)
{
	GSErrCode err = NoError;

	TBUI::IAPIToolUIData* uiData = reinterpret_cast<TBUI::IAPIToolUIData*> (data);
	if (uiData == nullptr)
		return APIERR_GENERAL;

	settingsHandler = new TestSettingsHandler (uiData);

	return err;
}


// -----------------------------------------------------------------------------
// Destroy settings callback
// -----------------------------------------------------------------------------

static	GSErrCode DestroySettingsCallback (void* /*data*/)
{
	GSErrCode err = NoError;

	delete settingsHandler;
	settingsHandler = nullptr;

	return err;
}


// -----------------------------------------------------------------------------
//
// Required functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = NoError;

	// ------ Custom Rolldown Panel into the Wall Settings dialog
	err = ACAPI_AddOnIntegration_RegisterSettingsPanel (SettingsPanelRefCon, API_WallID, IDS_SETTINGSPAGE_ICON, IDS_SETTINGSPAGE_NAME, SettingsPageId);
	if (err != NoError) {
		DBPrintf ("Panel_Test add-on: Cannot register element settings panel\n");
	} else {
		registrationSuccess |= SettingsPanelRegistered;
	}

	// ------ Custom Rolldown Panel into the Material Settings dialog
	err = ACAPI_AddOnIntegration_RegisterAttributePanel (AttributePanelRefCon, API_MaterialID, IDS_ATTRIBUTEPAGE_ICON, IDS_ATTRIBUTEPAGE_NAME, AttributePageId, 0);
	if (err != NoError) {
		DBPrintf ("Panel_Test add-on: Cannot register attribute settings panel\n");
	} else {
		registrationSuccess |= AttributePanelRegistered;
	}

	// ------ Custom Rolldown Panel into the Building Material Settings dialog
	err = ACAPI_AddOnIntegration_RegisterAttributePanel (AttributePanelRefCon, API_BuildingMaterialID, IDS_ATTRIBUTEPAGE_ICON, IDS_ATTRIBUTEPAGE_NAME, AttributePageId, 0);
	if (err != NoError) {
		DBPrintf ("Panel_Test add-on: Cannot register attribute settings panel\n");
	} else {
		registrationSuccess |= AttributePanelRegistered;
	}

	// ------ Custom Panel into the Info Box
	err = ACAPI_AddOnIntegration_RegisterInfoBoxPanel (InfoBoxPanelRefCon, API_WallID, IDS_INFOBOXPAGE_NAME, InfoBoxPageId);
	if (err != NoError) {
		DBPrintf ("Panel_Test add-on: Cannot register info box panel\n");
	} else {
		registrationSuccess |= InfoBoxPanelRegistered;
	}


	// ------ Settings Observer
	err = ACAPI_AddOnIntegration_RegisterSettingsObserver (API_WallID);
	if (err != NoError) {
		DBPrintf ("Panel_Test add-on: Cannot register settings observer\n");
	} else {
		registrationSuccess |= SettingsObserverRegistered;
	}

	return NoError;
}


// -----------------------------------------------------------------------------
// Called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = NoError;

	// ------ Custom Rolldown Panel into the Wall Settings dialog
	if (registrationSuccess & SettingsPanelRegistered) {
		try {
			settingsPanel = new TestSettingsPanel (SettingsPanelRefCon);
		}
		catch (...) {
			DBPrintf ("Panel_Test add-on: settingsPanel construction failed\n");
			settingsPanel = nullptr;
		}

		if (settingsPanel != nullptr) {
			err = ACAPI_AddOnIntegration_InstallPanelHandler (settingsPanel->GetRefCon (), CreatePageCallback, DestroyPageCallback);
			if (err != NoError) {
				DBPrintf ("Panel_Test add-on: Element settings panel handler initialization failed\n");
			}
		}
	}

	// ------ Custom Rolldown Panel into the Material Settings dialog
	if (registrationSuccess & AttributePanelRegistered) {
		try {
			attributePanel = new TestAttributePanel (AttributePanelRefCon);
		}
		catch (...) {
			DBPrintf ("Panel_Test add-on: attributePanel construction failed\n");
			attributePanel = nullptr;
		}

		if (attributePanel != nullptr) {
			err = ACAPI_AddOnIntegration_InstallPanelHandler (attributePanel->GetRefCon (), CreatePageCallback, DestroyPageCallback);
			if (err != NoError) {
				DBPrintf ("Panel_Test add-on: Attribute settings panel handler initialization failed\n");
			}
		}
	}

	// ------ Custom Panel into the Info Box
	if (registrationSuccess & InfoBoxPanelRegistered) {
		try {
			infoBoxPanel = new TestInfoBoxPanel (InfoBoxPanelRefCon);
		}
		catch (...) {
			DBPrintf ("Panel_Test add-on: infoBoxPanel construction failed\n");
			infoBoxPanel = nullptr;
		}

		if (infoBoxPanel != nullptr) {
			err = ACAPI_AddOnIntegration_InstallPanelHandler (infoBoxPanel->GetRefCon (), CreatePageCallback, DestroyPageCallback);
			if (err != NoError) {
				DBPrintf ("Panel_Test add-on: Info box panel handler initialization failed\n");
			}
		}
	}

	// ------ Settings Handler
	if (registrationSuccess & SettingsObserverRegistered) {
		err = ACAPI_AddOnIntegration_InstallSettingsHandler (API_WallID, CreateSettingsCallback, DestroySettingsCallback);
		if (err != NoError) {
			DBPrintf ("Panel_Test add-on: Wall settings observer initialization failed\n");
		}
	}

	return NoError;
}


// -----------------------------------------------------------------------------
// Called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData	(void)
{
	if (settingsPanel != nullptr)
		delete settingsPanel;
	if (attributePanel != nullptr)
		delete attributePanel;
	if (infoBoxPanel != nullptr)
		delete infoBoxPanel;

	return NoError;
}
