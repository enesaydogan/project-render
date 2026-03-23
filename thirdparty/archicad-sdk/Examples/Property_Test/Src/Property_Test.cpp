// *****************************************************************************
// File:			Property_Test.cpp
// Description:		Property_Test add-on test functions
// Project:			APITools/Property_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

// --- Includes ----------------------------------------------------------------

#include "Property_Test.hpp"
#include "Property_Test_Helpers.hpp"
#include "APIdefs_Properties.h"
#include "PropertyUtils.hpp"

// from DGLib
#include "DGFileDialog.hpp"

// from GSXML
#include <xercesc/dom/DOM.hpp>
#include <xercesc/dom/DOMNamedNodeMap.hpp>

// from GSXMLUtils
#include "GSXMLDOMUtils.hpp"
#include "XMLUtils.h"

// from InputOutput
#include "File.hpp"
#include "FileTypeManager.hpp"

static const GS::UniString XMLTag_BuildingInformation				= "BuildingInformation";
static const GS::UniString XMLTag_BuiltInPropertyDefinitionGroups	= "BuiltInPropertyDefinitionGroups";
static const GS::UniString XMLTag_BuiltInPropertyDefinitionGroup	= "BuiltInPropertyDefinitionGroup";
static const GS::UniString XMLTag_BuiltInPropertyDefinitions		= "BuiltInPropertyDefinitions";
static const GS::UniString XMLTag_BuiltInPropertyDefinition			= "BuiltInPropertyDefinition";
static const GS::UniString XMLTag_Name								= "Name";
static const GS::UniString XMLTag_Guid								= "Guid";


// -----------------------------------------------------------------------------
// Test functions
// -----------------------------------------------------------------------------

/*--------------------------------------------------**
** Tests common use-cases involving property groups **
**--------------------------------------------------*/
static void SimpleTestPropertyGroups ()
{
	// Define a property group
	API_PropertyGroup group = PropertyTestHelpers::CreateExamplePropertyGroup ();

	// Create it
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyGroup (group));

	// The create operation generates a guid for the group
	ASSERT (group.guid != APINULLGuid);

	// If we list all groups
	GS::Array<API_PropertyGroup> groups;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	// Then the result array will contain the newly created one
	ASSERT (groups.Contains (group));

	API_PropertyGroup group2;
	group2.guid = group.guid;

	// We fill an API_PropertyGroup knowing only its guid
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group2));
	ASSERT (group.name == group2.name);
	ASSERT (group.description == group2.description);

	// The name of the group can be changed
	group.name = "Renamed Property_Test Add-On Group - " + PropertyTestHelpers::GenearteUniqueName ();
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyGroup (group));

	ASSERT (group.name != group2.name);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group2));
	ASSERT (group.name == group2.name);
	
	// Also the description of the group can be changed
	group.description = "Property_Test Add-On Group Description";
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyGroup (group));

	ASSERT (group.description != group2.description);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group2));
	ASSERT (group.description == group2.description);

	// Property groups can be deleted
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyGroup (group.guid));

	// Then, retrieval operation results in an error
	ASSERT (ACAPI_Property_GetPropertyGroup (group) == APIERR_BADID);
}


/*-------------------------------------------------------------------------**
**		Tests rather unrealistic use-cases involving property groups	   **
** The purpose of this is to check the error handling of the API functions **
**-------------------------------------------------------------------------*/
static void ThoroughTestPropertyGroups ()
{
	// Create a property group
	API_PropertyGroup group;
	group.guid = APINULLGuid;
	group.name = "Property_Test Add-On Group - "  + PropertyTestHelpers::GenearteUniqueName ();
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyGroup (group));

	// Try to create another group with the same name
	API_PropertyGroup group2;
	group2.guid = APINULLGuid;
	group2.name = group.name;
	ASSERT (ACAPI_Property_CreatePropertyGroup (group2) == APIERR_NAMEALREADYUSED);
	ASSERT (group2.guid == APINULLGuid);

	// The created group can be retrieved
	GS::Array<API_PropertyGroup> groups;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (groups.Contains (group));
	API_PropertyGroup temp = group;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group));
	ASSERT (group == temp);

	// Can't get a group using an APINULLGuid id.
	ASSERT (ACAPI_Property_GetPropertyGroup (group2) == APIERR_BADID);

	// "Changing" a group without modifying fields does not result in an error
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyGroup (group));
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (groups.Contains (group));
	temp = group;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group));
	ASSERT (group == temp);

	// We can rename a group
	group.name += " Renamed";
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyGroup (group));
	temp = group;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group));
	ASSERT (group == temp);
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (groups.Contains (group));

	// Now we can create group2, as group was renamed
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyGroup (group2));
	temp = group2;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group2));
	ASSERT (group2 == temp);
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (groups.Contains (group2));

	// Deleting group won't remove group2
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyGroup (group.guid));
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group2));
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (groups.Contains (group2));

	// The deleted group can't be retrieved
	ASSERT (ACAPI_Property_GetPropertyGroup (group) == APIERR_BADID);
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (!groups.Contains (group));

	// Deleting the same group twice results in an error
	ASSERT (ACAPI_Property_DeletePropertyGroup (group.guid) == APIERR_BADID);

	// The deleted group still can't be retrieved
	ASSERT (ACAPI_Property_GetPropertyGroup (group) == APIERR_BADID);
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (!groups.Contains (group));

	// The not deleted group is still reachable
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group2));
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (groups.Contains (group2));

	// Delete group2 just to clean up
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyGroup (group2.guid));
	ASSERT (ACAPI_Property_GetPropertyGroup (group2) == APIERR_BADID);
	groups.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));
	ASSERT (!groups.Contains (group2));
}


/*-------------------------------------------------------**
** Tests common use-cases involving property definitions **
**-------------------------------------------------------*/
static void SimpleTestPropertyDefinitions ()
{
	// Define a property group
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	// Define a property definition
	API_PropertyDefinition definition = PropertyTestHelpers::CreateExampleBoolPropertyDefinition (group);

	// Create it
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition));

	// The create operation generates a guid for the definition
	ASSERT (definition.guid != APINULLGuid);

	// If we list all definitions
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (/*groupGuid = */ APINULLGuid, definitions));
	// Then the result array will contain the newly created one
	ASSERT (definitions.Contains (definition));

	// We can also list the definitions of only one group
	definitions.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (definition.groupGuid, definitions));
	ASSERT (definitions.Contains (definition));

	API_PropertyDefinition definition2;
	definition2.guid = definition.guid;

	// We can get a definition's data knowing only its guid
	ASSERT (definition != definition2);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));
	ASSERT (definition == definition2);

	// The parameters of the definition can be changed
	definition.name = "Renamed Property_Test Add-On Definition - " + PropertyTestHelpers::GenearteUniqueName ();
	definition.defaultValue.basicValue.singleVariant.variant.boolValue = true;
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition));

	ASSERT (definition != definition2);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));
	ASSERT (definition == definition2);

	// Property definitions can be deleted
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (definition.guid));

	// Then, retrieval operation results in an error
	ASSERT (ACAPI_Property_GetPropertyDefinition (definition) == APIERR_BADID);
}


/*-------------------------------------------------------------------------**
**		Tests rather unrealistic use-cases involving property groups	   **
** The purpose of this is to check the error handling of the API functions **
**-------------------------------------------------------------------------*/
static void ThoroughTestPropertyDefinitions ()
{
	// Create a single-choice string property definition
	API_PropertyDefinition definition;
	definition.guid = APINULLGuid;
	definition.groupGuid = APINULLGuid; // this is not valid
	definition.name = "Property_Test Add-On Definition - " + PropertyTestHelpers::GenearteUniqueName ();
	definition.valueType = API_PropertyStringValueType;
	definition.collectionType = API_PropertySingleChoiceEnumerationCollectionType;
	definition.measureType = API_PropertyDefaultMeasureType;

	// Set up the possibleEnumValues and the default value
	API_SingleEnumerationVariant variant;
	variant.keyVariant.type = API_PropertyGuidValueType;
	variant.keyVariant.guidValue = PropertyTestHelpers::RandomGuid ();
	variant.displayVariant.type = definition.valueType;
	variant.displayVariant.uniStringValue = "One";
	variant.nonLocalizedValue = GS::NoValue;
	definition.possibleEnumValues.Push (variant);

	definition.defaultValue.basicValue.singleVariant.variant = variant.keyVariant;

	variant.keyVariant.guidValue = PropertyTestHelpers::RandomGuid ();
	variant.displayVariant.uniStringValue = "Two";
	definition.possibleEnumValues.Push (variant);

	variant.keyVariant.guidValue = PropertyTestHelpers::RandomGuid ();
	variant.displayVariant.uniStringValue = "Three";
	definition.possibleEnumValues.Push (variant);

	variant.keyVariant.guidValue = PropertyTestHelpers::RandomGuid ();
	variant.displayVariant.uniStringValue = "Four";
	definition.possibleEnumValues.Push (variant);

	variant.keyVariant.guidValue = PropertyTestHelpers::RandomGuid ();
	variant.displayVariant.uniStringValue = "Five";
	definition.possibleEnumValues.Push (variant);

	// Creation results in an error, as the groupGuid is invalid
	ASSERT (ACAPI_Property_CreatePropertyDefinition (definition) == APIERR_BADID);

	// Create with a valid group
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));
	definition.groupGuid = group.guid;
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition));

	// Test the direct getter
	API_PropertyDefinition definition2;
	definition2.guid = definition.guid;
	ASSERT (definition != definition2);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));
	ASSERT (definition == definition2);

	// Test the getter that retrieves all of the definitions
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (definition.groupGuid, definitions));
	ASSERT (definitions.Contains (definition));

	// We can't create a copy of an existing definition, because of the name collision
	ASSERT (ACAPI_Property_CreatePropertyDefinition (definition2) == APIERR_NAMEALREADYUSED);

	// But we can create it with a different name
	definition2.name += PropertyTestHelpers::GenearteUniqueName ();
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition2));

	// Create a new group
	API_PropertyGroup group2 = PropertyTestHelpers::CreateExamplePropertyGroup ();
	ASSERT (group.name != group2.name);
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyGroup (group2));

	// Create a copy of an existing definition in the new group
	API_PropertyDefinition definition3 = definition2;
	ASSERT (ACAPI_Property_CreatePropertyDefinition (definition3) == APIERR_NAMEALREADYUSED);
	definition3.groupGuid = group2.guid;
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition3));

	// Move definition2 to the new group, but with the first definition's name
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition2));
	definition2.name = definition.name;
	definition2.groupGuid = definition3.groupGuid;
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition2));

	// We can't change the name def3's
	definition2.name = definition3.name;
	ASSERT (ACAPI_Property_ChangePropertyDefinition (definition2) == APIERR_NAMEALREADYUSED);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));

	// We can't move to a group where the name is not unqiue
	definition2.groupGuid = definition.groupGuid;
	ASSERT (ACAPI_Property_ChangePropertyDefinition (definition2) == APIERR_NAMEALREADYUSED);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));

	// If a possible enum value has null guid, then the API function will generate one
	definition.possibleEnumValues[1].keyVariant.guidValue = APINULLGuid;
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition));
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition));
	ASSERT (definition.possibleEnumValues[1].keyVariant.guidValue != APINULLGuid);

	// If the default value does not refer to a valid possible enum value, we get an error
	definition.possibleEnumValues[0].keyVariant.guidValue = APINULLGuid;
	ASSERT (ACAPI_Property_ChangePropertyDefinition (definition) != NoError);

	// But changing the default value fixes it of course
	definition.defaultValue.basicValue.singleVariant.variant = definition.possibleEnumValues[2].keyVariant;
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition));
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition));
	ASSERT (definition.possibleEnumValues[0].keyVariant.guidValue != APINULLGuid);

	// If more than one possible enum values have the same guid,
	// then the property definition is considered inconsistent
	for (UInt32 i = 1; i < definition.possibleEnumValues.GetSize (); i++) {
		definition.possibleEnumValues[i].keyVariant.guidValue = definition.possibleEnumValues[0].keyVariant.guidValue;
	}
	ASSERT (ACAPI_Property_ChangePropertyDefinition (definition) == APIERR_BADPARS);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition));

	// Invalid availability causes APIERR_BADID
	definition.availability.Push (APINULLGuid);
	ASSERT (ACAPI_Property_ChangePropertyDefinition (definition) == APIERR_BADID);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition));

	// Deleting one property definition doesn't affect the others
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (definition3.guid));
	definitions.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));
	ASSERT (definitions.Contains (definition));
	ASSERT (definitions.Contains (definition2));

	// Double delete results in an error
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (definition.guid));
	ASSERT (ACAPI_Property_DeletePropertyDefinition (definition.guid) == APIERR_BADID);

	// Deleting a property group removes all the definition inside it too.
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyGroup (group2.guid));
	ASSERT (ACAPI_Property_DeletePropertyDefinition (definition2.guid) == APIERR_BADID);
}



/*---------------------------------------------------------------**
** Tests common use-cases involving properties on a general elem **
**---------------------------------------------------------------*/
static void TestPropertiesOnElem (const API_Guid& elemGuid)
{
	// Create a property group
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	// Check if elem is classifiable - the test is pointless if not
	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, classificationSystemItemPairs));
	if (classificationSystemItemPairs.IsEmpty ()) {
		return;
	}

	// Create a property definition
	API_Property property;
	property.definition = PropertyTestHelpers::CreateExampleStringListPropertyDefinition (group);
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (property.definition));

	// Even though we have just created a definition, it is not available for the elem
	// so if we query which definitions apply to the elem, then the result array won't
	// contain the newly created one
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions));
	ASSERT (!definitions.Contains (property.definition));

	// Make the definition apply for all of the elem's classifications
	for (UIndex i = 0; i < classificationSystemItemPairs.GetSize (); ++i) {
		property.definition.availability.Push (classificationSystemItemPairs[i].second);
	}
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (property.definition));

	// Now the definition apply for the elem, and the query will reflect this
	definitions.Clear ();
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions));
	ASSERT (definitions.Contains (property.definition));

	// The property's value is now the default value of the definition
	property.isDefault = true;

	// Specific properties can be retrieved using their guid
	API_Property propertyResult;
	// ACAPI_Element_GetProperties returns the default value
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValue (elemGuid, property.definition.guid, propertyResult));
	ASSERT (propertyResult == property);

	// We can set a custom value to a property
	API_Variant variant;
	variant.type = API_PropertyStringValueType;
	for (int i = 0; i < 5; i++) {
		variant.uniStringValue = GS::ValueToUniString (1 << i);
		property.value.listVariant.variants.Push (variant);
	}

	property.isDefault = false;

	ASSERT (ACAPI_Property_IsValidValue (property.value, property.definition));
	ASSERT_NO_ERROR (ACAPI_Element_SetProperty (elemGuid, property));

	// ACAPI_Element_GetProperties returns the custom value now
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValue (elemGuid, property.definition.guid, propertyResult));
	ASSERT (propertyResult == property);

	// Inconsistent property values cause APIERR_BADPARS
	property.value.listVariant.variants[0].type = API_PropertyBooleanValueType;
	ASSERT (!ACAPI_Property_IsValidValue (property.value, property.definition));
	ASSERT (ACAPI_Element_SetProperty (elemGuid, property) == APIERR_BADPARS);

	// Delete the property definition to clean up after the test
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (property.definition.guid));
}


/*----------------------------------------------------------------**
** Tests common use-cases involving properties on an elem default **
**----------------------------------------------------------------*/
static void TestPropertiesOnElemDefault (const API_ElemType& type = API_ColumnID)
{
	// Create a property group
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	// Create the property definition
	API_Property property;
	property.definition = PropertyTestHelpers::CreateExampleStringMultiEnumPropertyDefinition (group);
	property.isDefault = true;
	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItemsDefault (type, classificationSystemItemPairs));
	for (UIndex i = 0; i < classificationSystemItemPairs.GetSize (); ++i) {
		property.definition.availability.Push (classificationSystemItemPairs[i].second);
	}
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (property.definition));

	API_Property propertyResult; 

	// Test the getter
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValueOfDefaultElem (type, property.definition.guid, propertyResult));
	ASSERT (propertyResult == property);

	// Set a custom value
	property.value.listVariant.variants.Push (property.definition.possibleEnumValues[0].keyVariant);
	property.value.listVariant.variants.Push (property.definition.possibleEnumValues[2].keyVariant);
	property.isDefault = false;
	ASSERT_NO_ERROR (ACAPI_Element_SetPropertyOfDefaultElem (type, property));

	// Test that the value has changed
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValueOfDefaultElem (type, property.definition.guid, propertyResult));
	ASSERT (propertyResult == property);

	// Delete the property definition to clean up after the test
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (property.definition.guid));
}


static API_Guid AddRandomClassification (API_ClassificationSystem& systemOut)
{
	systemOut.guid = APINULLGuid;
	systemOut.name = "Classification Test System";
	systemOut.editionVersion = "v1.0";
	systemOut.source = "Source";

	GSTimeRecord gsTimeRecord;
	TIGetTimeRecord (GSTime (), &gsTimeRecord, TI_CURRENT_TIME);
	GSDateRecord gsDateRecord (gsTimeRecord.year, gsTimeRecord.month, gsTimeRecord.day);

	systemOut.editionDate = gsDateRecord;
	if (ACAPI_Classification_GetClassificationSystem (systemOut) != NoError) {
		ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationSystem (systemOut));
	}
	ASSERT (systemOut.guid != APINULLGuid);

	API_ClassificationItem item;
	item.guid = APINULLGuid;
	item.id = PropertyTestHelpers::GenearteUniqueName ();
	item.name = "Classification Test Item [" + item.id + "]";
	item.description = "Description";
	ASSERT_NO_ERROR (ACAPI_Classification_CreateClassificationItem (item, systemOut.guid, APINULLGuid, APINULLGuid));
	ASSERT (item.guid != APINULLGuid);

	return item.guid;
}


static void EnsurePropertyExistsAvailableToElements (API_PropertyDefinition& definition, const GS::Array<API_Guid>& elemGuids)
{
	bool changed = false;
	for (const API_Guid& guid : elemGuids) {
		GS::Array<GS::Pair<API_Guid, API_Guid>> systemItemPairs;
		ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems	(guid, systemItemPairs));

		for (const auto& it : systemItemPairs) {
			API_Guid classification = it.second;

			if (!definition.availability.Contains (classification)) {
				definition.availability.Push (classification);
				changed = true;
			}
		}
	}

	if ((definition.guid == APINULLGuid)) {
		ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition));
	} else if (changed) {
		ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition));
	}
}


static void ShowReport (const GS::UniString& title, const GS::UniString& content, const GS::UniString& noContentDescription)
{
	if (content.IsEmpty ()) {
		DGAlert (DG_INFORMATION, title, noContentDescription, "", "Ok");
	} else if (!content.Contains ("\n")) {
		DGAlert (DG_INFORMATION, title, content, "", "Ok");
	} else {
		ACAPI_WriteReport ("\n" + title + "\n=====================================================\n\n" + content,
							false);
		DGAlert (DG_INFORMATION, title, "See Session Report window", "", "Ok");
	}
}


static GS::UniString PropertyDefinitionsToString (const GS::Array<API_PropertyDefinition>& definitions, const std::function<bool (const API_PropertyDefinition&)>& condition = [](const API_PropertyDefinition&) { return true; })
{
	GS::UniString string;
	int nextDisplayIndex = 1;
	definitions.EnumerateConst ([&] (const API_PropertyDefinition& definition) {
		if (condition (definition)) {
			string += GS::UniString::Printf ("%d. %T%s\n", nextDisplayIndex++, 
														   definition.name.ToPrintf (), 
														   definition.canValueBeEditable ? "" : " [ReadOnly]");
		}
	});
	return string;
}


GS::UniString PropertyValuesToString (const GS::Array<API_Property>& properties)
{
	GS::UniString string;
	for (UInt32 i = 0; i < properties.GetSize (); i++) {
		const API_Property& property = properties[i];

		switch (property.status) {
			case API_Property_NotAvailable:
				string += GS::UniString::Printf ("%d. [Not Available] %T\n", i + 1, property.definition.name.ToPrintf ());
				break;
			case API_Property_NotEvaluated:
				string += GS::UniString::Printf ("%d. [Not Evaluable] %T\n", i + 1, property.definition.name.ToPrintf ());
				break;
			default: 
				{
					GS::UniString propertyValue;
					const GSErrCode error = PropertyUtils::PropertyToString (property, propertyValue);
					if (error == NoError) {
						string += GS::UniString::Printf ("%d. %T%s = %T%s\n", i + 1, 
																			  property.definition.name.ToPrintf (), 
																			  property.definition.canValueBeEditable ? "" : " [ReadOnly]", 
																			  propertyValue.ToPrintf (), 
																			  property.isDefault ? " [Default]" : "");
					} else {
						string += GS::UniString::Printf ("%d. [Unknown error while converting value] %T\n", i + 1, property.definition.name.ToPrintf ());
					}
				} 
				break;
		}

	}
	return string;
}


static GS::Optional<API_AttributeIndex>	GetAlphabeticallyFirstAttributeIndex (API_AttrTypeID typeID)
{
	GS::Optional<API_AttributeIndex> firstIndex;
	GS::Optional<GS::UniString> firstName;
	ACAPI_Attribute_EnumerateAttributesByType (typeID, [&] (API_Attribute& attribute) {
		if (!firstName.HasValue () || !firstName->IsLess (attribute.header.name, GS::CaseInsensitive))  {
			firstIndex = attribute.header.index;
			firstName = attribute.header.name;
		}
	});

	return firstIndex;
}


static void ListAllPropertiesOfAlphabeticallyFirstAttributeByFilter (const API_AttrTypeID& typeID, API_PropertyDefinitionFilter filter, bool onlyVisible, GS::UniString& outAttributeName, GS::Array<API_Property>& propertiesOut)
{
	propertiesOut.Clear ();

	GS::Optional<API_AttributeIndex> index = GetAlphabeticallyFirstAttributeIndex (typeID);
	if (!index.HasValue ()) {
		return;
	}

	API_Attribute attrib	  = {};
	attrib.header.typeID	  = typeID;
	attrib.header.index		  = *index;
	ASSERT_NO_ERROR (ACAPI_Attribute_Get (&attrib));

	outAttributeName = attrib.header.name;

	GS::Array<API_PropertyDefinition> definitions;

	ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyDefinitions (attrib.header, filter, definitions));
	ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValues (attrib.header, definitions, propertiesOut));

	if (onlyVisible) {
		UIndex i = 0;
		while (i < propertiesOut.GetSize ()) {
			if (ACAPI_Attribute_IsPropertyDefinitionVisible (attrib.header, propertiesOut[i].definition.guid)) {
				++i;
			} else {
				propertiesOut.Delete (i);
			}
		}
	}
}

static void ListAllPropertiesOfAlphabeticallyFirstBuildingMaterial ()
{
	GS::UniString attributeName;
	GS::Array<API_Property> values;
	ListAllPropertiesOfAlphabeticallyFirstAttributeByFilter (API_BuildingMaterialID, API_PropertyDefinitionFilter_All, false, attributeName, values);

	ShowReport (GS::UniString::Printf ("Properties of building material \"%T\"", attributeName.ToPrintf ()),
				PropertyValuesToString (values),
				"No properties.");
				
}

static void ListAllUserDefinedPropertiesOfAlphabeticallyFirstBuildingMaterial ()
{
	GS::UniString attributeName;
	GS::Array<API_Property> values;
	ListAllPropertiesOfAlphabeticallyFirstAttributeByFilter (API_BuildingMaterialID, API_PropertyDefinitionFilter_UserDefined, false, attributeName, values);

	ShowReport (GS::UniString::Printf ("User defined properties of building material \"%T\"", attributeName.ToPrintf ()),
				PropertyValuesToString (values),
				"No properties.");
				
}

static void ListAllUserLevelBuiltInPropertiesOfAlphabeticallyFirstBuildingMaterial ()
{
	GS::UniString attributeName;
	GS::Array<API_Property> values;
	ListAllPropertiesOfAlphabeticallyFirstAttributeByFilter (API_BuildingMaterialID, API_PropertyDefinitionFilter_BuiltIn, false, attributeName, values);

	ShowReport (GS::UniString::Printf ("Built-in properties of of building material \"%T\"", attributeName.ToPrintf ()),
				PropertyValuesToString (values),
				"No properties.");
}


static void ListAllVisibleUserDefinedPropertiesOfAlphabeticallyFirstBuildingMaterial ()
{
	GS::UniString attributeName;
	GS::Array<API_Property> values;
	ListAllPropertiesOfAlphabeticallyFirstAttributeByFilter (API_BuildingMaterialID, API_PropertyDefinitionFilter_UserDefined, true, attributeName, values);

	ShowReport (GS::UniString::Printf ("Visible user defined properties of building material \"%T\"", attributeName.ToPrintf ()),
				PropertyValuesToString (values),
				"No visible properties.");
				
}

static void ListAllVisibleUserLevelBuiltInPropertiesOfAlphabeticallyFirstBuildingMaterial ()
{
	GS::UniString attributeName;
	GS::Array<API_Property> values;
	ListAllPropertiesOfAlphabeticallyFirstAttributeByFilter (API_BuildingMaterialID, API_PropertyDefinitionFilter_BuiltIn, true, attributeName, values);

	ShowReport (GS::UniString::Printf ("Visible built-in properties of of building material \"%T\"", attributeName.ToPrintf ()),
				PropertyValuesToString (values),
				"No visible properties.");
}


/*------------------------------------------------------------------------------**
** Tests common use-cases involving properties on a building material attribute **
**------------------------------------------------------------------------------*/
static void RunTestsOnBuildingMaterial ()
{
	// Create new test building material
	API_Attribute attrib;
	attrib = {};

	attrib.header.typeID = API_BuildingMaterialID;
	strcpy (attrib.header.name, "Test Building Material");
	ASSERT_NO_ERROR (ACAPI_Attribute_Create (&attrib, nullptr));

	// Create Classification System and Classification
	API_ClassificationSystem system;
	API_Guid classificationGuid = AddRandomClassification (system);

	// Classify building material
	ASSERT_NO_ERROR (ACAPI_Attribute_AddClassificationItem (attrib.header, classificationGuid));

	// Create a property group
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	// Create the property definitions
	API_Property intProperty;
	intProperty.definition = PropertyTestHelpers::CreateExampleIntPropertyDefinition (group);
	intProperty.isDefault = true;
	intProperty.definition.availability.Push (classificationGuid);
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (intProperty.definition));

	API_Property strListProperty;
	strListProperty.definition = PropertyTestHelpers::CreateExampleStringListPropertyDefinition (group);
	strListProperty.isDefault = true;
	strListProperty.definition.availability.Push (classificationGuid);
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (strListProperty.definition));

	API_Property multiEnumProperty;
	multiEnumProperty.definition = PropertyTestHelpers::CreateExampleStringMultiEnumPropertyDefinition (group);
	multiEnumProperty.isDefault = true;
	multiEnumProperty.definition.availability.Push (classificationGuid);
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (multiEnumProperty.definition));

	// Set a custom values
	intProperty.value.singleVariant.variant.intValue = 42;
	intProperty.value.singleVariant.variant.type = API_PropertyIntegerValueType;
	intProperty.isDefault = false;

	GS::UniString strValue1 = "AAA";
	GS::UniString strValue2 = "BBB";
	API_Variant variant1;
	API_Variant variant2;

	variant1.type = API_PropertyStringValueType;
	variant1.uniStringValue = strValue1;
	variant2.type = API_PropertyStringValueType;
	variant2.uniStringValue = strValue2;

	strListProperty.value.listVariant.variants.Push (variant1);
	strListProperty.value.listVariant.variants.Push (variant2);
	strListProperty.isDefault = false;

	multiEnumProperty.value.listVariant.variants.Push (multiEnumProperty.definition.possibleEnumValues [0].keyVariant);
	multiEnumProperty.value.listVariant.variants.Push (multiEnumProperty.definition.possibleEnumValues [2].keyVariant);
	multiEnumProperty.isDefault = false;

	// Set properties on building material
	GS::Array<API_Property> inProperties;
	inProperties.Push (strListProperty);
	inProperties.Push (multiEnumProperty);

	ASSERT_NO_ERROR (ACAPI_Attribute_SetProperty (attrib.header, intProperty));
	ASSERT_NO_ERROR (ACAPI_Attribute_SetProperties (attrib.header, inProperties));

	// Get definitions on building material
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyDefinitions (attrib.header, API_PropertyDefinitionFilter_UserDefined, definitions));
	ASSERT (definitions.GetSize () == 3);

	// Test property definitions
	ASSERT (ACAPI_Attribute_IsPropertyDefinitionAvailable (attrib.header, definitions[0].guid));
	ASSERT (ACAPI_Attribute_IsPropertyDefinitionVisible (attrib.header, definitions[1].guid));
	ASSERT (ACAPI_Attribute_IsPropertyDefinitionValueEditable (attrib.header, definitions [2].guid));

	// Get properties of building material
	{
		API_Property outProperty;
		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValue (attrib.header, intProperty.definition.guid, outProperty));

		GS::Array<API_Property> outProperties;
		GS::Array<API_Guid> inGuids;
		inGuids.Push (strListProperty.definition.guid);
		inGuids.Push (multiEnumProperty.definition.guid);
		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValuesByGuid (attrib.header, inGuids, outProperties));
		ASSERT (outProperties.GetSize () == 2);

		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValues (attrib.header, definitions, outProperties));
		ASSERT (outProperties.GetSize () == 3);

		// Check values of properties
		for (const API_Property& prop : outProperties) {
			if (prop.definition.guid == intProperty.definition.guid) {
				ASSERT (prop.value.singleVariant.variant.intValue == 42);
				ASSERT (prop.value.singleVariant.variant.intValue == outProperty.value.singleVariant.variant.intValue);
				continue;
			}
			if (prop.definition.guid == strListProperty.definition.guid) {
				ASSERT (prop.value.listVariant.variants [0].uniStringValue == strValue1);
				ASSERT (prop.value.listVariant.variants [1].uniStringValue == strValue2);
				continue;
			}
			if (prop.definition.guid == multiEnumProperty.definition.guid) {
				ASSERT (prop.value.listVariant.variants [0] == multiEnumProperty.definition.possibleEnumValues [0].keyVariant);
				ASSERT (prop.value.listVariant.variants [1] == multiEnumProperty.definition.possibleEnumValues [2].keyVariant);
				continue;
			}
			throw GS::Exception ();
		}
	}

	// Modify property value, and check if value is modified
	{
		API_Property toModifyProperty;
		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValue (attrib.header, intProperty.definition.guid, toModifyProperty));
		toModifyProperty.value.singleVariant.variant.intValue = 69;
		
		GS::Array<API_Attr_Head> attributeList;
		attributeList.Push (attrib.header);
		ASSERT_NO_ERROR (ACAPI_Attribute_ModifyPropertyValue (toModifyProperty, attributeList));

		API_Property toCheckProperty;
		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValue (attrib.header, intProperty.definition.guid, toCheckProperty));
		ASSERT (toCheckProperty.value.singleVariant.variant.intValue == 69);
	}

	
	// Check if attribute has a property returning it's name
	{ 
		GS::Array<API_PropertyDefinition> builtInProperties;
		GS::Array<API_Property> outValues;

		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyDefinitions (attrib.header, API_PropertyDefinitionFilter_BuiltIn, builtInProperties));
		ASSERT_NO_ERROR (ACAPI_Attribute_GetPropertyValues (attrib.header, builtInProperties, outValues));

		bool hasPropertyReturningAttributeName = false;
		for (const API_Property& it : outValues) {
			if (it.status != API_Property_HasValue)
				continue;

			const API_Variant& variant = it.value.singleVariant.variant;
			if (variant.type == API_PropertyStringValueType && variant.uniStringValue == attrib.header.name)
				hasPropertyReturningAttributeName = true;
		}

		ASSERT (hasPropertyReturningAttributeName);
	}
		



	// Cleanup - Delete building material
	ASSERT_NO_ERROR (ACAPI_Attribute_Delete (attrib.header));

	// Cleanup - Delete classification system
	ASSERT_NO_ERROR (ACAPI_Classification_DeleteClassificationSystem (system.guid));

	// Cleanup - Delete properties
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (intProperty.definition.guid));
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (strListProperty.definition.guid));
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (multiEnumProperty.definition.guid));

	DGAlert (DG_INFORMATION, "Test success", "Tests executed successfully.", "", "Ok");
}


/*------------------------------------------------------------------------**
** Tests common use-cases involving properties on a list of general elems **
**------------------------------------------------------------------------*/
static void TestPropertiesOnElemList (const API_Guid& elemGuid)
{
	GS::Array<API_Guid> elemList;
	elemList.Push (elemGuid);

	// Check if elem is classifiable - the test is pointless if not
	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, classificationSystemItemPairs));
	if (classificationSystemItemPairs.IsEmpty ()) {
		return;
	}

	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	API_PropertyDefinition definition;
	definition = PropertyTestHelpers::CreateExampleIntPropertyDefinition (group);

	EnsurePropertyExistsAvailableToElements (definition, elemList);

	// Check that the definition is created
	ASSERT (definition.guid != APINULLGuid);
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition));

	// Check that the elem has the definition
	GS::Array<API_PropertyDefinition> elemProperties;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, elemProperties));
	ASSERT (elemProperties.Contains (definition));

	// temporarily make the definition unavailable for the elem
	definition.availability.Clear ();
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition));
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, elemProperties));
	ASSERT (!elemProperties.Contains (definition));

	EnsurePropertyExistsAvailableToElements (definition, elemList);

	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, elemProperties));
	ASSERT (elemProperties.Contains (definition));

	// ACAPI_Property_ModifyPropertyValue can be used the change the property value of all elements in the list
	API_Property property;
	property.definition = definition;
	property.value = definition.defaultValue.basicValue;
	property.value.singleVariant.variant.intValue = 42;
	ASSERT_NO_ERROR (ACAPI_Property_ModifyPropertyValue (property, elemList));

	API_Property propertyResult;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValue (elemGuid, definition.guid, propertyResult));
	ASSERT (propertyResult == property);

	// clean up
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (definition.guid));
}


static void TestBuiltInProperties (const API_Guid& elemGuid)
{
	{
		API_PropertyGroup group;
		ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

		API_PropertyDefinition definition;
		definition = PropertyTestHelpers::CreateExampleIntPropertyDefinition (group);

		// Built-in properties can not be created from the API
		definition.definitionType = API_PropertyStaticBuiltInDefinitionType;

		// The definition will not be created
		ASSERT (ACAPI_Property_CreatePropertyDefinition (definition) != NoError);
		ASSERT (ACAPI_Property_GetPropertyDefinition (definition) == APIERR_BADID);

		// The same goes for groups: built-in property groups are not modifiable
		group.groupType = API_PropertyDynamicBuiltInGroupType;
		ASSERT (ACAPI_Property_ChangePropertyGroup (group) != NoError);

		// Clean up (no need, nothing created, returns error)
		ASSERT (ACAPI_Property_DeletePropertyDefinition (definition.guid) == APIERR_BADID);
	}

	// Find a built-in group
	GS::Array<API_PropertyGroup> groups;
	ACAPI_Property_GetPropertyGroups (groups);

	for (UIndex i = 0; i < groups.GetSize (); ++i) {
		// If found a built-in group
		if (groups[i].groupType != API_PropertyCustomGroupType) {
			API_PropertyGroup& builtInGroup = groups[i];
			if (builtInGroup.guid != APINULLGuid) {
				// we can not modify the group
				ASSERT (ACAPI_Property_ChangePropertyGroup (builtInGroup) != NoError);

				API_PropertyDefinition definition;
				definition = PropertyTestHelpers::CreateExampleIntPropertyDefinition (builtInGroup);

				// we can not create a definition in the group
				ASSERT (ACAPI_Property_CreatePropertyDefinition (definition) != NoError);

				// Get an existing definition in the group
				GS::Array<API_PropertyDefinition> definitions;
				ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (builtInGroup.guid, definitions));
				for (UIndex i = 0; i < definitions.GetSize (); ++i) {
					API_PropertyDefinition& def = definitions[i];
					// All the definitions in the group are built-in
					ASSERT (def.definitionType != API_PropertyCustomDefinitionType);

					// Built-in definitions can't be changed
					ASSERT (ACAPI_Property_ChangePropertyDefinition (def) != NoError);

					GS::Array<API_Property> propertiesToChange;
					{
						API_Property property;
						property.definition = def;
						property.value = def.defaultValue.basicValue;
						propertiesToChange.Push (property);
					}

					if (!def.canValueBeEditable) {
						// Some property are guaranteed to be read-only for all elements
						// Please note, that if def.canValueBeEditable is true, the value for a particular elem might still be read-only.
						if (ACAPI_Element_IsPropertyDefinitionAvailable (elemGuid, def.guid)) {
							ASSERT (ACAPI_Element_SetProperties (elemGuid, propertiesToChange) == APIERR_READONLY);
						}
					}
				}
			}
		}
	}
}


static void SimpleTestPropertiesWithExpressions ()
{
	// Define a property definition
	API_PropertyGroup group;
	PropertyTestHelpers::GetCommonExamplePropertyGroup (group);
	API_PropertyDefinition definition = PropertyTestHelpers::CreateExampleExpressionPropertyDefinition (group);

	// Create it
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition));

	// Modify it
	definition.defaultValue.propertyExpressions[0] = "\"a\"";

	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition));

	// Specific properties can be retrieved using their guid
	API_PropertyDefinition definition2;
	definition2.guid = definition.guid;

	// Property expressions should be copied
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));
	ASSERT (definition == definition2);

	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (definition.guid));
}


static void ThoroughTestPropertiesWithExpressions ()
{
	// Define a property definition
	API_PropertyGroup group;
	PropertyTestHelpers::GetCommonExamplePropertyGroup (group);
	API_PropertyDefinition definition = PropertyTestHelpers::CreateExampleExpressionPropertyDefinition (group);

	// Empty property expression list should cause an error
	definition.defaultValue.propertyExpressions = GS::Array<GS::UniString> ();
	ASSERT (ACAPI_Property_CreatePropertyDefinition (definition) == APIERR_PARMISSING);

	// Invalid expressions in the list should also cause an error
	definition.defaultValue.propertyExpressions = {"asdfasdf(*)(*"};
	ASSERT (ACAPI_Property_CreatePropertyDefinition (definition) == APIERR_BADPARS);

	definition.defaultValue.propertyExpressions = {"1"};
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition));
	API_Guid nullGuid {};
	ASSERT (ACAPI_Element_IsPropertyDefinitionValueEditable (nullGuid, definition.guid) == true); //No circular dependencies so it's ok
}


static void TestPropertiesWithExpressionsWithCycles ()
{
	// Define a property definition
	API_PropertyGroup group;
	PropertyTestHelpers::GetCommonExamplePropertyGroup (group);
	API_PropertyDefinition definition1 = PropertyTestHelpers::CreateExampleExpressionPropertyDefinition (group);
	API_PropertyDefinition definition2 = PropertyTestHelpers::CreateExampleExpressionPropertyDefinition (group);
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition1));
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition2));

	GS::UniString definition1expr; 
	ACAPI_Property_GetPropertyExprReferenceString (&definition1, &definition1expr);

	GS::UniString definition2expr; 
	ACAPI_Property_GetPropertyExprReferenceString (&definition2, &definition2expr);

	definition1.defaultValue.propertyExpressions = {definition2expr};
	definition2.defaultValue.propertyExpressions = {definition1expr};

	ACAPI_Property_ChangePropertyDefinition (definition1);
	ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definition2)); //This would create a cycle but it's ok
}


static void CompareConvertedUndefinedPropertyDefinition (const API_PropertyDefinition& definition)
{
	API_PropertyDefinition definition1 = definition;
	// A property value with UserUndefined variant status is valid
	definition1.defaultValue.basicValue.variantStatus = API_VariantStatusUserUndefined;
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (definition1));

	API_PropertyDefinition definition2;
	definition2.guid = definition1.guid;

	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinition (definition2));
	ASSERT (definition1 == definition2);
}

static void SimpleTestPropertyVariantStatuses ()
{
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	CompareConvertedUndefinedPropertyDefinition (PropertyTestHelpers::CreateExampleBoolPropertyDefinition (group));
	CompareConvertedUndefinedPropertyDefinition (PropertyTestHelpers::CreateExampleIntPropertyDefinition (group));
	CompareConvertedUndefinedPropertyDefinition (PropertyTestHelpers::CreateExampleStringListPropertyDefinition (group));
	CompareConvertedUndefinedPropertyDefinition (PropertyTestHelpers::CreateExampleStringMultiEnumPropertyDefinition (group));
	CompareConvertedUndefinedPropertyDefinition (PropertyTestHelpers::CreateExampleExpressionPropertyDefinition (group));

	{
		// A property value with Null variant status is invalid
		API_PropertyDefinition definition1 = PropertyTestHelpers::CreateExampleBoolPropertyDefinition (group);
		definition1.defaultValue.basicValue.variantStatus = API_VariantStatusNull;
		ASSERT (ACAPI_Property_CreatePropertyDefinition (definition1) == APIERR_BADVALUE);
	}
}

static void TestXMLImport ()
{
	// Exported from ARCHICAD
	const char* xmlString =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>"
		"<BuildingInformation>"
		"	<Classification/>"
		"	<PropertyDefinitionGroups>"
		"		<PropertyDefinitionGroup>"
		"			<Name>Property_Test Add-On Group</Name>"
		"			<PropertyDefinitions>"
		"				<PropertyDefinition>"
		"					<Name>Property1</Name>"
		"					<Description>Expression Property imported from the Property_Test add-on.</Description>"
		"					<ValueDescriptor Type=\"SingleValueDescriptor\">"
		"						<ValueType>Integer</ValueType>"
		"					</ValueDescriptor>"
		"					<MeasureType>Default</MeasureType>"
		"					<DefaultValue>"
		"						<DefaultValueType>Expression</DefaultValueType>"
		"						<ExpressionDefaultValue>"
		"							<Expression>9 + {Property:Property_Test Add-On Group/Property2}</Expression>"
		"						</ExpressionDefaultValue>"
		"					</DefaultValue>"
		"					<ClassificationIDs/>"
		"				</PropertyDefinition>"
		"				<PropertyDefinition>"
		"					<Name>Property2</Name>"
		"					<Description>Property imported from the Property_Test add-on.</Description>"
		"					<ValueDescriptor Type=\"SingleValueDescriptor\">"
		"						<ValueType>Integer</ValueType>"
		"					</ValueDescriptor>"
		"					<DefaultValue>"
		"						<DefaultValueType>Basic</DefaultValueType>"
		"						<Variant Type=\"IntVariant\">"
		"							<Status>Normal</Status>"
		"							<Value>42</Value>"
		"						</Variant>"
		"					</DefaultValue>"
		"					<ClassificationIDs/>"
		"				</PropertyDefinition>"
		"			</PropertyDefinitions>"
		"		</PropertyDefinitionGroup>"
		"	</PropertyDefinitionGroups>"
		"</BuildingInformation>";

	ASSERT_NO_ERROR (ACAPI_Property_Import (xmlString, API_ReplaceConflictingProperties));

	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));

	API_PropertyDefinition importedDefinition1;
	API_PropertyDefinition importedDefinition2;
	for (UIndex i = 0; i < definitions.GetSize (); ++i) {
		if (definitions[i].name == "Property1" && definitions[i].description == "Expression Property imported from the Property_Test add-on.") {
			importedDefinition1 = definitions[i];
		} else if (definitions[i].name == "Property2" && definitions[i].description == "Property imported from the Property_Test add-on.") {
			importedDefinition2 = definitions[i];
		}
	}

	ASSERT (importedDefinition1.guid != APINULLGuid);
	ASSERT (importedDefinition2.guid != APINULLGuid);
	ASSERT (importedDefinition1.collectionType == API_PropertySingleCollectionType);
	ASSERT (importedDefinition1.valueType ==  API_PropertyIntegerValueType);
	ASSERT (importedDefinition1.defaultValue.hasExpression);
	ASSERT (importedDefinition1.defaultValue.propertyExpressions.GetSize () == 1);
	GS::UniString propertyReferenceString;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyExprReferenceString (&importedDefinition2, &propertyReferenceString));
	ASSERT (importedDefinition1.defaultValue.propertyExpressions[0] == "9 + " + propertyReferenceString);
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (importedDefinition1.guid));
	ASSERT_NO_ERROR (ACAPI_Property_DeletePropertyDefinition (importedDefinition2.guid));
}


/*----------------------------------------------------------------**
**			Runs all the previously defined test cases.           **
**				Requires an element to be selected.			      **
**----------------------------------------------------------------*/
static void RunTestsOnElem (const API_Guid& elemGuid)
{
	SimpleTestPropertyGroups ();
	ThoroughTestPropertyGroups ();
	SimpleTestPropertyDefinitions ();
	ThoroughTestPropertyDefinitions ();
	TestPropertiesOnElem (elemGuid);
	TestPropertiesOnElemDefault ();
	SimpleTestPropertiesWithExpressions ();
	ThoroughTestPropertiesWithExpressions ();
	TestPropertiesWithExpressionsWithCycles ();
	SimpleTestPropertyVariantStatuses ();
	TestPropertiesOnElemList (elemGuid);
	TestBuiltInProperties (elemGuid);
	TestXMLImport ();

	DGAlert (DG_INFORMATION, "Test success", "Tests executed successfully.", "", "Ok");
}


static GS::Array<API_PropertyDefinition> GetPropertiesOfElementForPropertyValue (const API_Guid& elemGuid, const API_PropertyValue& propertyValue)
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions));

	GS::Array<API_PropertyDefinition> filteredDefinitions;
	for (const API_PropertyDefinition& definition : definitions) {
		if (ACAPI_Property_IsValidValue (propertyValue, definition)) {
			filteredDefinitions.Push (definition);
		}
	}

	return filteredDefinitions;
}


static void ListPropertiesWithIntegerDataType (const API_Guid& elemGuid)
{
	API_PropertyValue propertyValue;
	propertyValue.singleVariant.variant.type = API_PropertyIntegerValueType;

	GS::Array<API_PropertyDefinition> definitions;
	definitions = GetPropertiesOfElementForPropertyValue (elemGuid, propertyValue);

	ShowReport ("Properties with integer value for the selected element",
				PropertyDefinitionsToString (definitions),
				"No properties with integer value found.");
}


/*------------------------------------------------------------**
** Creates a new boolean single value property for an element **
**------------------------------------------------------------*/
static void DefineNewBoolProperty (const API_Guid& elemGuid)
{
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	API_Property property;
	property.definition = PropertyTestHelpers::CreateExampleBoolPropertyDefinition (group);
	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, classificationSystemItemPairs));
	for (UIndex i = 0; i < classificationSystemItemPairs.GetSize (); ++i) {
		property.definition.availability.Push (classificationSystemItemPairs[i].second);
	}
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (property.definition));

	// Add a custom value
	property.value.singleVariant.variant.type = API_PropertyBooleanValueType;
	property.value.singleVariant.variant.boolValue = true;
	property.isDefault = false;
	GS::Array<API_Property> properties;
	properties.Push (property);
	ASSERT_NO_ERROR (ACAPI_Element_SetProperties (elemGuid, properties));
}


/*---------------------------------------------------------**
** Creates a new integer list type property for an element **
**---------------------------------------------------------*/
static void DefineNewStringListProperty (const API_Guid& elemGuid)
{
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	API_Property property;
	property.definition = PropertyTestHelpers::CreateExampleStringListPropertyDefinition (group);
	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, classificationSystemItemPairs));
	for (UIndex i = 0; i < classificationSystemItemPairs.GetSize (); ++i) {
		property.definition.availability.Push (classificationSystemItemPairs[i].second);
	}
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (property.definition));

	// Add a custom value
	API_Variant variant;
	variant.type = API_PropertyStringValueType;
	for (int i = 0; i < 5; i++) {
		variant.uniStringValue = GS::ValueToUniString (1 << i);
		property.value.listVariant.variants.Push (variant);
	}
	property.isDefault = false;
	GS::Array<API_Property> properties;
	properties.Push (property);
	ASSERT_NO_ERROR (ACAPI_Element_SetProperties (elemGuid, properties));
}


/*--------------------------------------------------------------------------**
** Creates a new string multiple choice enumeration property for an element **
**--------------------------------------------------------------------------*/
static void DefineStringMultiEnumtProperty (const API_Guid& elemGuid)
{
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	API_Property property;
	property.definition = PropertyTestHelpers::CreateExampleStringMultiEnumPropertyDefinition (group);
	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, classificationSystemItemPairs));
	for (UIndex i = 0; i < classificationSystemItemPairs.GetSize (); ++i) {
		property.definition.availability.Push (classificationSystemItemPairs[i].second);
	}
	ASSERT_NO_ERROR (ACAPI_Property_CreatePropertyDefinition (property.definition));

	// Add a custom value
	property.value.listVariant.variants.Push (property.definition.possibleEnumValues[1].keyVariant);
	property.isDefault = false;
	GS::Array<API_Property> properties;
	properties.Push (property);
	ASSERT_NO_ERROR (ACAPI_Element_SetProperties (elemGuid, properties));
}


/*----------------------------------------------------------------------------**
** Lists all the visible properties (definition name and value) of an element **
**----------------------------------------------------------------------------*/
static void ListVisiblePropertiesByFilter (const API_Guid& elemGuid, const GS::UniString& title, API_PropertyDefinitionFilter filter)
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, filter, definitions));
	GS::Array<API_Guid> propertyDefinitionList;
	for (UInt32 i = 0; i < definitions.GetSize (); i++) {
		if (ACAPI_Element_IsPropertyDefinitionVisible (elemGuid, definitions[i].guid)) {
			propertyDefinitionList.Push (definitions[i].guid);
		}
	}
	GS::Array<API_Property> properties;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValuesByGuid (elemGuid, propertyDefinitionList, properties));

	ShowReport (title,
				PropertyValuesToString (properties),
				"No properties found.");
}

static void ListVisibleProperties (const API_Guid& elemGuid)
{
	ListVisiblePropertiesByFilter (elemGuid, "Visible properties of selected element", API_PropertyDefinitionFilter_All);
}

static void ListVisibleUserDefinedProperties (const API_Guid& elemGuid)
{
	ListVisiblePropertiesByFilter (elemGuid, "Visible user defined properties of selected element", API_PropertyDefinitionFilter_UserDefined);
}

static void ListVisibleUserLevelBuiltInProperties (const API_Guid& elemGuid)
{
	ListVisiblePropertiesByFilter (elemGuid, "Visible built-in properties of selected element", API_PropertyDefinitionFilter_BuiltIn);
}


/*----------------------------------------------------------------**
** Removes all the custom values for the properties of an element **
**----------------------------------------------------------------*/
static void SetAllPropertiesDefault (const API_Guid& elemGuid)
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions));
	GS::Array<API_Property> properties;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValues (elemGuid, definitions, properties));

	for (UInt32 i = 0; i < properties.GetSize (); i++) {
		properties[i].isDefault = true;
	}
	ASSERT_NO_ERROR (ACAPI_Element_SetProperties (elemGuid, properties));
}


/*-----------------------------------------------------------------------------------------**
** Makes all properties unavailable for an element (affects every element in its category) **
**-----------------------------------------------------------------------------------------*/
static void DeleteAllProperties (const API_Guid& elemGuid)
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions));

	GS::Array<GS::Pair<API_Guid, API_Guid>> classificationSystemItemPairs;
	ASSERT_NO_ERROR (ACAPI_Element_GetClassificationItems (elemGuid, classificationSystemItemPairs));

	for (UInt32 i = 0; i < definitions.GetSize (); i++) {
		if (definitions[i].definitionType == API_PropertyCustomDefinitionType) {
			for (UIndex j = 0; j < classificationSystemItemPairs.GetSize (); ++j) {
				definitions[i].availability.DeleteAll (classificationSystemItemPairs[j].second);
			}
			ASSERT_NO_ERROR (ACAPI_Property_ChangePropertyDefinition (definitions[i]));
		}
	}
}


namespace SelectionProperties {

/*--------------------------------------------------**
**  Adds a new integer property that is available   **
**		 for all of the selected elements			**
**--------------------------------------------------*/
static void DefineNewIntProperty ()
{
	API_PropertyGroup group;
	ASSERT_NO_ERROR (PropertyTestHelpers::GetCommonExamplePropertyGroup (group));

	API_PropertyDefinition definition;
	definition = PropertyTestHelpers::CreateExampleIntPropertyDefinition (group);

	EnsurePropertyExistsAvailableToElements (definition, PropertyTestHelpers::GetSelectedElements());
}


/*-------------------------------------------------------------------------**
** Sets all the integer property values of all the selected elements to 42 **
**-------------------------------------------------------------------------*/
static void SetAllIntPropertiesTo42 ()
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));
	GS::Array<API_Guid> selectedElements = PropertyTestHelpers::GetSelectedElements ();

	for (UIndex i = 0; i < definitions.GetSize (); ++i) {
		if (definitions[i].definitionType == API_PropertyCustomDefinitionType &&
			definitions[i].collectionType == API_PropertySingleCollectionType &&
			definitions[i].valueType == API_PropertyIntegerValueType)
		{
			API_Property property;
			property.definition = definitions[i];
			property.value.singleVariant.variant.type = property.definition.valueType;
			property.value.singleVariant.variant.intValue = 42;
			property.isDefault = false;

			// remove the elements from the list, which the property is not available for
			// (if you don't remove them, APIERR_BADPROPERTY will be returned)
			GS::Array<API_Guid> filteredSelectedElements;

			for (UIndex i = 0; i < selectedElements.GetSize (); ++i) {
				bool isAvailable = ACAPI_Element_IsPropertyDefinitionAvailable (selectedElements[i], property.definition.guid);
				bool isValueEditable = ACAPI_Element_IsPropertyDefinitionValueEditable (selectedElements[i], property.definition.guid);
				if (isValueEditable && DBVERIFY (isAvailable)) {
					filteredSelectedElements.Push (selectedElements[i]);
				}
			}
			ASSERT_NO_ERROR (ACAPI_Property_ModifyPropertyValue (property, filteredSelectedElements));
		}
	}
}

/*-------------------------------------------------------------------------**
** Sets all the integer property values of all the selected elements to 42 **
**-------------------------------------------------------------------------*/
static GSErrCode ExampleForDocumentation_GetEditablePropertyDefinitions (const API_Guid& elemGuid, GS::Array<API_PropertyDefinition>& editableProperties)
{
	GS::Array<API_PropertyDefinition> definitions;
	GSErrCode error = ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions);
	if (error == NoError) {
		for (UInt32 i = 0; i < definitions.GetSize (); ++i) {
			if (ACAPI_Element_IsPropertyDefinitionValueEditable (elemGuid, definitions[i].guid)) {
				editableProperties.Push (definitions[i]);
			}
		}
	}
	return error;
}

static void SetAllIntPropertiesTo42_ExampleForDocumentation ()
{
	GS::Array<API_Guid> selectedElements = PropertyTestHelpers::GetSelectedElements ();
	for (API_Guid elemGuid : selectedElements) {
		GS::Array<API_PropertyDefinition> definitions;
		ASSERT_NO_ERROR (ExampleForDocumentation_GetEditablePropertyDefinitions (elemGuid, definitions));
		for (UIndex i = 0; i < definitions.GetSize (); ++i) {
			if (definitions[i].definitionType == API_PropertyCustomDefinitionType &&
				definitions[i].collectionType == API_PropertySingleCollectionType &&
				definitions[i].valueType == API_PropertyIntegerValueType)
			{
				API_Property property;
				property.definition = definitions[i];
				property.value.singleVariant.variant.type = property.definition.valueType;
				property.value.singleVariant.variant.intValue = 42;
				property.isDefault = false;
	
				GS::Array<API_Guid> filteredSelectedElements;
				filteredSelectedElements.Push (elemGuid);
				ASSERT_NO_ERROR (ACAPI_Property_ModifyPropertyValue (property, filteredSelectedElements));
			}
		}
	}
}


static GSErrCode SetAllStringPropertiesToAbcd ()
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));
	GS::Array<API_Guid> selectedElements = PropertyTestHelpers::GetSelectedElements ();

	for (UIndex i = 0; i < definitions.GetSize (); ++i) {
		if (definitions[i].canValueBeEditable &&
			definitions[i].collectionType == API_PropertySingleCollectionType &&
			definitions[i].valueType == API_PropertyStringValueType)
		{
			API_Property property;
			property.definition = definitions[i];
			property.value.singleVariant.variant.type = property.definition.valueType;
			property.value.singleVariant.variant.uniStringValue = "abcd";
			property.isDefault = false;

			// remove the elements from the list, which the property is not available for
			// (if you don't remove them, APIERR_BADPROPERTY will be returned)
			GS::Array<API_Guid> filteredSelectedElements;

			for (UIndex i = 0; i < selectedElements.GetSize (); ++i) {
				bool isAvailable = ACAPI_Element_IsPropertyDefinitionAvailable (selectedElements[i], property.definition.guid);
				bool isValueEditable = ACAPI_Element_IsPropertyDefinitionValueEditable (selectedElements[i], property.definition.guid);
				if (isValueEditable && DBVERIFY (isAvailable)) {
					filteredSelectedElements.Push (selectedElements[i]);
				}
			}

			ASSERT_NO_ERROR (ACAPI_Property_ModifyPropertyValue (property, filteredSelectedElements));
		}
	}
	return NoError;
}

} // namespace SelectionProperties

static GSErrCode NotificationCallback (const API_NotifyElementType *elemType)
{
	if (elemType != nullptr && elemType->notifID == APINotifyElement_PropertyValueChange) {
		DGAlert (DG_INFORMATION, "Property value change", "An element's property value has been changed.", "", "Ok");
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


static GSErrCode ListAllBuiltInProperties ()
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));

	ShowReport ("All Built-in Properties",
				PropertyDefinitionsToString (definitions, 
											[&] (const API_PropertyDefinition& definition) { return definition.definitionType != API_PropertyCustomDefinitionType; }),
				"No properties found.");

	return NoError;
}


static GSErrCode ListAllCustomProperties ()
{
	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));

	ShowReport ("All Custom Properties",
				PropertyDefinitionsToString (definitions, 
											[&] (const API_PropertyDefinition& definition) { return definition.definitionType == API_PropertyCustomDefinitionType; }),
				"No properties found.");

	return NoError;
}


static GSErrCode ListAllPropertyGroups (const GS::UniString& alertTitle, const std::function<bool (const API_PropertyGroup&)>& condition)
{
	GS::Array<API_PropertyGroup> groups;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));

	GS::UniString string;
	groups.EnumerateConst ([&] (const API_PropertyGroup& group) {
		if (condition (group)) {
			string += group.name + "\n";
		}
	});

	ShowReport (alertTitle,
				string,
				"No groups found.");

	return NoError;
}


static GSErrCode ListAllBuiltInPropertyGroups ()
{
	return ListAllPropertyGroups ("All Built-in Property Groups", [&] (const API_PropertyGroup& group) {
		return group.groupType != API_PropertyCustomGroupType;
	});
}


static GSErrCode ListAllCustomPropertyGroups ()
{
	return ListAllPropertyGroups ("All Custom Property Groups", [&] (const API_PropertyGroup& group) {
		return group.groupType == API_PropertyCustomGroupType;
	});
}


static GSErrCode ListAllPropertiesOfFirstGroup (const GS::UniString& alertTitle, const std::function<bool (const API_PropertyGroup&)>& condition)
{
	GS::Array<API_PropertyGroup> groups;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));

	GS::UniString string;
	UIndex firstMatchingGroupIndex = groups.FindFirst (condition);
	if (firstMatchingGroupIndex != MaxUIndex) {
		API_PropertyGroup group;
		group.guid = groups[firstMatchingGroupIndex].guid;
		ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroup (group));
		string += "Group: " + group.name + "\n\n";
		GS::Array<API_PropertyDefinition> definitions;
		ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (group.guid, definitions));

		string += PropertyDefinitionsToString (definitions);
	}

	ShowReport (alertTitle,
				string,
				"No groups found.");

	return NoError;
}


static GSErrCode ListAllPropertiesOfFirstBuiltInGroup ()
{
	return ListAllPropertiesOfFirstGroup ("All properties of First Built-in Group", [&] (const API_PropertyGroup& group) {
		return group.groupType != API_PropertyCustomGroupType;
	});
}


static GSErrCode ListAllPropertiesOfFirstCustomGroup ()
{
	return ListAllPropertiesOfFirstGroup ("All properties of First Custom Group", [&] (const API_PropertyGroup& group) {
		return group.groupType == API_PropertyCustomGroupType;
	});
}


static GSErrCode ListVisiblePropertiesOfCurrentDefaultElemByFilter (const GS::UniString& title, API_PropertyDefinitionFilter filter)
{
	API_ToolBoxItem tboxInfo = {};
	GSErrCode errCode = ACAPI_Toolbox_GetToolBoxMode (&tboxInfo);
	if (errCode != NoError) {
		return errCode;
	}

	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyDefinitionsOfDefaultElem (tboxInfo.type, filter, definitions));
	GS::Array<API_Guid> propertyDefinitionList;
	for (UInt32 i = 0; i < definitions.GetSize (); i++) {
		if (ACAPI_Element_IsPropertyDefinitionVisibleDefault (tboxInfo.type, definitions[i].guid)) {
			propertyDefinitionList.Push (definitions[i].guid);
		}
	}
	GS::Array<API_Property> properties;
	ASSERT_NO_ERROR (ACAPI_Element_GetPropertyValuesOfDefaultElemByGuid (tboxInfo.type, propertyDefinitionList, properties));

	ShowReport (title,
				PropertyValuesToString (properties),
				"No properties found.");

	return NoError;
}


static GSErrCode ListVisiblePropertiesOfCurrentDefaultElem ()
{
	return ListVisiblePropertiesOfCurrentDefaultElemByFilter ("Visible properties of the current default elem", API_PropertyDefinitionFilter_All);
}

static GSErrCode ListVisibleUserDefinedPropertiesOfCurrentDefaultElem ()
{
	return ListVisiblePropertiesOfCurrentDefaultElemByFilter ("Visible user defined properties of the current default elem", API_PropertyDefinitionFilter_UserDefined);
}


static GSErrCode ListVisibleUserLevelBuiltInPropertiesOfCurrentDefaultElem ()
{
	return ListVisiblePropertiesOfCurrentDefaultElemByFilter ("Visible built-in properties of the current default elem", API_PropertyDefinitionFilter_BuiltIn);
}


static GSErrCode SetAllStringPropertiesToAbcdForCurrentDefaultElem ()
{
	API_ToolBoxItem tboxInfo = {};
	GSErrCode errCode = ACAPI_Toolbox_GetToolBoxMode (&tboxInfo);
	if (errCode != NoError) {
		return errCode;
	}

	GS::Array<API_PropertyDefinition> definitions;
	ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, definitions));

	for (UIndex i = 0; i < definitions.GetSize (); ++i) {
		if (definitions[i].canValueBeEditable &&
			definitions[i].collectionType == API_PropertySingleCollectionType &&
			definitions[i].valueType == API_PropertyStringValueType)
		{
			bool isAvailable = ACAPI_Element_IsPropertyDefinitionAvailableDefault (tboxInfo.type, definitions[i].guid);
			bool isValueEditable = ACAPI_Element_IsPropertyDefinitionValueEditableDefault (tboxInfo.type, definitions[i].guid);
			if (isValueEditable && DBVERIFY (isAvailable)) {
				API_Property property;
				property.definition = definitions[i];
				property.value.singleVariant.variant.type = property.definition.valueType;
				property.value.singleVariant.variant.uniStringValue = "abcd";
				property.isDefault = false;
				ASSERT_NO_ERROR (ACAPI_Element_SetPropertyOfDefaultElem (tboxInfo.type, property));
			}
		}
	}
	return NoError;
}


static GSErrCode ExampleForDocumentation_GetAvailablePropertyDefinitions (const API_ElemType& type, GS::Array<API_PropertyDefinition>& availableProperties)
{
	GS::Array<API_PropertyDefinition> definitions;
	GSErrCode error = ACAPI_Element_GetPropertyDefinitionsOfDefaultElem (type, API_PropertyDefinitionFilter_UserDefined, definitions);
	if (error == NoError) {
		for (UInt32 i = 0; i < definitions.GetSize (); ++i) {
			if (ACAPI_Element_IsPropertyDefinitionValueEditableDefault (type, definitions[i].guid)) {
				availableProperties.Push (definitions[i]);
			}
		}
	}
	return error;
}

static GSErrCode SetAllStringPropertiesToAbcdForCurrentDefaultElem_ExampleForDocumentation ()
{
	API_ToolBoxItem tboxInfo = {};
	GSErrCode errCode = ACAPI_Toolbox_GetToolBoxMode (&tboxInfo);
	if (errCode != NoError) {
		return errCode;
	}

	GS::Array<API_PropertyDefinition> definitions;
	errCode = ExampleForDocumentation_GetAvailablePropertyDefinitions (tboxInfo.type, definitions);
	if (errCode != NoError) {
		return errCode;
	}

	for (UIndex i = 0; i < definitions.GetSize (); ++i) {
		if (definitions[i].definitionType == API_PropertyCustomDefinitionType &&
			definitions[i].collectionType == API_PropertySingleCollectionType &&
			definitions[i].valueType == API_PropertyStringValueType) 
		{
			API_Property property;
			property.definition = definitions[i];
			property.value.singleVariant.variant.type = property.definition.valueType;
			property.value.singleVariant.variant.uniStringValue = "abcd";
			property.isDefault = false;
			ASSERT_NO_ERROR (ACAPI_Element_SetPropertyOfDefaultElem (tboxInfo.type, property));
		}
	}
	return NoError;
}


static GSErrCode WriteAllBuiltInPropertiesToXMLDocument (xercesc::DOMDocument*& document)
{
	GSErrCode err = NoError;

	document = xercesc::DOMImplementation::getImplementation ()->createDocument (nullptr, UNISTR_TO_XMLCH_PTR (XMLTag_BuildingInformation), nullptr);
	if (DBERROR (document == nullptr))
		return Error;

	try {
		xercesc::DOMElement* rootTag = document->getDocumentElement ();
		if (rootTag == nullptr)
			throw 1;

		xercesc::DOMElement* groupsElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_BuiltInPropertyDefinitionGroups));
		GSXMLUtils::WriteNewLine (rootTag);
		GSXMLUtils::WriteTabs (rootTag, 1);
		rootTag->appendChild (groupsElement);

		GS::Array<API_PropertyGroup> groups;
		ASSERT_NO_ERROR (ACAPI_Property_GetPropertyGroups (groups));

		groups.EnumerateConst ([&](const API_PropertyGroup& group) {
			if (group.groupType != API_PropertyCustomGroupType) {
				xercesc::DOMElement* groupElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_BuiltInPropertyDefinitionGroup));
				GSXMLUtils::WriteNewLine (groupsElement);
				GSXMLUtils::WriteTabs (groupsElement, 2);
				groupsElement->appendChild (groupElement);

				xercesc::DOMElement* groupNameElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_Name));
				GSXMLUtils::WriteNewLine (groupElement);
				GSXMLUtils::WriteTabs (groupElement, 3);
				groupElement->appendChild (groupNameElement);
				groupNameElement->appendChild (document->createTextNode (UNISTR_TO_XMLCH_PTR (GS::UniString (group.name))));

				xercesc::DOMElement* groupGuidElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_Guid));
				GSXMLUtils::WriteNewLine (groupElement);
				GSXMLUtils::WriteTabs (groupElement, 3);
				groupElement->appendChild (groupGuidElement);
				GS::Guid guid = APIGuid2GSGuid (group.guid);
				groupGuidElement->appendChild (document->createTextNode (UNISTR_TO_XMLCH_PTR (guid.ToUniString ())));

				xercesc::DOMElement* definitionsElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_BuiltInPropertyDefinitions));
				GSXMLUtils::WriteNewLine (groupElement);
				GSXMLUtils::WriteTabs (groupElement, 3);
				groupElement->appendChild (definitionsElement);

				GS::Array<API_PropertyDefinition> definitions;
				ASSERT_NO_ERROR (ACAPI_Property_GetPropertyDefinitions (group.guid, definitions));

				definitions.EnumerateConst ([&](const API_PropertyDefinition& definition) {
					xercesc::DOMElement* definitionElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_BuiltInPropertyDefinition));
					GSXMLUtils::WriteNewLine (definitionsElement);
					GSXMLUtils::WriteTabs (definitionsElement, 4);
					definitionsElement->appendChild (definitionElement);

					xercesc::DOMElement* definitionNameElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_Name));
					GSXMLUtils::WriteNewLine (definitionElement);
					GSXMLUtils::WriteTabs (definitionElement, 5);
					definitionElement->appendChild (definitionNameElement);
					definitionNameElement->appendChild (document->createTextNode (UNISTR_TO_XMLCH_PTR (GS::UniString (definition.name))));

					xercesc::DOMElement* definitionGuidElement = document->createElement (UNISTR_TO_XMLCH_PTR (XMLTag_Guid));
					GSXMLUtils::WriteNewLine (definitionElement);
					GSXMLUtils::WriteTabs (definitionElement, 5);
					definitionElement->appendChild (definitionGuidElement);
					GS::Guid definitionGuid = APIGuid2GSGuid (definition.guid);
					definitionGuidElement->appendChild (document->createTextNode (UNISTR_TO_XMLCH_PTR (definitionGuid.ToUniString ())));

					GSXMLUtils::WriteNewLine (definitionElement);
					GSXMLUtils::WriteTabs (definitionElement, 4);
				});

				GSXMLUtils::WriteNewLine (definitionsElement);
				GSXMLUtils::WriteTabs (definitionsElement, 3);

				GSXMLUtils::WriteNewLine (groupElement);
				GSXMLUtils::WriteTabs (groupElement, 2);
			}
		});

		GSXMLUtils::WriteNewLine (groupsElement);
		GSXMLUtils::WriteTabs (groupsElement, 1);

		GSXMLUtils::WriteNewLine (rootTag);

	} catch (...) {
		err = Error;
	}

	if (err != NoError) {
		document->release ();
		document = nullptr;
	}

	return err;
}


static GSErrCode ExportAllBuiltInPropertiesToXMLFile ()
{
	GSErrCode err = NoError;
	xercesc::DOMDocument* document = nullptr;

	err = WriteAllBuiltInPropertiesToXMLDocument (document);
	if (err != NoError)
		return err;

	DG::FileDialog dialog (DG::FileDialog::Save);

	dialog.SetTitle ("Select destination file");

	FTM::FileTypeManager manager ("XML files");
	FTM::FileType type (nullptr, "xml", 0, 0, 0);
	FTM::TypeID id = FTM::FileTypeManager::SearchForType (type);

	if (id == FTM::UnknownType) {
		id = manager.AddType (type);
	}

	dialog.AddFilter (id);

	IO::Location location;
	if (dialog.Invoke ()) {
		location = dialog.GetSelectedFile ();
	} else {
		return Cancel;
	}

	IO::File xmlFile (location, IO::File::Create);
	err = xmlFile.Open (IO::File::WriteEmptyMode, IO::AccessDeniedIsError);

	if (err != NoError) {
		document->release ();
		return Error;
	}

	err = FormatXMLDoc (document, &xmlFile) ? NoError : Error;

	document->release ();
	xmlFile.Close ();

	return NoError;
}


static bool IsSupportedPropertyDefinitionTypeForConversion (const API_PropertyDefinition& definition)
{
	const bool isSupportedSingleCollectionType =	definition.collectionType == API_PropertySingleCollectionType &&
													definition.valueType != API_PropertyUndefinedValueType &&
													definition.valueType != API_PropertyGuidValueType;
	const bool isSupportedListOrChoiceCollectionType =	(definition.collectionType == API_PropertyListCollectionType ||
														 definition.collectionType == API_PropertySingleChoiceEnumerationCollectionType ||
														 definition.collectionType == API_PropertyMultipleChoiceEnumerationCollectionType) &&
														definition.valueType == API_PropertyStringValueType;
	return	isSupportedSingleCollectionType || isSupportedListOrChoiceCollectionType;
}


static GS::HashTable<API_Guid, GS::HashTable<API_Guid, GS::UniString>> propertyValueStringsForDefinitionGuidForElement;

static void SavePropertyValuesOfElement (const API_Guid& elemGuid)
{
	GS::Array<API_PropertyDefinition> definitions;
	GSErrCode err = ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_All, definitions);
	if (err != NoError) {
		return;
	}

	GS::Array<API_Property> properties;
	err = ACAPI_Element_GetPropertyValues (elemGuid, definitions, properties);
	if (err != NoError) {
		return;
	}

	GS::HashTable<API_Guid, GS::UniString> propertyValueStringForDefinitionGuid;
	for (API_Property& property : properties) {
		if (!IsSupportedPropertyDefinitionTypeForConversion (property.definition) ||
			property.status != API_Property_HasValue ||
			property.value.variantStatus == API_VariantStatusUserUndefined)
		{
			continue;
		}

		GS::UniString propertyValueString;
		err = ACAPI_Property_GetPropertyValueString (property, &propertyValueString);
		if (err != NoError ||
			(property.definition.valueType != API_PropertyStringValueType &&
			 propertyValueString.IsEmpty ()))
		{
			ACAPI_WriteReport ("Failed to save property value for definition (%T) for element: %T",
							   false,
							   APIGuidToString (property.definition.guid).ToPrintf (),
							   APIGuidToString (elemGuid).ToPrintf ());
			continue;
		}

		propertyValueStringForDefinitionGuid.Add (property.definition.guid, propertyValueString);
	}

	propertyValueStringsForDefinitionGuidForElement.Put (elemGuid, propertyValueStringForDefinitionGuid);
}


class PropertyConversionUtils : public API_PropertyConversionUtilsInterface {
private:
	const GS::UniString degreeSymbol = L("\u00B0");
	const GS::UniString minuteSymbol = "'";
	const GS::UniString secondSymbol = "\"";
	const GS::UniString gradientSymbol = "G";
	const GS::UniString radianSymbol = "R";
	const GS::UniString northSymbol = "N";
	const GS::UniString southSymbol = "S";
	const GS::UniString eastSymbol = "E";
	const GS::UniString westSymbol = "w";

public:
	PropertyConversionUtils ();

	virtual ~PropertyConversionUtils ();

	virtual const GS::UniString& GetDegreeSymbol1	() const { return degreeSymbol; }
	virtual const GS::UniString& GetDegreeSymbol2	() const { return degreeSymbol; }
	virtual const GS::UniString& GetMinuteSymbol	() const { return minuteSymbol; }
	virtual const GS::UniString& GetSecondSymbol	() const { return secondSymbol; }

	virtual const GS::UniString& GetGradientSymbol	() const { return gradientSymbol; }
	virtual const GS::UniString& GetRadianSymbol	() const { return radianSymbol; }

	virtual const GS::UniString& GetNorthSymbol () const { return northSymbol; }
	virtual const GS::UniString& GetSouthSymbol () const { return southSymbol; }
	virtual const GS::UniString& GetEastSymbol	() const { return eastSymbol; }
	virtual const GS::UniString& GetWestSymbol	() const { return westSymbol; }

	virtual GS::uchar_t					GetDecimalDelimiterChar		() const { return '.'; }
	virtual GS::Optional<GS::UniChar>	GetThousandSeparatorChar	() const { return ' '; }

	virtual API_LengthTypeID	GetLengthType	() const { return API_LengthTypeID::Meter; }
	virtual API_AreaTypeID		GetAreaType		() const { return API_AreaTypeID::SquareMeter; }
	virtual API_VolumeTypeID	GetVolumeType	() const { return API_VolumeTypeID::CubicMeter; }
	virtual API_AngleTypeID		GetAngleType	() const { return API_AngleTypeID::DecimalDegree; }
};


PropertyConversionUtils::PropertyConversionUtils () = default;


PropertyConversionUtils::~PropertyConversionUtils () = default;


static void LoadPropertyValuesOfElement (const API_Guid& elemGuid)
{
	const PropertyConversionUtils conversionUtils;
	for (const auto& it : propertyValueStringsForDefinitionGuidForElement.Retrieve (elemGuid)) {
		const API_Guid& definitionGuid = it.key;
		const GS::UniString& propertyValueString = it.value;

		API_Property property = {};
		GSErrCode err = ACAPI_Element_GetPropertyValue (elemGuid, definitionGuid, property);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_GetPropertyValue", err);
			continue;
		}

		if (!ACAPI_Element_IsPropertyDefinitionValueEditable (elemGuid, property.definition.guid) ||
			!ACAPI_Element_IsPropertyDefinitionVisible (elemGuid, property.definition.guid))
		{
			continue;
		}

		err = ACAPI_Property_SetPropertyValueFromString (propertyValueString, conversionUtils, &property);
		if (err != NoError) {
			ACAPI_WriteReport ("Failed to set property value for definition: %T",
							   false,
							   property.definition.name.ToPrintf ());
			continue;
		}

		err = ACAPI_Element_SetProperty (elemGuid, property);
		if (err != NoError) {
			ACAPI_WriteReport ("Failed to set property value for definition: %T",
							   false,
							   property.definition.name.ToPrintf ());
		}
	}
}


// -----------------------------------------------------------------------------
// Add-on entry point definition
// -----------------------------------------------------------------------------
GSErrCode APIMenuCommandProc_Main (const API_MenuParams *menuParams)
{
	if (menuParams->menuItemRef.menuResID == 32500) {
		return ACAPI_CallUndoableCommand ("Property Test API Function",
			[&] () -> GSErrCode {
				try {
					switch (menuParams->menuItemRef.itemIndex) {
						case  1: PropertyTestHelpers::CallOnSelectedElem (DefineNewBoolProperty); break;
						case  2: PropertyTestHelpers::CallOnSelectedElem (DefineNewStringListProperty); break;
						case  3: PropertyTestHelpers::CallOnSelectedElem (DefineStringMultiEnumtProperty); break;
						case  4: break; // "-"
						case  5: PropertyTestHelpers::CallOnSelectedElem (SetAllPropertiesDefault); break;
						case  6: PropertyTestHelpers::CallOnSelectedElem (DeleteAllProperties); break;
						case  7: break; // "-"
						case  8: SelectionProperties::DefineNewIntProperty (); break;
						case  9: SelectionProperties::SetAllIntPropertiesTo42 (); break;
						case 10: SelectionProperties::SetAllIntPropertiesTo42_ExampleForDocumentation (); break;
						case 11: break; // "-"
						case 12: SelectionProperties::SetAllStringPropertiesToAbcd (); break;
						case 13: SetAllStringPropertiesToAbcdForCurrentDefaultElem (); break;
						case 14: SetAllStringPropertiesToAbcdForCurrentDefaultElem_ExampleForDocumentation (); break;
						case 15: break; // "-"
						case 16: ListAllBuiltInProperties (); break;
						case 17: ListAllCustomProperties (); break;
						case 18: PropertyTestHelpers::CallOnAllSelectedElems (ListPropertiesWithIntegerDataType); break;
						case 19: break; // "-"
						case 20: ListAllBuiltInPropertyGroups (); break;
						case 21: ListAllCustomPropertyGroups (); break;
						case 22: ListAllPropertiesOfFirstBuiltInGroup (); break;
						case 23: ListAllPropertiesOfFirstCustomGroup (); break;
						case 24: break; // "-"
						case 25: PropertyTestHelpers::CallOnSelectedElem (ListVisibleProperties); break;
						case 26: PropertyTestHelpers::CallOnSelectedElem (ListVisibleUserDefinedProperties); break;
						case 27: PropertyTestHelpers::CallOnSelectedElem (ListVisibleUserLevelBuiltInProperties); break;
						case 28: ListVisiblePropertiesOfCurrentDefaultElem (); break;
						case 29: ListVisibleUserDefinedPropertiesOfCurrentDefaultElem (); break;
						case 30: ListVisibleUserLevelBuiltInPropertiesOfCurrentDefaultElem (); break;
						case 31: break; // "-"
						case 32: ListAllPropertiesOfAlphabeticallyFirstBuildingMaterial (); break;
						case 33: ListAllUserDefinedPropertiesOfAlphabeticallyFirstBuildingMaterial (); break;
						case 34: ListAllUserLevelBuiltInPropertiesOfAlphabeticallyFirstBuildingMaterial (); break;
						case 35: ListAllVisibleUserDefinedPropertiesOfAlphabeticallyFirstBuildingMaterial (); break;
						case 36: ListAllVisibleUserLevelBuiltInPropertiesOfAlphabeticallyFirstBuildingMaterial (); break;
						case 37: break; // "-"
						case 38: ExportAllBuiltInPropertiesToXMLFile (); break;
						case 39: break; // "-"
						case 40: PropertyTestHelpers::CallOnSelectedElem (WatchElement); break;
						case 41: PropertyTestHelpers::CallOnSelectedElem (UnwatchElement); break;
						case 42: break; // "-"
						case 43: PropertyTestHelpers::CallOnAllSelectedElems (SavePropertyValuesOfElement); break;
						case 44: PropertyTestHelpers::CallOnAllSelectedElems (LoadPropertyValuesOfElement); break;
						case 45: break; // "-"
						case 46: PropertyTestHelpers::CallOnAllSelectedElems (RunTestsOnElem); break;
						case 47: RunTestsOnBuildingMaterial (); break;
						default: ASSERT (false); break;
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
