// *****************************************************************************
// Source code for the AddOnObject Manager Add-On
// *****************************************************************************

#if !defined (EDITCUSTOMERDIALOG_HPP)
#define EDITCUSTOMERDIALOG_HPP
#include "APIEnvir.h"

// ---------------------------------- Includes ---------------------------------

#include	"DGModule.hpp"
#include	"CustomerData.hpp"
#include	"APIdefs_TeamworkReserveInterface.h"


class EditCustomerDialog : public DG::ModalDialog,
						   public DG::CompoundItemObserver,
						   public DG::PanelObserver,
						   public DG::ButtonItemObserver
{
private:
	enum {
		OkButtonId		= 1,
		CancelButtonId	= 2,
		DataTextId		= 3,
		ModifyButtonId	= 4,
		TeamworkTabId	= 5,
		SeparatorId		= 6
	};

	DG::Button					okButton;
	DG::Button					cancelButton;
	DG::LeftText				dataText;
	DG::Button					modifyButton;
	IAddOnData*					addonData;
	DG::SimpleTab				teamworkTab;
	DG::Separator				separator;
	CloseReason					twCloseReason;
	bool						isTWTabInitialized;

public:
	EditCustomerDialog (GSResModule dialModule, short resId, GSResModule dialIconModule, IAddOnData* addonData);
	virtual ~EditCustomerDialog ();

	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;

	void			TeamWorkPanelParentUIRefreshNeeded (API_ReservationInterfaceStatus reservationStatus);
	void			TeamWorkPanelParentDataSaveNeeded (API_ReservationInterfaceStatus status);

	CloseReason		GetTWCloseReason (void) const;

private:
	void			InitTWTab (bool isTeamwork);
	void			DeleteTWTab (void);
	void			SetDataText (void);
};
#endif