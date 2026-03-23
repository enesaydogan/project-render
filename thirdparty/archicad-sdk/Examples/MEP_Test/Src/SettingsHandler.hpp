#if !defined SETTINGS_HANDLER_HPP
#define SETTINGS_HANDLER_HPP


#include "IAPIToolUIData.hpp"

namespace MEPExample {

class TestSettingsHandler : public	TBUI::IAPIToolUIDataObserver
{
public:
	TestSettingsHandler (TBUI::IAPIToolUIData* tuiData);
	~TestSettingsHandler ();

	virtual	void APIElementChanged (const TBUI::APIElemDefaultFieldMask& fieldMask) override;

private:
	TBUI::IAPIToolUIData* uiData;
};

} // namespace MEPExample

#endif