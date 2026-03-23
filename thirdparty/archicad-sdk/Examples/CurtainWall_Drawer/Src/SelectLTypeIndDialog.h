// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn SelectLTypeIndDialog class header
//					for managing the line type index dialog
// *****************************************************************************

#ifndef SELECTLTYPEINDDIALOG_HPP
#define SELECTLTYPEINDDIALOG_HPP

#pragma once

#include	"DGModule.hpp"

class SelectLTypeIndDialog : public DG::ModalDialog,
							 public DG::ButtonItemObserver,
							 public DG::CheckItemObserver,
							 public DG::CompoundItemObserver,
							 public DG::PanelObserver
{
private:
	void InitUserControl (GS::Owner<API_AttributePicker>& attributePicker, short controlId);

protected:
	enum Controls {
		OKButtonID = 1,
		CancelButtonID = 2,
		BasicLeftTextID = 3,
		HiddenLeftTextID = 4,
		BasicLineTypePickerID = 5,
		HiddenLineTypePickerID = 6
	};

	DG::Button			okButton;
	DG::Button			cancelButton;
	DG::LeftText		basicLine;
	DG::LeftText		hiddenLine;
	DG::PushCheck		basicLineTypePickerPushCheck;
	DG::PushCheck	    hiddenLineTypePickerPushCheck;

	GS::Owner<API_AttributePicker> basicLineTypePicker;
	GS::Owner<API_AttributePicker> hiddenLineTypePicker;

	virtual void ButtonClicked 		 (const DG::ButtonClickEvent& ev) override;
	virtual void CheckItemChanged 	 (const DG::CheckItemChangeEvent& ev) override;
	virtual	void PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted) override;

public:
	SelectLTypeIndDialog ();
};

#endif