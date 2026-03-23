#ifndef PANELS_HPP
#define PANELS_HPP

#include "DGModule.hpp"
#include "IAPIAttrUIData.hpp"
#include "APIAttrUIPanel.hpp"
#include "IAPIToolUIData.hpp"
#include "APIToolUIPanel.hpp"

#include "APIdefs_Elements.h"

// MEPAPI
#include "ACAPI/MEPRoutingSegmentUIManager.hpp"
#include "ACAPI/MEPRoutingSegmentDefault.hpp"


namespace MEPExample {

// --- RegData structure -------------------------------------------------------

struct MEPUserData {
	double	customValue;
	char	customText[32];
};

// --- TestSettingsPage --------------------------------------------------------

class TestSettingsPage: public DG::TabPage {

friend class	TestSettingsPageObserver;

private:

	enum {
		DiameterEditId						= 2,
		CustomTextEditId					= 4,
		CustomValueEditId					= 6,
		CircumferenceValueEditValueEditId	= 8,
		CrossSectionAreaValueEditId			= 10,
	};

	DG::MMInchEdit				diameterEdit;
	DG::TextEdit				customTextEdit;
	DG::RealEdit				customValueEdit;
	DG::MMInchEdit				circumferenceValueEdit;
	DG::AreaEdit				crossSectionAreaValueEdit;
	TBUI::IAPIToolUIData* 		uiData;

public:
	 TestSettingsPage (const DG::TabControl& tabControl, TBUI::IAPIToolUIData* puiData);
	~TestSettingsPage (void);
};


// --- TestSettingsPageObserver ------------------------------------------------

class TestSettingsPageObserver:	public  DG::PanelObserver,
								public	DG::TextEditBaseObserver,
								public	DG::RealEditObserver,
								public  DG::PopUpObserver,
								public  DG::CheckItemObserver,
								public  DG::CompoundItemObserver,
								public  TBUI::IAPIToolUIDataObserver
{
private:
	TestSettingsPage*								tabPage;

	Owner<ACAPI::MEP::RoutingSegmentUIManager>	uiManager;
	Owner<ACAPI::MEP::RoutingSegmentDefault>	currentSegmentDefault;

	GSErrCode			GetUserDataFromDefault	(MEPUserData* data);
	GSErrCode			SetUserDataToDefault	(MEPUserData* sdata);

protected:

	virtual void		PanelOpened				(const DG::PanelOpenEvent& ev)			override;
	virtual void		PanelResized			(const DG::PanelResizeEvent& ev)		override;

	virtual	void		APIElementChanged		(const TBUI::APIElemDefaultFieldMask&)	override;
	virtual void		RealEditChanged			(const DG::RealEditChangeEvent&)		override;
	virtual void		TextEditChanged			(const DG::TextEditChangeEvent&)		override;

public:

	explicit TestSettingsPageObserver (TestSettingsPage* testPage);
	~TestSettingsPageObserver (void);

};

// --- Wrapper class -----------------------------------------------------------

class TestSettingsPanel	:	public TBUI::APIToolUIPanel
{
private:
	TestSettingsPageObserver*	observer;

public:
				 TestSettingsPanel (Int32 refCon);
	virtual		~TestSettingsPanel ();

	virtual		bool	CreatePage (const DG::TabControl& tabControl, TBUI::IAPIToolUIData* data, DG::TabPage** tabPage) override;
	virtual		void	DestroyPage (void) override;
};


} // namespace MEPExample

#endif
