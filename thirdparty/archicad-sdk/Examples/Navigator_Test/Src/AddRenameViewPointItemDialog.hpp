// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#if !defined (ADDRENAMEVIEWPOINTITEMDIALOG_HPP)
#define ADDRENAMEVIEWPOINTITEMDIALOG_HPP


// ---------------------------------- Includes ---------------------------------

#include "DGModule.hpp"
#include "Resources.hpp"


// ---------------------------------- Declarations -----------------------------

class AddRenameViewPointItemDialog : public DG::ModalDialog,
									 public DG::PanelObserver,
									 public DG::ButtonItemObserver
{
public:
	enum DialogType {
		NewItemDialogType		= 0,
		RenameItemDialogType	= 1
	};


private:
	static const short NavigatorAPI_NewItemDialogId;
	static const short NavigatorAPI_RenameItemDialogId;

	enum {
		OkButtonId			= 1,
		CancelButtonId		= 2,
		IDTextEditId		= 4,
		NameTextEditId		= 6
	};

	DG::Button		okButton;
	DG::Button		cancelButton;
	DG::TextEdit	idTextEdit;
	DG::TextEdit	nameTextEdit;

public:
	AddRenameViewPointItemDialog (GSResModule dialModule, DialogType dialogType, GS::UniString oldID = GS::UniString (), GS::UniString oldName = GS::UniString ());
	virtual ~AddRenameViewPointItemDialog ();

	GS::UniString	GetViewPointName (void) const;
	GS::UniString	GetViewPointID (void) const;

protected:
	virtual void	PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;
};

#endif