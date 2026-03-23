#include "Property_Visibility_Dialog.hpp"

PropertyVisibilityDialog::PropertyVisibilityDialog (GSResModule dialResModule, const VisibilityData& propertyVisibilityData) :
	DG::ModalDialog (dialResModule, PropertyVisibilityDialogResID, dialResModule),
	okButton (GetReference (), OkButtonResID),
	cancelButton (GetReference (), CancelButtonResID),
	enabledCheckBox (GetReference (), EnabledCheckBoxID),
	propertyTreeView (GetReference (), TreeViewResID),
	propertyVisibilityData (propertyVisibilityData)
{
	Attach (*this);
	AttachToAllItems (*this);
}

const VisibilityData& PropertyVisibilityDialog::GetPropertyVisibilityData () const
{
	return propertyVisibilityData;
}

void PropertyVisibilityDialog::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
	InitEnabledCheckBox ();
	InitPropertyTreeView ();
}

void PropertyVisibilityDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		PostCloseRequest (Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (Cancel);
	} else {
		DBBREAK ();
	}
}

void PropertyVisibilityDialog::CheckItemChanged (const DG::CheckItemChangeEvent& ev)
{
	if (ev.GetSource () == &enabledCheckBox) {
		propertyVisibilityData.SetEnabled (enabledCheckBox.IsChecked ());
		propertyTreeView.SetStatus (propertyVisibilityData.IsEnabled ());
	}
}

void PropertyVisibilityDialog::TreeViewItemClicked (const DG::TreeViewItemClickEvent& ev, bool* /*denySelectionChange*/)
{
	if (ev.GetSource () == &propertyTreeView) {
		if (ev.GetPartId () == DG_TVPI_NORMALICON) {
			Int32 itemIndex = ev.GetTreeItem ();
			if (treeIndexToPropertyGuid.ContainsKey (itemIndex)) {
				GS::Guid guid = treeIndexToPropertyGuid[itemIndex];
				if (propertyVisibilityData.IsItemVisible (guid)) {
					propertyVisibilityData.HideItem (guid);
				} else {
					propertyVisibilityData.ShowItem (guid);
				}
				IconResourceID iconResID = propertyVisibilityData.IsItemVisible (guid) ? OpenEyeResID : ClosedEyeResID;
				propertyTreeView.SetItemNormalIcon (itemIndex, DG::Icon (ACAPI_GetOwnResModule (), (short) iconResID));
			}
		}
	}
}

void PropertyVisibilityDialog::InitEnabledCheckBox ()
{
	if (propertyVisibilityData.IsEnabled ()) {
		enabledCheckBox.Check ();
	} else {
		enabledCheckBox.Uncheck ();
	}
}

void PropertyVisibilityDialog::InitPropertyTreeView ()
{
	GS::Array<API_PropertyGroup> groups;
	GSErrCode err = ACAPI_Property_GetPropertyGroups (groups);
	if (DBVERIFY (err == NoError)) {
		for (UIndex groupIndex = 0; groupIndex < groups.GetSize (); ++groupIndex) {
			const API_PropertyGroup& group = groups[groupIndex];
			if (group.groupType != API_PropertyCustomGroupType) {
				continue;
			}
			Int32 treeGroupIndex = AddGroupToTreeView (DG::TreeView::RootItem, group);
			GS::Array<API_PropertyDefinition> definitions;
			ACAPI_Property_GetPropertyDefinitions (group.guid, definitions);
			for (UIndex definitionIndex = 0; definitionIndex < definitions.GetSize (); ++definitionIndex) {
				const API_PropertyDefinition& definition = definitions[definitionIndex];
				if (definition.definitionType != API_PropertyCustomDefinitionType) {
					continue;
				}
				AddItemToTreeView (treeGroupIndex, definition);
			}
		}
	}
	propertyTreeView.Redraw ();
	propertyTreeView.SetStatus (propertyVisibilityData.IsEnabled ());
}

Int32 PropertyVisibilityDialog::AddTreeViewItem (Int32 parentIndex, const GS::UniString& text, const DG::Icon& icon)
{
	Int32 itemIndex = propertyTreeView.AppendItem (parentIndex);
	propertyTreeView.SetItemText (itemIndex, text);
	propertyTreeView.SetItemNormalIcon (itemIndex, icon);
	return itemIndex;
}

Int32 PropertyVisibilityDialog::AddGroupToTreeView (Int32 parentIndex, const API_PropertyGroup& group)
{
	return AddTreeViewItem (parentIndex, group.name, DG::Icon (ACAPI_GetOwnResModule (), (short) PropertyGroupResID));
}

Int32 PropertyVisibilityDialog::AddItemToTreeView (Int32 parentIndex, const API_PropertyDefinition& definition)
{
	GS::Guid definitionGuid = APIGuid2GSGuid (definition.guid);
	IconResourceID iconResID = propertyVisibilityData.IsItemVisible (definitionGuid) ? OpenEyeResID : ClosedEyeResID;
	Int32 treeIndex = AddTreeViewItem (parentIndex, definition.name, DG::Icon (ACAPI_GetOwnResModule (), (short) iconResID));
	treeIndexToPropertyGuid.Add (treeIndex, definitionGuid);
	return treeIndex;
}
