#ifndef PROPERTY_VISIBILITY_DIALOG_HPP
#define PROPERTY_VISIBILITY_DIALOG_HPP

#include "Property_Visibility_Test.hpp"
#include "Visibility_Data.hpp"

#include "DGDialog.hpp"
#include "DGButton.hpp"
#include "DGCheckItem.hpp"
#include "DGTreeView.hpp"

class PropertyVisibilityDialog :	public DG::ModalDialog,
									public DG::PanelObserver,
									public DG::ButtonItemObserver,
									public DG::CheckItemObserver,
									public DG::TreeViewObserver,
									public DG::CompoundItemObserver
{
public:
	enum DialogResourceID
	{
		PropertyVisibilityDialogResID	= 32511,
		OkButtonResID					= 1,
		CancelButtonResID				= 2,
		EnabledCheckBoxID				= 3,
		TreeViewResID					= 4
	};

	enum IconResourceID
	{
		PropertyGroupResID	= 32501,
		OpenEyeResID		= 32502,
		ClosedEyeResID		= 32503
	};

	PropertyVisibilityDialog (GSResModule dialResModule, const VisibilityData& propertyVisibilityData);

	const VisibilityData&	GetPropertyVisibilityData () const;

	virtual void	PanelOpened (const DG::PanelOpenEvent& ev) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;
	virtual void	CheckItemChanged (const DG::CheckItemChangeEvent& ev) override;
	virtual void	TreeViewItemClicked (const DG::TreeViewItemClickEvent& ev, bool* denySelectionChange) override;

private:
	void			InitEnabledCheckBox ();
	void			InitPropertyTreeView ();
	Int32			AddTreeViewItem (Int32 parentIndex, const GS::UniString& text, const DG::Icon& icon);
	Int32			AddGroupToTreeView (Int32 parentIndex, const API_PropertyGroup& group);
	Int32			AddItemToTreeView (Int32 parentIndex, const API_PropertyDefinition& definition);

	DG::Button						okButton;
	DG::Button						cancelButton;
	DG::CheckBox					enabledCheckBox;
	DG::SingleSelTreeView			propertyTreeView;

	VisibilityData					propertyVisibilityData;
	GS::HashTable<Int32, GS::Guid>	treeIndexToPropertyGuid;
};

#endif
