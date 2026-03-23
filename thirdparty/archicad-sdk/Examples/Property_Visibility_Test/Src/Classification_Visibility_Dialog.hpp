#ifndef CLASSIFICATION_VISIBILITY_DIALOG_HPP
#define CLASSIFICATION_VISIBILITY_DIALOG_HPP

#include "Property_Visibility_Test.hpp"
#include "Visibility_Data.hpp"

#include "DGDialog.hpp"
#include "DGButton.hpp"
#include "DGCheckItem.hpp"
#include "DGTreeView.hpp"

class ClassificationVisibilityDialog : public DG::ModalDialog,
									   public DG::PanelObserver,
									   public DG::ButtonItemObserver,
									   public DG::CheckItemObserver,
									   public DG::TreeViewObserver,
									   public DG::CompoundItemObserver
{
public:
	enum DialogResourceID
	{
		ClassificationVisibilityDialogResID	= 32510,
		OkButtonResID					= 1,
		CancelButtonResID				= 2,
		EnabledCheckBoxID				= 3,
		TreeViewResID					= 4
	};

	enum IconResourceID
	{
		OpenEyeResID				= 32502,
		ClosedEyeResID				= 32503,
		ClassificationSystemResID	= 32504
	};

	ClassificationVisibilityDialog (GSResModule dialResModule, const VisibilityData& classificationVisibilityData);

	const VisibilityData&	GetClassificationVisibilityData () const;

	virtual void	PanelOpened (const DG::PanelOpenEvent& ev) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;
	virtual void	CheckItemChanged (const DG::CheckItemChangeEvent& ev) override;
	virtual void	TreeViewItemClicked (const DG::TreeViewItemClickEvent& ev, bool* denySelectionChange) override;

private:
	void			InitEnabledCheckBox ();
	void			InitClassificationTreeView ();
	Int32			AddTreeViewItem (Int32 parentIndex, const GS::UniString& text, const DG::Icon& icon);
	Int32			AddSystemToTreeView (Int32 parentIndex, const API_ClassificationSystem& system);
	Int32			AddItemToTreeView (Int32 parentIndex, const API_ClassificationItem& item);

	DG::Button						okButton;
	DG::Button						cancelButton;
	DG::CheckBox					enabledCheckBox;
	DG::SingleSelTreeView			classificationTreeView;

	VisibilityData					classificationVisibilityData;
	GS::HashTable<Int32, GS::Guid>	treeIndexToClassificationGuid;
};

#endif
