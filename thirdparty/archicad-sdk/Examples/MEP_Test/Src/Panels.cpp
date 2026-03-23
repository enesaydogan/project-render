#include "Panels.hpp"
#include "Resources.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APIdefs.h"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"
#include "ACAPI/Result.hpp"
#include "ACAPI/MEPRoutingSegment.hpp"
#include "ACAPI/MEPRoutingElementUIManager.hpp"
#include "ACAPI/MEPRoutingElementDefault.hpp"
#include "ACAPI/MEPRoutingSegmentDefault.hpp"

// Geometry
#include "AngleData.h"


namespace MEPExample {

// --- Class definitions -------------------------------------------------------

// --- TestSettingsPage --------------------------------------------------------

TestSettingsPage::TestSettingsPage (const DG::TabControl& tabControl, TBUI::IAPIToolUIData* puiData)
	: DG::TabPage		(tabControl, 1, ACAPI_GetOwnResModule (), SettingsPageId, InvalidResModule)
	, diameterEdit		(GetReference (), DiameterEditId)
	, customTextEdit	(GetReference (), CustomTextEditId)
	, customValueEdit	(GetReference (), CustomValueEditId)
	, circumferenceValueEdit (GetReference (), CircumferenceValueEditValueEditId)
	, crossSectionAreaValueEdit (GetReference (), CrossSectionAreaValueEditId)
	, uiData			(puiData)
{
}


TestSettingsPage::~TestSettingsPage (void)
{
	uiData = nullptr;
}


// --- TestSettingsPageObserver ------------------------------------------------

TestSettingsPageObserver::TestSettingsPageObserver (TestSettingsPage* testPage):
	tabPage (testPage)
{
	tabPage->Attach (*this);
	AttachToAllItems (*tabPage);
	tabPage->uiData->AttachObserver (this);

	ACAPI::Result<ACAPI::MEP::RoutingSegmentUIManager> manager = ACAPI::MEP::CreateRoutingSegmentUIManager ();
	DBASSERT (manager.IsOk ());
	uiManager = GS::NewOwned<ACAPI::MEP::RoutingSegmentUIManager> (*manager);

	ACAPI::Result<ACAPI::MEP::RoutingSegmentDefault> routingSegmentDefault = uiManager->CreateRoutingSegmentDefaultFromToolUIData (*tabPage->uiData);
	
	DBASSERT (routingSegmentDefault.IsOk ());

	currentSegmentDefault = GS::NewOwned<ACAPI::MEP::RoutingSegmentDefault> (*routingSegmentDefault);
	tabPage->diameterEdit.SetValue (currentSegmentDefault->GetCrossSectionWidth ());
	tabPage->circumferenceValueEdit.SetValue (currentSegmentDefault->GetCrossSectionWidth () * PI);
	tabPage->crossSectionAreaValueEdit.SetValue (std::pow (currentSegmentDefault->GetCrossSectionWidth () / 2, 2) * PI);

	MEPUserData data;
	BNZeroMemory (&data, sizeof (MEPUserData));
	GetUserDataFromDefault (&data);

	tabPage->customTextEdit.SetText (data.customText);
	tabPage->customValueEdit.SetValue (data.customValue);
}


TestSettingsPageObserver::~TestSettingsPageObserver (void)
{
	tabPage->Detach (*this);
	DetachFromAllItems (*tabPage);
	tabPage->uiData->DetachObserver (this);
	tabPage = nullptr;
}


// ... Utility functions ......................................................

GSErrCode	TestSettingsPageObserver::GetUserDataFromDefault (MEPUserData* data)
{
	API_ElementUserData userData;
	BNZeroMemory (&userData, sizeof (API_ElementUserData));
	tabPage->uiData->GetUserData (&userData);

	// allocating area if needed
	if (userData.dataHdl == nullptr) {
		userData.dataHdl = BMAllocateHandle (sizeof(MEPUserData), ALLOCATE_CLEAR, 0);
		if (userData.dataHdl == nullptr)
			return APIERR_GENERAL;
	}

	// gets pointer
	MEPUserData* sdata = reinterpret_cast<MEPUserData*> (*userData.dataHdl);

	// copies from the data
	CHCopyC (sdata->customText, data->customText);
	data->customValue = sdata->customValue;

	// kill
	BMKillHandle (&userData.dataHdl);

	return NoError;
}


GSErrCode	TestSettingsPageObserver::SetUserDataToDefault (MEPUserData* sdata)
{
	API_ElementUserData userData;
	BNZeroMemory (&userData, sizeof (API_ElementUserData));

	// allocating
	userData.dataHdl = BMAllocateHandle (sizeof(MEPUserData), ALLOCATE_CLEAR, 0);
	if (userData.dataHdl == nullptr)
		return APIERR_GENERAL;			//!!! error!!!

	userData.flags = APIUserDataFlag_FillWith | APIUserDataFlag_Pickup;

	// gets pointer
	MEPUserData* data = reinterpret_cast<MEPUserData*>(*userData.dataHdl);

	// copies to the data
	CHCopyC (sdata->customText, data->customText);
	data->customValue = sdata->customValue;

	// sets to the default
	tabPage->uiData->SetUserData (&userData);

	BMKillHandle (&userData.dataHdl);

	return NoError;
}


// ... TabPage notifications ...................................................

void	TestSettingsPageObserver::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
}


void	TestSettingsPageObserver::PanelResized (const DG::PanelResizeEvent& /*ev*/)
{
}


// ... Dialog item notifications ...............................................

bool operator== (const API_Variant& lhs, const API_Variant& rhs)
{
	if (lhs.type != rhs.type) {
		return false;
	}

	switch (lhs.type) {
		case API_PropertyIntegerValueType:
			return lhs.intValue == rhs.intValue;
		case API_PropertyRealValueType:
			return lhs.doubleValue == rhs.doubleValue;
		case API_PropertyStringValueType:
			return lhs.uniStringValue == rhs.uniStringValue;
		case API_PropertyBooleanValueType:
			return lhs.boolValue == rhs.boolValue;
		case API_PropertyGuidValueType:
			return lhs.guidValue == rhs.guidValue;
		default:
			return false;
	}
}


void	TestSettingsPageObserver::APIElementChanged (const TBUI::APIElemDefaultFieldMask& fieldMask)
{
	if (fieldMask.GetRegDataChanged ()) {
		MEPUserData data;
		BNZeroMemory (&data, sizeof (MEPUserData));
		GetUserDataFromDefault (&data);

		tabPage->customTextEdit.SetText (data.customText);
		tabPage->customValueEdit.SetValue (data.customValue);
	}

	if (fieldMask.GetOriginalFieldsChanged ()) {
		
		ACAPI::Result<ACAPI::MEP::RoutingSegmentDefault> routingSegmentDefault = uiManager->CreateRoutingSegmentDefaultFromToolUIData (*tabPage->uiData);

		if (routingSegmentDefault.IsErr ())
			return;

		double diameter = routingSegmentDefault->GetCrossSectionWidth ();

		if (diameter != currentSegmentDefault->GetCrossSectionWidth ()) {
			tabPage->diameterEdit.SetValue (diameter);
			tabPage->circumferenceValueEdit.SetValue (diameter * PI);
			tabPage->crossSectionAreaValueEdit.SetValue (std::pow (diameter / 2, 2) * PI);

			currentSegmentDefault->Modify ([&] (ACAPI::MEP::RoutingSegmentDefault::Modifier& modifier) {
				modifier.SetCrossSectionWidth (diameter);
				modifier.SetCrossSectionHeight (diameter);
			});
		}
	}
}


void TestSettingsPageObserver::RealEditChanged (const DG::RealEditChangeEvent& ev) 
{
	if (ev.GetSource () == &tabPage->diameterEdit) {
		double diameter = tabPage->diameterEdit.GetValue ();
		tabPage->circumferenceValueEdit.SetValue (diameter * PI);
		tabPage->crossSectionAreaValueEdit.SetValue (std::pow (diameter / 2, 2) * PI);

		currentSegmentDefault->Modify ([&] (ACAPI::MEP::RoutingSegmentDefault::Modifier& modifier) {
			modifier.SetCrossSectionWidth (diameter);
			modifier.SetCrossSectionHeight (diameter);
		});
		ACAPI::Result<void> result = uiManager->ApplyRoutingSegmentDefaultToToolUIData (*tabPage->uiData, *currentSegmentDefault);
		DBASSERT (result.IsOk ());
	} else if (ev.GetSource () == &tabPage->customValueEdit) {
		MEPUserData data;
		BNZeroMemory (&data, sizeof (MEPUserData));

		GS::UniString buffer = tabPage->customTextEdit.GetText ();
		CHTruncate (buffer.ToCStr (), data.customText, Sizeof32 (data.customText));
		data.customValue = tabPage->customValueEdit.GetValue ();

		SetUserDataToDefault (&data);
	} else if (ev.GetSource () == &tabPage->circumferenceValueEdit) {
		double perimeter = tabPage->circumferenceValueEdit.GetValue ();
		tabPage->diameterEdit.SetValue (perimeter / PI);
		tabPage->crossSectionAreaValueEdit.SetValue (std::pow (perimeter / PI / 2, 2) * PI);

		currentSegmentDefault->Modify ([&](ACAPI::MEP::RoutingSegmentDefault::Modifier& modifier) {
			modifier.SetCrossSectionWidth (perimeter / PI);
			modifier.SetCrossSectionHeight (perimeter / PI);
		});
		ACAPI::Result<void> result = uiManager->ApplyRoutingSegmentDefaultToToolUIData (*tabPage->uiData, *currentSegmentDefault);
		DBASSERT (result.IsOk ());
	} else if (ev.GetSource () == &tabPage->crossSectionAreaValueEdit) {
		double crossSectionArea = tabPage->crossSectionAreaValueEdit.GetValue ();

		double diameter = std::sqrt (crossSectionArea / PI) * 2;
		tabPage->circumferenceValueEdit.SetValue (diameter * PI);
		tabPage->diameterEdit.SetValue (diameter);

		currentSegmentDefault->Modify ([&](ACAPI::MEP::RoutingSegmentDefault::Modifier& modifier) {
			modifier.SetCrossSectionWidth (diameter);
			modifier.SetCrossSectionHeight (diameter);
		});
		ACAPI::Result<void> result = uiManager->ApplyRoutingSegmentDefaultToToolUIData (*tabPage->uiData, *currentSegmentDefault);
		DBASSERT (result.IsOk ());
	}
}


void TestSettingsPageObserver::TextEditChanged (const DG::TextEditChangeEvent& ev) 
{
	if (ev.GetSource () == &tabPage->customTextEdit) {
		MEPUserData data;
		BNZeroMemory (&data, sizeof (MEPUserData));

		GS::UniString buffer = tabPage->customTextEdit.GetText ();
		CHTruncate (buffer.ToCStr (), data.customText, Sizeof32 (data.customText));
		data.customValue = tabPage->customValueEdit.GetValue ();

		SetUserDataToDefault (&data);
	}
}

// --- TestSettingsPanel -------------------------------------------------------

TestSettingsPanel::TestSettingsPanel (Int32 refCon) :
	TBUI::APIToolUIPanel	(refCon),
	observer				(nullptr)
{
}


TestSettingsPanel::~TestSettingsPanel (void)
{

}


bool	TestSettingsPanel::CreatePage (const DG::TabControl& tabControl, TBUI::IAPIToolUIData* data, DG::TabPage** tabPage)
{
	this->tabPage = new TestSettingsPage (tabControl, data);
	*tabPage = this->tabPage;

	observer = new TestSettingsPageObserver (dynamic_cast<TestSettingsPage*> (this->tabPage));

	return true;
}


void	TestSettingsPanel::DestroyPage (void)
{
	delete observer;		// this must be first
	observer = nullptr;

	delete tabPage;			// this must be after destroying the observers
	tabPage = nullptr;
}

}		// namespace MEPExample
