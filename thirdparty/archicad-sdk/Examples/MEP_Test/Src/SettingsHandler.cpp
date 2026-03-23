// --- Includes ----------------------------------------------------------------

#include "SettingsHandler.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APIdefs.h"


// --- Function definitions --------------------------------------------------------------------------------------------

namespace MEPExample {


TestSettingsHandler::TestSettingsHandler (TBUI::IAPIToolUIData* tuiData) :
	uiData (tuiData)
{
	ACAPI_WriteReport ("TestSettingsHandler: Settings Dialog is opened.", false);
	uiData->AttachObserver (this);
}


TestSettingsHandler::~TestSettingsHandler ()
{
	ACAPI_WriteReport ("TestSettingsHandler: Settings Dialog is closed.", false);
	uiData->DetachObserver (this);
}


void TestSettingsHandler::APIElementChanged (const TBUI::APIElemDefaultFieldMask& fieldMask)
{
	if (fieldMask.GetRegDataChanged ()) {
		ACAPI_WriteReport ("TestSettingsHandler: RegData changed.", false);
	}

	if (fieldMask.GetOriginalFieldsChanged ()) {
		ACAPI_WriteReport ("TestSettingsHandler: Original fields changed.", false);
	}

	if (fieldMask.GetPropertiesChanged ()) {
		ACAPI_WriteReport ("TestSettingsHandler: Properties changed.", false);
	}
}


}		// namespace MEPExample