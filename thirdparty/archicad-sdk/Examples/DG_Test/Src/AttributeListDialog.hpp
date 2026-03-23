// *****************************************************************************
// Header file for the AttributeList Example Dialog in DG Test Add-On
// *****************************************************************************

#ifndef ATTRIBUTELISTDIALOG_H
#define ATTRIBUTELISTDIALOG_H

#pragma once

#include	"DGModule.hpp"
#include	"UCModule.hpp"
#include	"ACAPI/UI/AttributeList.hpp"


#define ATTRIBUTE_LIST_DIALOG_RESID			32590

// --- AttributeListDialog -----------------------------------------------------

class AttributeListDialog: public DG::ModalDialog,
						   public DG::PanelObserver,
						   public DG::ListBoxObserver,
						   public DG::ButtonItemObserver,
						   public ACAPI::UI::AttributeListPenItemObserver,
						   public ACAPI::UI::AttributeListFillItemObserver,
						   public ACAPI::UI::AttributeListCheckItemObserver,
						   public ACAPI::UI::AttributeListMaterialItemObserver,
						   public ACAPI::UI::AttributeListLineTypeItemObserver,
						   public ACAPI::UI::AttributeListTextItemObserver,
						   public ACAPI::UI::AttributeListRealItemObserver,
						   public ACAPI::UI::AttributeListButtonItemObserver,
						   public DG::CompoundItemObserver
{
private:

	enum {
		OKButtonId			= 1,
		AttributeListId		= 2
	};

	DG::Button									okButton;
	ACAPI::UI::AttributeList					attributeList;

	// Groups
	ACAPI::UI::AttributeListGroupItem		firstGroup;
	ACAPI::UI::AttributeListGroupItem		secondGroup;
	ACAPI::UI::AttributeListGroupItem		fillGroup;
	ACAPI::UI::AttributeListGroupItem		compositeGroup;

	// Items
	ACAPI::UI::AttributeListPenItem				penItem;
	ACAPI::UI::AttributeListCheckItem			checkItem;
	ACAPI::UI::AttributeListSelectionItem		selectionItem;
	ACAPI::UI::AttributeListMaterialItem		materialItem;
	ACAPI::UI::AttributeListLineTypeItem		lineTypeItem;
	ACAPI::UI::AttributeListTextItem			textItem;
	ACAPI::UI::AttributeListAngleItem			angleItem;
	ACAPI::UI::AttributeListButtonItem			buttonItem;
	ACAPI::UI::AttributeListSeparatorItem		separatorItem;

	ACAPI::UI::AttributeListFillItem			fillItem;
	ACAPI::UI::AttributeListPenItem				fillBackgroundPenItem;
	ACAPI::UI::AttributeListPenItem				fillForegroundPenItem;
	ACAPI::UI::AttributeListPenItem				fillContourPenItem;

	ACAPI::UI::AttributeListCompositeItem		compositeItem;
	ACAPI::UI::AttributeListPenItem				compositeBackgroundPenItem;
	ACAPI::UI::AttributeListPenItem				compositeForegroundPenItem;
	ACAPI::UI::AttributeListPenItem				compositeContourPenItem;
	ACAPI::UI::AttributeListPenItem				compositeSeparatorPenItem;



	virtual void	PanelResized (const DG::PanelResizeEvent& ev) override;

	virtual void	ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev) override;

	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;

	virtual void	PenChanged			(const ACAPI::UI::AttributeListPenItem& source) override;
	virtual void	FillChanged			(const ACAPI::UI::AttributeListFillItem& source) override;
	virtual void	CheckChanged		(const ACAPI::UI::AttributeListCheckItem& source) override;
	virtual void	SelectionChanged	(const ACAPI::UI::AttributeListSelectionItem& source) override;
	virtual void	MaterialChanged		(const ACAPI::UI::AttributeListMaterialItem& source) override;
	virtual void	LineTypeChanged		(const ACAPI::UI::AttributeListLineTypeItem& source) override;
	virtual void	TextChanged			(const ACAPI::UI::AttributeListTextItem& source) override;
	virtual void	ValueChanged		(const ACAPI::UI::AttributeListRealItem& source) override;
	virtual void	ButtonClicked		(const ACAPI::UI::AttributeListButtonItem& source) override;

public:

	AttributeListDialog ();
	~AttributeListDialog ();
};

#endif // ATTRIBUTELISTDIALOG_H
