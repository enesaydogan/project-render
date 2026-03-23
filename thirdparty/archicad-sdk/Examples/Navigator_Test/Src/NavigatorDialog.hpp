// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#ifndef NAVIGATOR_DIALOG_HPP
#define NAVIGATOR_DIALOG_HPP

// ---------------------------------- Includes ---------------------------------

#include "DGModule.hpp"
#include "AddOnNavigatorViewPointData.hpp"
#include "APIdefs_Elements.h"
#include "APIdefs_TeamworkReserveInterface.h"


class NavigatorDialog : public DG::ModalDialog,
								public DG::CompoundItemObserver,
								public DG::PanelObserver,
								public DG::ButtonItemObserver,
								public DG::ListBoxObserver,
								public DG::TextEditBaseObserver,
								public DG::PopUpObserver,
								public DG::IntEditObserver
{
public:

enum TWCloseReason {
	TWCloseReason_NoTWOperation,
	TWCloseReason_Reserve,
	TWCloseReason_Release,
	TWCloseReason_Grant,
	TWCloseReason_Request
};

private:
	static const short NavigatorDialogId;
	static const short NavigatorTeamworkTabPageId;
	static const short NavigatorTeamworkTextId;

	enum {
		OkButtonId						= 1,
		CancelButtonId					= 2,
		NavigatorItemOnTabIdEditId		= 3,
		NavigatorItemOnTabNameEditId	= 4,
		NavigatorItemListId				= 5,
		NewButtonId						= 6,
		DeleteButtonId					= 7,
		ElementTypeTextId				= 8,
		ElementTypePopupId				= 9,
		NumberOfElementsTextId			= 10,
		NumberOfElementsEditId			= 11,
		SeparatorId						= 12,
		TeamworkTabId					= 13
	};

	enum {
		IdTabItem	= 1,
		NameTabItem	= 2
	};

	DG::Button				okButton;
	DG::Button				cancelButton;
	DG::TextEdit			navigatorItemOnTabIdEdit;
	DG::TextEdit			navigatorItemOnTabNameEdit;
	DG::SingleSelListBox	navigatorItemList;
	DG::Button				newButton;
	DG::Button				deleteButton;
	DG::LeftText			elementTypeText;
	DG::PopUp				elementTypePopup;
	DG::LeftText			numberOfElementsText;
	DG::IntEdit				numberOfElementsEdit;
	DG::Separator			separator;
	DG::SimpleTab			teamworkTab;

	const GSResModule		dialogResModule;
	bool					isTWTabInitialized;
	TWCloseReason			twCloseReason;
	short					grantUserId;

	GS::Array<AddOnNavigatorViewPointData>	navigatorViewPointList;
	GS::Array<AddOnNavigatorViewPointData>	origNavigatorViewPointList;
	GS::Array<API_Guid>						clientCreatedViewPointGuids;
	API_Guid								prevSelectedVPGuid;

public:
	NavigatorDialog (GSResModule dialModule, GSResModule dialIconModule, const GS::Array<AddOnNavigatorViewPointData>& _navigatorViewPointList, API_Guid selectedItemGuid);
	virtual ~NavigatorDialog ();

	void			TeamWorkPanelParentUIRefreshNeeded (API_ReservationInterfaceStatus reservationStatus);
	void			TeamWorkPanelParentDataSaveNeeded (API_ReservationInterfaceStatus status);

	TWCloseReason	GetTWCloseReason (void) const;
	GSErrCode		HandlePanelAccepted (bool* panelClosedAccepted, TWCloseReason twCloseReason);
	void			HandleClientCreatedViewPointsOnCancel (void);

	API_Guid		GetSelectedViewPointGuid (void);

protected:
	virtual void	PanelResized			(const DG::PanelResizeEvent& ev) override;
	virtual void	PanelResizeExited		(const DG::PanelResizeEvent& ev) override;
	virtual void	ListBoxSelectionChanged	(const DG::ListBoxSelectionEvent& ev) override;
	virtual void	TextEditChanged			(const DG::TextEditChangeEvent& ev) override;
	virtual void	ButtonClicked			(const DG::ButtonClickEvent& ev) override;
	virtual void	PopUpChanged			(const DG::PopUpChangeEvent& ev) override;
	virtual void	IntEditChanged			(const DG::IntEditChangeEvent& ev) override;

private:
	void			InitTWTab (API_Guid guid);
	void			DeleteTWTab (API_Guid guid);
	void			RefreshTWTab (API_Guid oldGuid, API_Guid newGuid);

	GS::Array<AddOnNavigatorViewPointData>&	GetNavigatorViewPoints (void) { return navigatorViewPointList; }
	short									GetGrantUserId (void) const;

	void			InitListAndOnTabItems (void);
	void			FillList (GS::UniString viewPointToSelect = GS::UniString ());
	short			SelectGivenViewPointGuid (API_Guid selectedViewPointGuid);
	bool			ShowOnTabListItems (void);

	void			UpdatePopups (void);
	API_ElemTypeID	PopupItemToElemType (short popupItem);
	short			ElemTypeToPopupIndex (API_ElemTypeID elemType);
};

#endif
