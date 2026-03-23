// *****************************************************************************
// Header file for the SplitButton Dialog in DG Test Add-On
// *****************************************************************************

#ifndef SPLITBUTTON_HPP
#define SPLITBUTTON_HPP

// ---------------------------------- Includes ---------------------------------

#include	"DGModule.hpp"

// --- Class declaration: SplitButtonDialog -------------------------------------------------

class SplitButtonDialog :	public DG::ModalDialog,
							public DG::SplitButtonObserver,
							public DG::PanelObserver,
							public DG::RadioItemObserver {
protected:
	enum {
		CloseButtonID = 1,
		LeftRadioButtonID = 3,
		CenterRadioButtonID = 4,
		RightRadioButtonID = 5,
		SplitButtonID = 6
	};

	DG::Button		closeButton;
	DG::RadioButton	leftRadioButton;
	DG::RadioButton	centerRadioButton;
	DG::RadioButton	rightRadioButton;
	DG::SplitButton	splitButton;

	virtual void	SplitButtonPopupChanged (const DG::SplitButtonChangeEvent& ev) override;
	virtual void	PanelOpened (const DG::PanelOpenEvent& ev)	override;
	virtual void	RadioItemChanged (const DG::RadioItemChangeEvent& ev) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev)	override;

public:
	SplitButtonDialog ();
	~SplitButtonDialog ();

};

#endif // SPLITBUTTON_HPP
