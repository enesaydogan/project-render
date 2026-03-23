// *****************************************************************************
// Source code for the Panel Test Add-On
// *****************************************************************************

// --- Includes ----------------------------------------------------------------

#include "SettingsHandler.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APIdefs.h"

// --- Function definitions --------------------------------------------------------------------------------------------

namespace PanelTest {


TestSettingsHandler::TestSettingsHandler (TBUI::IAPIToolUIData* tuiData) :
	uiData (tuiData)
{
	API_Element elem;
	uiData->GetAPIElement (&elem);
	ACAPI_WriteReport ("TestSettingsHandler: Wall Settings Dialog is opened.", false);
	char msgStr[256];
	sprintf (msgStr, "Wall height is: %f", elem.wall.height);
	ACAPI_WriteReport (msgStr, false);
	uiData->AttachObserver (this);
}


TestSettingsHandler::~TestSettingsHandler ()
{
	API_Element elem;
	uiData->GetAPIElement (&elem);
	ACAPI_WriteReport ("TestSettingsHandler: Wall Settings Dialog is closed.", false);
	char msgStr[256];
	sprintf (msgStr, "Wall height is: %f", elem.wall.height);
	ACAPI_WriteReport (msgStr, false);
	uiData->DetachObserver (this);
}


void TestSettingsHandler::APIElementChanged (const TBUI::APIElemDefaultFieldMask& fieldMask)
{
	if (fieldMask.GetRegDataChanged ()) {
		ACAPI_WriteReport ("TestSettingsHandler: RegData changed.", false);
	}

	if (fieldMask.GetOriginalFieldsChanged ()) {
		ACAPI_WriteReport ("TestSettingsHandler: Original fields changed.", false);
		API_Element elem;
		uiData->GetAPIElement (&elem);
		char msgStr[256];
		sprintf (msgStr, "Wall height is: %f", elem.wall.height);
		ACAPI_WriteReport (msgStr, false);
	}
	if (fieldMask.GetPropertiesChanged ()) {
		ACAPI_WriteReport ("TestSettingsHandler: Properties changed.", false);
	}
}


}		// namespace PanelTest