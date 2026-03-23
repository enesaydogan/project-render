// *****************************************************************************
// Source code for the SplitButton Dialog in DG Test Add-On
// *****************************************************************************


// ---------------------------------- Includes ---------------------------------

#include	"SplitButtonDialog.hpp"
#include	"DGTestResIDs.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h


// --- Class definition: FontPopUpDialog -------------------------------------------------

SplitButtonDialog::SplitButtonDialog () :
	DG::ModalDialog (ACAPI_GetOwnResModule (), DGTEST_SPLITBUTTONDIALOG_RESID, ACAPI_GetOwnResModule ()),
	closeButton (GetReference (), CloseButtonID),
	leftRadioButton (GetReference (), LeftRadioButtonID),
	centerRadioButton (GetReference (), CenterRadioButtonID),
	rightRadioButton (GetReference (), RightRadioButtonID),
	splitButton (GetReference (), SplitButtonID)
{
	Attach (*this);
	closeButton.Attach (*this);
	leftRadioButton.Attach (*this);
	centerRadioButton.Attach (*this);
	rightRadioButton.Attach (*this);
	splitButton.Attach (*this);

	splitButton.SetButtonAlignment (DG::SplitButton::CenterAligned);
	splitButton.SetFontSize (DG::Font::Large);
	splitButton.SetFontStyle (DG::Font::Bold);
	centerRadioButton.Select ();

	splitButton.AppendItem ("Icon & Text", DG::Icon (ACAPI_GetOwnResModule (), 27102));
	splitButton.AppendSeparator ();
	splitButton.AppendItem ("Only Icon", DG::Icon (ACAPI_GetOwnResModule (), 27102));
	splitButton.AppendItem ("Only Text", DG::Icon ());

}

SplitButtonDialog::~SplitButtonDialog ()
{
	Detach (*this);
	splitButton.Detach (*this);
	closeButton.Detach (*this);
	leftRadioButton.Detach (*this);
	centerRadioButton.Detach (*this);
	rightRadioButton.Detach (*this);
}


void SplitButtonDialog::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
}


void SplitButtonDialog::SplitButtonPopupChanged (const DG::SplitButtonChangeEvent& ev)
{
	if (ev.GetSource () == &splitButton) {
		UIndex itemIndex = splitButton.GetSelectedItem ();
		switch (itemIndex) {
			case 1:
				splitButton.SetText (splitButton.GetItemText (1));
				splitButton.SetIcon (splitButton.GetItemIcon (1));
				break;

			case 3:
				splitButton.SetText ("");
				splitButton.SetIcon (splitButton.GetItemIcon (3));
				break;

			case 4:
				splitButton.SetText (splitButton.GetItemText (4));
				splitButton.SetIcon (DG::Icon ());
				break;

			default:
				break;
		}
	}
}

void	SplitButtonDialog::RadioItemChanged (const DG::RadioItemChangeEvent& ev)
{
	if (ev.GetSource () == &leftRadioButton) {
		splitButton.SetButtonAlignment (DG::SplitButton::LeftAligned);
		splitButton.SetFontStyle (DG::Font::Plain);
		splitButton.SetFontSize (DG::Font::ExtraSmall);
	} else if (ev.GetSource () == &centerRadioButton) {
		splitButton.SetButtonAlignment (DG::SplitButton::CenterAligned);
		splitButton.SetFontSize (DG::Font::Large);
		splitButton.SetFontStyle (DG::Font::Bold);
	} else if (ev.GetSource () == &rightRadioButton) {
		splitButton.SetButtonAlignment (DG::SplitButton::RightAligned);
		splitButton.SetFontSize (DG::Font::Small);
		splitButton.SetFontStyle (DG::Font::Italic);
	}
}


void SplitButtonDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &closeButton)
		PostCloseRequest (Cancel);
}