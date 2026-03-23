#include "Classification_Visibility_Dialog.hpp"

ClassificationVisibilityDialog::ClassificationVisibilityDialog (GSResModule dialResModule, const VisibilityData& classificationVisibilityData) :
	DG::ModalDialog (dialResModule, ClassificationVisibilityDialogResID, dialResModule),
	okButton (GetReference (), OkButtonResID),
	cancelButton (GetReference (), CancelButtonResID),
	enabledCheckBox (GetReference (), EnabledCheckBoxID),
	classificationTreeView (GetReference (), TreeViewResID),
	classificationVisibilityData (classificationVisibilityData)
{
	Attach (*this);
	AttachToAllItems (*this);
}

const VisibilityData& ClassificationVisibilityDialog::GetClassificationVisibilityData () const
{
	return classificationVisibilityData;
}

void ClassificationVisibilityDialog::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
	InitEnabledCheckBox ();
	InitClassificationTreeView ();
}

void ClassificationVisibilityDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (Cancel);
	} else {
		DBBREAK ();
	}
}

void ClassificationVisibilityDialog::CheckItemChanged (const DG::CheckItemChangeEvent& ev)
{
	if (ev.GetSource () == &enabledCheckBox) {
		classificationVisibilityData.SetEnabled (enabledCheckBox.IsChecked ());
		classificationTreeView.SetStatus (classificationVisibilityData.IsEnabled ());
	}
}

void ClassificationVisibilityDialog::TreeViewItemClicked (const DG::TreeViewItemClickEvent& ev, bool* /*denySelectionChange*/)
{
	if (ev.GetSource () == &classificationTreeView) {
		if (ev.GetPartId () == DG_TVPI_NORMALICON) {
			Int32 itemIndex = ev.GetTreeItem ();
			if (treeIndexToClassificationGuid.ContainsKey (itemIndex)) {
				GS::Guid guid = treeIndexToClassificationGuid[itemIndex];
				if (classificationVisibilityData.IsItemVisible (guid)) {
					classificationVisibilityData.HideItem (guid);
				} else {
					classificationVisibilityData.ShowItem (guid);
				}
				IconResourceID iconResID = classificationVisibilityData.IsItemVisible (guid) ? OpenEyeResID : ClosedEyeResID;
				classificationTreeView.SetItemNormalIcon (itemIndex, DG::Icon (ACAPI_GetOwnResModule (), (short) iconResID));
			}
		}
	}
}

void ClassificationVisibilityDialog::InitEnabledCheckBox ()
{
	if (classificationVisibilityData.IsEnabled ()) {
		enabledCheckBox.Check ();
	} else {
		enabledCheckBox.Uncheck ();
	}
}


void ClassificationVisibilityDialog::InitClassificationTreeView ()
{
	GS::Array<API_ClassificationSystem> systems;
	ACAPI_Classification_GetClassificationSystems (systems);
	for (API_ClassificationSystem& system : systems) {
		Int32 systemIndex = AddSystemToTreeView (DG::TreeView::RootItem, system);

		GS::Array<API_ClassificationItem> items;
		ACAPI_Classification_GetClassificationSystemRootItems (system.guid, items);
		for (API_ClassificationItem& item : items) {
			Int32 index = AddItemToTreeView (systemIndex, item);

			std::function<void (API_ClassificationItem item, Int32 parentIndex)> processItemRecursively = [&] (API_ClassificationItem item, Int32 parentIndex) {
				GS::Array<API_ClassificationItem> children;
				ACAPI_Classification_GetClassificationItemChildren (item.guid, children);
				for (API_ClassificationItem& child : children) {
					Int32 index = AddItemToTreeView (parentIndex, child);
					processItemRecursively (child, index);
				}
			};

			processItemRecursively (item, index);
		}
	}

	classificationTreeView.Redraw ();
	classificationTreeView.SetStatus (classificationVisibilityData.IsEnabled ());
}

Int32 ClassificationVisibilityDialog::AddTreeViewItem (Int32 parentIndex, const GS::UniString& text, const DG::Icon& icon)
{
	Int32 itemIndex = classificationTreeView.AppendItem (parentIndex);
	classificationTreeView.SetItemText (itemIndex, text);
	classificationTreeView.SetItemNormalIcon (itemIndex, icon);
	return itemIndex;
}

Int32 ClassificationVisibilityDialog::AddSystemToTreeView (Int32 parentIndex, const API_ClassificationSystem& system)
{
	return AddTreeViewItem (parentIndex, system.name, DG::Icon (ACAPI_GetOwnResModule (), (short) ClassificationSystemResID));
}

Int32 ClassificationVisibilityDialog::AddItemToTreeView (Int32 parentIndex, const API_ClassificationItem& item)
{
	GS::Guid itemGuid = APIGuid2GSGuid (item.guid);
	IconResourceID iconResID = classificationVisibilityData.IsItemVisible (itemGuid) ? OpenEyeResID : ClosedEyeResID;
	GS::UniString displayString;
	if (item.name.IsEmpty ()) {
		displayString = item.id;
	} else {
		displayString = item.id + " " + item.name;
	}
	Int32 treeIndex = AddTreeViewItem (parentIndex, displayString, DG::Icon (ACAPI_GetOwnResModule (), (short) iconResID));
	treeIndexToClassificationGuid.Add (treeIndex, itemGuid);
	return treeIndex;
}
