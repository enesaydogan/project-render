// *****************************************************************************
// File:			Classification_Test.cpp
// Description:		Classification_Test add-on test functions
// Project:			APITools/Classification_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

// --- Includes ----------------------------------------------------------------

#include "ACAPinc.h"
#include "Classification_Test.hpp"
#include "Classification_Test_Helpers.hpp"

#include "StringConversion.hpp"

// -----------------------------------------------------------------------------
// Test functions
// -----------------------------------------------------------------------------

static API_Guid AddEmptyClassificationSystem ()
{
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "Classification Test System";
	system.editionVersion = "v1.0";
	system.source = "Source";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();
	if (ACAPI_Classification_GetClassificationSystem (system) != NoError) {
		ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system));
	}
	ASSERT (system.guid != APINULLGuid);

	return system.guid;
}

static API_Guid AddRandomClassification (const API_Guid& systemGuidIn = APINULLGuid)
{
	API_Guid systemGuid = systemGuidIn;
	if (systemGuid == APINULLGuid)
		systemGuid = AddEmptyClassificationSystem ();

	API_ClassificationItem item;
	item.guid = APINULLGuid;
	item.id = ClassificationTestHelpers::GenearteUniqueName ();
	item.name = "Classification Test Item [" + item.id + "]";
	item.description = "Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item, systemGuid, APINULLGuid, APINULLGuid));
	ASSERT (item.guid != APINULLGuid);

	return item.guid;
}

static void AddRandomClassificationForElement (const API_Guid& elemGuid)
{
	API_Guid classificationGuid = AddRandomClassification ();
	ASSERT_NO_ERROR (ACAPI_Element_AddClassificationItem (elemGuid, classificationGuid));
}

static void RemoveAllClassificationsFromItem (const API_Guid& elemGuid)
{
	GS::Array<GS::Pair<API_Guid, API_Guid>> systemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, systemItemPairs));

	for (UIndex i = 0; i < systemItemPairs.GetSize (); ++i) {
		ASSERT_NO_ERROR (ACAPI_Element_RemoveClassificationItem (elemGuid, systemItemPairs[i].second));
	}
}

static void ListVisibleClassificationsOfElem (const API_Guid& elemGuid)
{
	GS::UniString dialogString;

	GS::Array<GS::Pair<API_Guid, API_Guid>>	systemItemPairs;
	if (elemGuid != APINULLGuid) {
		ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, systemItemPairs));
	} else {
		API_ToolBoxItem tboxInfo = {};
		ASSERT_NO_ERROR (ACAPI_Toolbox_GetToolBoxMode (&tboxInfo));
		ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItemsDefault (tboxInfo.type, systemItemPairs));
	}

	for (UInt32 i = 0; i < systemItemPairs.GetSize (); i++) {
		API_ClassificationSystem system;
		system.guid = systemItemPairs[i].first;
		ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystem (system));

		if (ACAPI_Element_IsClassificationItemVisible (elemGuid, systemItemPairs[i].second)) {
			API_ClassificationItem item;
			item.guid = systemItemPairs[i].second;
			ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItem (item));

			dialogString += system.name + " " + system.editionVersion + " - " + item.id + " " + item.name + "\n\n";
		}
	}


	if (!dialogString.IsEmpty ()) {
		DGAlert (DG_INFORMATION, "Classifications of the selection", "", dialogString, "Ok");
	} else {
		DGAlert (DG_INFORMATION, "Classifications of the selection", "No Classifications found.", "", "Ok");
	}
}

static void GatherAllDescendantOfClassification (const API_ClassificationItem& item, GS::Array<API_ClassificationItem>& allDescendant)
{
	GS::Array<API_ClassificationItem> directChildren;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemChildren (item.guid, directChildren));
	for (UIndex i = 0; i < directChildren.GetSize (); ++i) {
		allDescendant.Push (directChildren[i]);
		GatherAllDescendantOfClassification (directChildren[i], allDescendant);
	}
}

static void ListAllClassificationItems ()
{
	const UInt32 maxClassificationPerDialog = 24;
	const UInt32 maxDialogCount = 4;

	GS::Array<API_ClassificationSystem> systems;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystems (systems));

	GS::Array<GS::UniString> dialogStrings;
	USize currentClassificationCount = 0;
	for (UIndex i = 0; i < systems.GetSize (); ++i) {
		GS::Array<API_ClassificationItem> allItems;
		GS::Array<API_ClassificationItem> rootItems;
		ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystemRootItems (systems[i].guid, rootItems));
		for (UIndex j = 0; j < rootItems.GetSize (); ++j) {
			allItems.Push (rootItems[j]);
			GatherAllDescendantOfClassification (rootItems[j], allItems);
		}

		for (UIndex j = 0; j < allItems.GetSize (); ++j) {
			if (dialogStrings.IsEmpty () || ++currentClassificationCount > maxClassificationPerDialog) {
				if (dialogStrings.GetSize () == maxDialogCount) {
					break;
				}
				dialogStrings.Push (GS::UniString());
				currentClassificationCount = 0;
			}
			dialogStrings.GetLast () += systems[i].name + " " + systems[i].editionVersion + " - " + allItems[j].id + " " + allItems[j].name + "\n\n";
		}
	}

	if (!dialogStrings.IsEmpty ()) {
		for (UIndex i = 0; i < dialogStrings.GetSize (); ++i) {
			DGAlert (DG_INFORMATION, "Classifications of the selection [" + GS::ValueToUniString (i+1) + "/" + GS::ValueToUniString (dialogStrings.GetSize ()) + "]", "", dialogStrings[i], "Ok");
		}
		if (dialogStrings.GetSize () == maxDialogCount && currentClassificationCount == maxClassificationPerDialog) {
			DGAlert (DG_INFORMATION, "Classifications of the selection", "", "Too many classifications, please use the classification manager instead!", "Ok");
		}
	} else {
		DGAlert (DG_INFORMATION, "Classifications of the selection", "No Classifications found.", "", "Ok");
	}
}

static void SimpleTestClassificationSystems ()
{
	// Define a classification system
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "Classification Test System";
	system.editionVersion = "v1.0";
	system.source = "Source";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();

	// If a classification system with the same name and edition version doesn't exist already
	if (ACAPI_Classification_GetClassificationSystem (system) != NoError) {
		// Then create it
		ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system));
	}

	// The create operation generates a guid for the system
	ASSERT (system.guid != APINULLGuid);

	// All of the classification systems in the current plan can be retrieved
	GS::Array<API_ClassificationSystem> allSystem;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystems (allSystem));

	// And it should contain the previously created one
	ASSERT (allSystem.Contains (system));

	// A system's information can be retrieved knowing only its guid
	API_ClassificationSystem system2;
	system2.guid = system.guid;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystem (system2));
	ASSERT (system == system2);

	// A classification system can be changed
	system.name += " Renamed";
	system.editionDate.month = 1;
	ASSERT_NO_ERROR (ACAPI_Classification_ChangeClassificationSystem (system));

	ASSERT (system != system2);
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystem (system2));
	ASSERT (system == system2);

	// Classification systems can also be deleted
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));
}


static void ThoroughTestClassificationSystems ()
{
	// The empty string is an invalid classification name
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "";
	system.editionVersion = "v1.0";
	system.source = "";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();
	ASSERT (ACAPI_Classification_CreateClassificationSystem (system) == APIERR_BADNAME);

	// Whitespace only names aren't accepted either
	system.name = "		\n	";
	ASSERT (ACAPI_Classification_CreateClassificationSystem (system) == APIERR_BADNAME);

	// The names have to be unique
	system.name = "Classification Test System";
	ACAPI_Classification_CreateClassificationSystem (system);	// it probably already exists
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystem (system));	// definitely exists

	API_ClassificationSystem system2 = system;
	system2.guid = APINULLGuid;
	ASSERT (ACAPI_Classification_CreateClassificationSystem (system2) == APIERR_NAMEALREADYUSED);

	// Same name, but different version systems are acceptable though
	system2.editionVersion = "v2.0";
	if (ACAPI_Classification_GetClassificationSystem (system2) == APIERR_BADID) {
		ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system2));
	}
	ASSERT (system.guid != system2.guid);

	// Empty edition version is also an error
	system2.editionVersion = "";
	ASSERT (ACAPI_Classification_CreateClassificationSystem (system2) == APIERR_BADNAME);

	// Name change also requires a valid name and edition version
	ASSERT (ACAPI_Classification_ChangeClassificationSystem (system2) == APIERR_BADNAME);

	// And name change also requires uniqueness
	system2.name = system.name;
	system2.editionVersion = system.editionVersion;
	ASSERT (ACAPI_Classification_ChangeClassificationSystem (system2) == APIERR_NAMEALREADYUSED);

	// Once a system has been deleted, the functions will not recognize it as a valid classification system
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system2.guid));
	ASSERT (ACAPI_Classification_GetClassificationSystem (system) == APIERR_BADID);
	ASSERT (ACAPI_Classification_ChangeClassificationSystem (system) == APIERR_BADID);
	ASSERT (ACAPI_Classification_DeleteClassificationSystem (system.guid) == APIERR_BADID);

	// ACAPI_Classification_GetClassificationSystems won't retrieve deleted systems
	GS::Array<API_ClassificationSystem> allSystem;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystems (allSystem));
	ASSERT (!allSystem.Contains (system) && !allSystem.Contains (system2));
}

static void SimpleTestClassificationItems ()
{
	// Define a classification system
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "Classification Test System";
	system.editionVersion = "v1.0";
	system.source = "Source";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();

	// If there was a classification system with this name and edition version, then delete it first
	if (ACAPI_Classification_GetClassificationSystem (system) == NoError) {
		ACAPI_Classification_DeleteClassificationSystem (system.guid);
	}

	// Create an empty system
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system));

	// Create an item
	API_ClassificationItem item;
	item.name = "Classification Test Item";
	item.id = "TestID";
	item.description = "Meaningless Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item, system.guid, APINULLGuid /*parentItem*/, APINULLGuid /*nextItem*/));

	// Create an item before it
	API_ClassificationItem sibling;
	sibling.name = "Classification Test Item's sibling";
	sibling.id = "TestID sibling";
	sibling.description = "Meaningless Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (sibling, system.guid, APINULLGuid /*parentItem*/, item.guid));

	// Create a child item
	API_ClassificationItem child;
	child.name = "Classification Test Item";
	child.id = "TestID child";
	child.description = "Meaningless Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (child, system.guid, item.guid, APINULLGuid /*nextItem*/));

	// The root items don't have a parent, but the child item does
	API_ClassificationItem parent;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemParent (item.guid, parent));
	ASSERT (parent.guid == APINULLGuid);
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemParent (sibling.guid, parent));
	ASSERT (parent.guid == APINULLGuid);
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemParent (child.guid, parent));
	ASSERT (parent == item);

	// only the item has a child
	GS::Array<API_ClassificationItem> children;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemChildren (item.guid, children));
	ASSERT (children.GetSize () == 1 && children[0] == child);
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemChildren (sibling.guid, children));
	ASSERT (children.IsEmpty ());
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemChildren (child.guid, children));
	ASSERT (children.IsEmpty ());

	// Get the system of an item.
	API_ClassificationSystem getSystem;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemSystem (item.guid, getSystem));
	ASSERT (getSystem.guid == system.guid);
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemSystem (sibling.guid, getSystem));
	ASSERT (getSystem.guid == system.guid);
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItemSystem (child.guid, getSystem));
	ASSERT (getSystem.guid == system.guid);

	// The root items of a system can also be queried
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystemRootItems (system.guid, children));
	ASSERT (children.GetSize () == 2 && children[0] == sibling && children[1] == item);

	// Items can be retrieved through their guid
	API_ClassificationItem temp;
	temp.guid = item.guid;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationItem (temp));
	ASSERT (temp == item);

	// Items can be deleted
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationItem (item.guid));
	ASSERT (ACAPI_Classification_DeleteClassificationItem (child.guid) == APIERR_BADID);	// it was already deleted through it's parent
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationItem (sibling.guid));

	// Clean up the system too
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));
}


static void ThoroughTestClassificationItems ()
{
	// Define a classification system
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "Classification Test System";
	system.editionVersion = "v1.0";
	system.source = "Source";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();

	// If there was a classification system with this name and edition version, then delete it first
	if (ACAPI_Classification_GetClassificationSystem (system) == NoError) {
		ACAPI_Classification_DeleteClassificationSystem (system.guid);
	}
	// Create an empty system
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system));

	// The empty string is an invalid classification id
	API_ClassificationItem item;
	item.name = "Classification Test Item";
	item.id = "";
	item.description = "Meaningless Description";
	ASSERT (ACAPI_Classification_CreateClassificationItem (item, system.guid, APINULLGuid, APINULLGuid) == APIERR_BADNAME);

	// Whitespace only ids aren't accepted either
	item.id = "		\n	";
	ASSERT (ACAPI_Classification_CreateClassificationItem (item, system.guid, APINULLGuid, APINULLGuid) == APIERR_BADNAME);

	// The ids have to be unique
	item.id = "TestID";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item, system.guid, APINULLGuid, APINULLGuid));

	API_ClassificationItem item2 = item;
	item2.guid = APINULLGuid;
	ASSERT (ACAPI_Classification_CreateClassificationItem (item2, system.guid, APINULLGuid, APINULLGuid) == APIERR_NAMEALREADYUSED);

	// The system's guid must not be null
	item2.id = "TestID 2";
	ASSERT (ACAPI_Classification_CreateClassificationItem (item2, APINULLGuid, APINULLGuid, APINULLGuid) == APIERR_BADID);

	// invalid parent / next item result in APIERR_BADPARS
	ASSERT (ACAPI_Classification_CreateClassificationItem (item2, system.guid, item.guid, item.guid) == APIERR_BADPARS);

	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item2, system.guid, item.guid, APINULLGuid));

	// Item change also requires a valid id
	item2.id = "	";
	ASSERT (ACAPI_Classification_ChangeClassificationItem (item2) == APIERR_BADNAME);

	// And name change also requires uniqueness
	item2.id = item.id;
	ASSERT (ACAPI_Classification_ChangeClassificationItem (item2) == APIERR_NAMEALREADYUSED);

	//// Once an item has been deleted, the functions will not recognize it as a valid classification system
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationItem (item.guid));
	ASSERT (ACAPI_Classification_DeleteClassificationItem (item2.guid) == APIERR_BADID); // deleted implicitly through parent
	ASSERT (ACAPI_Classification_GetClassificationItem (item) == APIERR_BADID);
	ASSERT (ACAPI_Classification_ChangeClassificationItem (item) == APIERR_BADID);
	ASSERT (ACAPI_Classification_DeleteClassificationItem (item.guid) == APIERR_BADID);

	GS::Array<API_ClassificationItem> children;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystemRootItems (system.guid, children));
	ASSERT (children.IsEmpty ());

	// clean up
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));
}

static void TestClassificationsOnElem (const API_Guid& elemGuid)
{
	// Define a classification system
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "Classification Test System";
	system.editionVersion = "v1.0";
	system.source = "Source";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();

	// If there was a classification system with this name and edition version, then delete it first
	if (ACAPI_Classification_GetClassificationSystem (system) == NoError) {
		ACAPI_Classification_DeleteClassificationSystem (system.guid);
	}

	// Create an empty system
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system));

	API_Element elem;
	elem.header.guid = elemGuid;
	ASSERT_NO_ERROR (ACAPI_Element_Get (&elem));

	// Create an item
	API_ClassificationItem item;
	item.name = "Classification Test Item";
	item.id = "TestID";
	item.description = "Meaningless Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item, system.guid, APINULLGuid /*parentItem*/, APINULLGuid /*nextItem*/));

	// Classify the element
	ASSERT_NO_ERROR (ACAPI_Element_AddClassificationItem (elemGuid, item.guid));

	// The classification can be retrieved
	GS::Array<GS::Pair<API_Guid, API_Guid>> systemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, systemItemPairs));
	ASSERT (systemItemPairs.Contains (GS::Pair<API_Guid, API_Guid> (system.guid, item.guid)));

	API_ClassificationItem item2;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationInSystem (elemGuid, system.guid, item2));
	ASSERT (item2 == item);

	GS::Array<API_Guid> elemGuids;
	ASSERT_NO_ERROR (ACAPI_Element_GetElementsWithClassification (item.guid, elemGuids));
	ASSERT (elemGuids.Contains (elemGuid));

	// One can remove the classification with ACAPI_Element_RemoveClassificationItem
	ASSERT_NO_ERROR (ACAPI_Element_RemoveClassificationItem (elemGuid, item.guid));

	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, systemItemPairs));
	ASSERT (!systemItemPairs.Contains (GS::Pair<API_Guid, API_Guid> (system.guid, item.guid)));

	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationInSystem (elemGuid, system.guid, item2));
	ASSERT (item2.guid == APINULLGuid);

	// clean up
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));
}

static void TestClassificationsOnElemDefault (const API_ElemType& type = API_ColumnID)
{
	// Define a classification system
	API_ClassificationSystem system;
	system.guid = APINULLGuid;
	system.name = "Classification Test System";
	system.editionVersion = "v1.0";
	system.source = "Source";
	system.editionDate = ClassificationTestHelpers::GetCurrentDate ();

	// If there was a classification system with this name and edition version, then delete it first
	if (ACAPI_Classification_GetClassificationSystem (system) == NoError) {
		ACAPI_Classification_DeleteClassificationSystem (system.guid);
	}

	// Create an empty system
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (system));

	// Create an item
	API_ClassificationItem item;
	item.name = "Classification Test Item";
	item.id = "TestID";
	item.description = "Meaningless Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item, system.guid, APINULLGuid /*parentItem*/, APINULLGuid /*nextItem*/));

	// Classify the element
	ASSERT_NO_ERROR (ACAPI_Element_AddClassificationItemDefault (type, item.guid));

	// The classification can be retrieved
	GS::Array<GS::Pair<API_Guid, API_Guid>> systemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItemsDefault (type, systemItemPairs));
	ASSERT (systemItemPairs.Contains (GS::Pair<API_Guid, API_Guid> (system.guid, item.guid)));

	API_ClassificationItem item2;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationInSystemDefault (type, system.guid, item2));
	ASSERT (item2 == item);

	// One can remove the classification with ACAPI_Element_RemoveClassificationItemDefault
	ASSERT_NO_ERROR (ACAPI_Element_RemoveClassificationItemDefault (type, item.guid));

	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItemsDefault (type, systemItemPairs));
	ASSERT (!systemItemPairs.Contains (GS::Pair<API_Guid, API_Guid> (system.guid, item.guid)));

	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationInSystemDefault (type, system.guid, item2));
	ASSERT (item2.guid == APINULLGuid);

	// clean up
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));
}


static void TestClassificationsOnAttribute ()
{
	// Create new test building material
	API_Attribute attrib = {};

	attrib.header.typeID = API_BuildingMaterialID;
	strcpy (attrib.header.name, "Test Building Material");
	ASSERT_NO_ERROR (ACAPI_Attribute_Create (&attrib, nullptr));

	// Create classification system and classifications
	API_Guid systemGuid = AddEmptyClassificationSystem ();
	API_Guid firstClassificationGuid = AddRandomClassification (systemGuid);
	API_Guid secondClassificationGuid = AddRandomClassification (systemGuid);

	// Classify building material
	ASSERT_NO_ERROR (ACAPI_Attribute_AddClassificationItem (attrib.header, firstClassificationGuid));

	// Check the classification of the building material
	GS::Array<GS::Pair<API_Guid, API_Guid>> systemItemPairs;
	API_ClassificationItem					item;

	ASSERT_NO_ERROR (ACAPI_Attribute_GetClassificationItems (attrib.header, systemItemPairs));
	ASSERT (systemItemPairs.GetSize () == 1);

	ASSERT_NO_ERROR (ACAPI_Attribute_GetClassificationInSystem (attrib.header, systemItemPairs[0].first, item));
	ASSERT (item.guid == firstClassificationGuid);

	// Change Classification
	ASSERT_NO_ERROR (ACAPI_Attribute_AddClassificationItem (attrib.header, secondClassificationGuid));
	ASSERT_NO_ERROR (ACAPI_Attribute_GetClassificationInSystem (attrib.header, systemItemPairs [0].first, item));
	ASSERT (item.guid == secondClassificationGuid);

	// Is it visible?
	ASSERT (ACAPI_Attribute_IsClassificationItemVisible (attrib.header, secondClassificationGuid));
	ASSERT (ACAPI_Attribute_IsClassificationItemVisible (attrib.header, firstClassificationGuid));

	// Remove a classification
	ASSERT_NO_ERROR (ACAPI_Attribute_RemoveClassificationItem (attrib.header, secondClassificationGuid));
	ASSERT_NO_ERROR (ACAPI_Attribute_GetClassificationItems (attrib.header, systemItemPairs));
	ASSERT (systemItemPairs.GetSize () == 0);
	
	// Cleanup - Delete building material
	ASSERT_NO_ERROR (ACAPI_Attribute_Delete (attrib.header));

	// Cleanup - Delete classification system
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (systemGuid));

	DGAlert (DG_INFORMATION, "Test success", "Tests executed successfully.", "", "Ok");
}


static void TestXMLImport ()
{
	// Exported from ARCHICAD
	const char* xmlString =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>"
		"<BuildingInformation>"
		"	<Classification>"
		"		<System>"
		"			<Name>Classification System imported from the Classification_Test add-on.</Name>"
		"			<EditionVersion>20</EditionVersion>"
		"			<EditionDate>"
		"				<Year>2000</Year>"
		"				<Month>1</Month>"
		"				<Day>1</Day>"
		"			</EditionDate>"
		"			<Description/>"
		"			<Source>Classification_Test addon</Source>"
		"			<Items>"
		"				<Item>"
		"					<ID>Classification Item imported from the Classification_Test add-on.</ID>"
		"					<Name/>"
		"					<Description/>"
		"					<Availability>ModelElement</Availability>"
		"					<Children/>"
		"				</Item>"
		"			</Items>"
		"		</System>"
		"	</Classification>"
		"	<PropertyDefinitionGroups/>"
		"</BuildingInformation>";

	ASSERT_NO_ERROR (ACAPI_Classification_Import (xmlString, API_ReplaceConflictingSystems, API_ReplaceConflictingItems));


	GS::Array<API_ClassificationSystem> systems;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystems (systems));
	API_ClassificationSystem importedSystem;
	for (UIndex i = 0; i < systems.GetSize (); ++i) {
		if (systems[i].name == "Classification System imported from the Classification_Test add-on.") {
			importedSystem = systems[i];
			break;
		}
	}
	ASSERT (importedSystem.guid != APINULLGuid);

	GS::Array<API_ClassificationItem> items;
	ASSERT_NO_ERROR (ACAPI_Classification_GetClassificationSystemRootItems (importedSystem.guid, items));
	API_ClassificationItem importedItem;
	for (UIndex i = 0; i < items.GetSize (); ++i) {
		if (items[i].id == "Classification Item imported from the Classification_Test add-on.") {
			importedItem = items[i];
			break;
		}
	}
	ASSERT (importedItem.guid != APINULLGuid);
}


static void RunClassificationsTestOnSelectedElem (const API_Guid& elemGuid)
{
	SimpleTestClassificationSystems ();
	ThoroughTestClassificationSystems ();
	SimpleTestClassificationItems ();
	ThoroughTestClassificationItems ();
	TestClassificationsOnElem (elemGuid);
	TestClassificationsOnElemDefault ();
	TestXMLImport ();

	DGAlert (DG_INFORMATION, "Test success", "Tests executed successfully.", "", "Ok");
}


static GSErrCode NotificationCallback (const API_NotifyElementType *elemType)
{
	if (elemType != nullptr && elemType->notifID == APINotifyElement_ClassificationChange) {
		DGAlert (DG_INFORMATION, "Classification change", "An element's classifications has been changed.", "", "Ok");
	}
	return NoError;
}


void WatchElement (const API_Guid& elemGuid)
{
	ASSERT_NO_ERROR (ACAPI_Element_AttachObserver (elemGuid));
}


void UnwatchElement (const API_Guid& elemGuid)
{
	ASSERT_NO_ERROR (ACAPI_Element_DetachObserver (elemGuid));
}


// -----------------------------------------------------------------------------
// Add-on entry point definition
// -----------------------------------------------------------------------------
GSErrCode APIMenuCommandProc_Main (const API_MenuParams *menuParams)
{
	if (menuParams->menuItemRef.menuResID == 32500) {
		return ACAPI_CallUndoableCommand ("Classification Test API Function",
			[&] () -> GSErrCode {
				try {
					switch (menuParams->menuItemRef.itemIndex) {
						case  1: ClassificationTestHelpers::CallOnSelectedElem (AddRandomClassificationForElement); break;
						case  2: ClassificationTestHelpers::CallOnSelectedElem (RemoveAllClassificationsFromItem); break;
						case  3: break; // "-"
						case  4: ClassificationTestHelpers::CallOnSelectedElem (ListVisibleClassificationsOfElem, false/*assertIfNoSel*/, false/*onlyEditable*/); break;
						case  5: ListAllClassificationItems (); break;
						case  6: break; // "-"
						case  7: ClassificationTestHelpers::CallOnSelectedElem (WatchElement); break;
						case  8: ClassificationTestHelpers::CallOnSelectedElem (UnwatchElement); break;
						case  9: break; // "-"
						case 10: ClassificationTestHelpers::CallOnSelectedElem (RunClassificationsTestOnSelectedElem); break;
						case 11: break; // "-"
						case 12: TestClassificationsOnAttribute (); break;
						default: break;
					}
					return NoError;
				} catch (const GS::Exception&) {
					return Error;
				}
			}
		);
	}

	return NoError;
}		// APIMenuCommandProc_Main


// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	return ACAPI_MenuItem_RegisterMenu (32500, 32600, MenuCode_UserDef, MenuFlag_Default);
}


// -----------------------------------------------------------------------------
// Called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, APIMenuCommandProc_Main);

	if (err == NoError) {
		err = ACAPI_Element_InstallElementObserver (NotificationCallback);
	}

	return err;
}


// -----------------------------------------------------------------------------
// Called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData	(void)
{
	return NoError;
}
