// *****************************************************************************
// Header file for the Panel Test Add-On
// *****************************************************************************

#if !defined SETTINGS_HANDLER_HPP
#define SETTINGS_HANDLER_HPP

#pragma once


// --- Includes ----------------------------------------------------------------


// --- Class declarations ------------------------------------------------------

#include "IAPIToolUIData.hpp"

namespace PanelTest {

class TestSettingsHandler : public	TBUI::IAPIToolUIDataObserver
{
public:
	TestSettingsHandler (TBUI::IAPIToolUIData* tuiData);
	~TestSettingsHandler ();

	virtual	void APIElementChanged (const TBUI::APIElemDefaultFieldMask& fieldMask) override;

private:
	TBUI::IAPIToolUIData* uiData;
};

} // namespace PanelTest


#endif