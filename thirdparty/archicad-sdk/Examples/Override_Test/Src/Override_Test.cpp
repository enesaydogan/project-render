// *****************************************************************************
// File:			Override_Test.cpp
// Description:		Override_Test add-on test functions
// Project:			APITools/Override_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

// --- Includes ----------------------------------------------------------------

#include "VectorImage.hpp"
#include "VectorImageIterator.hpp"

#include "UserItemDialog.hpp"

#include "Override_Test.hpp"
#include "Override_Test_Helpers.hpp"
#include "APICommon.h"
#include "ProfileAdditionalInfo.hpp"

#include "BuiltInLibrary.hpp"
#include "API_AttributeIndexUtils.hpp"

// -----------------------------------------------------------------------------
// Test functions
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// This test shows common use-cases of the visual override API
// -----------------------------------------------------------------------------
static void TestNormalUsage ()
{
	{ // ensure that the data that should be created by the test do not exist
		API_OverrideCombination combination = {APINULLGuid, "TestCombination"};
		if (ACAPI_GraphicalOverride_GetOverrideCombination (combination, nullptr) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid));
		}
		combination.name = "TestCombination Renamed";
		if (ACAPI_GraphicalOverride_GetOverrideCombination (combination, nullptr) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid));
		}
		combination.name = "TestCombination2";
		if (ACAPI_GraphicalOverride_GetOverrideCombination (combination, nullptr) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid));
		}
		
//![ACAPI_GraphicalOverride_DeleteOverrideRuleGroup-Example-Snippet]
		API_OverrideRuleGroup ruleGroup { APINULLGuid, "TestGroup1" };
		if (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRuleGroup (ruleGroup.guid));
		}
//![ACAPI_GraphicalOverride_DeleteOverrideRuleGroup-Example-Snippet]
		ruleGroup.name = "TestGroup2";
		if (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRuleGroup (ruleGroup.guid));
		}
		ruleGroup.name = "TestGroup1 - Renamed";
		if (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRuleGroup (ruleGroup.guid));
		}
		// rules can't exist if their groups don't
	}

	// Create an empty combination using the ACAPI_GraphicalOverride_CreateOverrideCombination
	API_OverrideCombination combination = {APINULLGuid, "TestCombination"};
	GS::Array<API_Guid> ruleList;
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList));
	// the create method generated a guid
	ASSERT (combination.guid != APINULLGuid);

	// We can retrieve all of the combinations' guids with ACAPI_GraphicalOverride_GetOverrideCombinationList
	GS::Array<API_Guid> combinationList;
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombinationList (combinationList));
	// It will contain the combination that we have previously
	ASSERT (combinationList.Contains (combination.guid));

	// A combination's name can be retrieved knowing its guid using the
	// ACAPI_GraphicalOverride_GetOverrideCombination function.
	API_OverrideCombination combination2 = {combination.guid, ""};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombination (combination2, nullptr));
	ASSERT (combination == combination2);

	// A combination's name or its rules can be changed using ACAPI_GraphicalOverride_ChangeOverrideCombination
	combination.name = "TestCombination Renamed";
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_ChangeOverrideCombination (combination, nullptr));

	// A combination's guid can be retrieved knowing its name too
	API_OverrideCombination combination3 = {APINULLGuid, combination.name};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombination (combination3, nullptr));
	ASSERT (combination == combination3);

	// If both the combination's name and guid is set, then guid wins
	API_OverrideCombination combination4 = {combination.guid, "foobar"};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombination (combination4, nullptr));
	ASSERT (combination == combination4);
	
//![ACAPI_GraphicalOverride_CreateOverrideRuleGroup-Example-Snippet]
	// Create an empty rule group using the ACAPI_GraphicalOverride_CreateOverrideRuleGroup function
	API_OverrideRuleGroup ruleGroup1 = {APINULLGuid, "TestGroup1"};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRuleGroup (ruleGroup1));
	// the create method generated a guid
	ASSERT (ruleGroup1.guid != APINULLGuid);
//![ACAPI_GraphicalOverride_CreateOverrideRuleGroup-Example-Snippet]

	// Create another empty rule group using the ACAPI_GraphicalOverride_CreateOverrideRuleGroup function
	API_OverrideRuleGroup ruleGroup2 = {APINULLGuid, "TestGroup2"};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRuleGroup (ruleGroup2));
	// the create method generated a guid
	ASSERT (ruleGroup2.guid != APINULLGuid);
	
//![ACAPI_GraphicalOverride_ChangeOverrideRuleGroup-Example-Snippet]
	// We can rename a rule group using the ACAPI_GraphicalOverride_ChangeOverrideRuleGroup function
	ruleGroup1.name = "TestGroup1 - Renamed";
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_ChangeOverrideRuleGroup (ruleGroup1));
	ASSERT (ruleGroup1.guid != APINULLGuid);
	ASSERT (ruleGroup1.name == "TestGroup1 - Renamed");
//![ACAPI_GraphicalOverride_ChangeOverrideRuleGroup-Example-Snippet]
	
//![ACAPI_GraphicalOverride_GetOverrideRuleGroupList-Example-Snippet]
	// We can query the ordered list of all rule groups using the ACAPI_GraphicalOverride_GetOverrideRuleGroupList function
	GS::Array<API_Guid> ruleGroups;
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleGroupList (ruleGroups));
	ASSERT (ruleGroups.GetSize () >= 2);
	ASSERT (ruleGroups[ruleGroups.GetSize () - 2] == ruleGroup1.guid);
	ASSERT (ruleGroups[ruleGroups.GetSize () - 1] == ruleGroup2.guid);
//![ACAPI_GraphicalOverride_GetOverrideRuleGroupList-Example-Snippet]

//![ACAPI_GraphicalOverride_SetOverrideRuleGroupOrder-Example-Snippet]
	// Switch the last two and apply the new order
	ruleGroups[ruleGroups.GetSize () - 2] = ruleGroup2.guid;
	ruleGroups[ruleGroups.GetSize () - 1] = ruleGroup1.guid;
	// We can reorder the rule groups using the ACAPI_GraphicalOverride_SetOverrideRuleGroupOrder function
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_SetOverrideRuleGroupOrder (ruleGroups));
//![ACAPI_GraphicalOverride_SetOverrideRuleGroupOrder-Example-Snippet]

	// Now the rule group list should reflect the new order
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleGroupList (ruleGroups));
	ASSERT (ruleGroups.GetSize () >= 2);
	ASSERT (ruleGroups[ruleGroups.GetSize () - 2] == ruleGroup2.guid);
	ASSERT (ruleGroups[ruleGroups.GetSize () - 1] == ruleGroup1.guid);

	// We can get all the rules' guids (including the built in ones), with
	// the ACAPI_GraphicalOverride_GetOverrideRuleList function.
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleList (ruleList));

	// we can easily create a new rule based on an existing one
	if (ruleList.GetSize () > 0) {
//![ACAPI_GraphicalOverride_CreateOverrideRule-Example-Snippet]
		// Get the data of a rule using ACAPI_GraphicalOverride_GetOverrideRule knowing its guid
		API_OverrideRule rule = {ruleList[0]};
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleById (rule));
		
		// A new rule can be created using the ACAPI_GraphicalOverride_CreateOverrideRule function.
		API_OverrideRule newRule = {APINULLGuid, "Override_Test Add-on's Rule", rule.style, ""};
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRule (newRule, ruleGroup1.guid));
//![ACAPI_GraphicalOverride_CreateOverrideRule-Example-Snippet]

		// The list of all rules will contain the newly created rule
		ruleList.Clear ();
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleList (ruleList));
		ASSERT (ruleList.Contains (newRule.guid));
		
//![ACAPI_GraphicalOverride_GetOverrideRuleById-Example-Snippet]
		// After that we can retrieve that rule using ACAPI_GraphicalOverride_GetOverrideRule
		API_OverrideRule newRule2 = {newRule.guid};
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleById (newRule2));
//![ACAPI_GraphicalOverride_GetOverrideRuleById-Example-Snippet]
		// Although the empty criterionXML means a criteria that fits all elements,
		// when used in ACAPI_GraphicalOverride_CreateOverrideRule, the ACAPI_GraphicalOverride_GetOverrideRule
		// call fills this field with the actual xml of that criteria.
		newRule.criterionXML = newRule2.criterionXML;
		ASSERT (newRule == newRule2);

		// An existing rule's data can be changed using ACAPI_GraphicalOverride_ChangeOverrideRule.
		newRule.name = "Override_Test Add-on's Rule Renamed";
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_ChangeOverrideRule (newRule));
		
//![ACAPI_GraphicalOverride_GetOverrideRuleByName-Example-Snippet]
		// We can do that also using the rule's name
		API_OverrideRule newRule3 = {APINULLGuid, newRule.name};
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleByName (newRule3, ruleGroup1.guid));
		ASSERT (newRule == newRule3);
//![ACAPI_GraphicalOverride_GetOverrideRuleByName-Example-Snippet]

		API_OverrideCombination combination2 = {APINULLGuid, "TestCombination2"};
		// If the second parameter of ACAPI_GraphicalOverride_CreateOverrideCombination is not an empty
		// array of guid, then those rules whose guids are in the array will be contained by
		// the combination.
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideCombination (combination2, ruleList));

		// We can retrieve the contained rules of a combination
		// if we specify the second parameter of ACAPI_GraphicalOverride_GetOverrideCombination
		GS::Array<API_Guid> ruleList2;
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombination (combination, &ruleList2));
		ASSERT (ruleList2.IsEmpty ());
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombination (combination2, &ruleList2));
		ASSERT (ruleList == ruleList2);

		// Create another rule for testing purposes so we can demonstrate rule ordering
		API_OverrideRule anotherTestRule = {APINULLGuid, "Another Test Rule", rule.style, ""};
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRule (anotherTestRule, ruleGroup1.guid));
		
//![ACAPI_GraphicalOverride_GetOverrideRuleGroup-Example-Snippet]
		// We can retrieve the contained rules of a rule group with the ACAPI_GraphicalOverride_GetOverrideRuleGroup function
		// if we specify an array of API_Guids for the contained rules
		API_OverrideRuleGroup queriedRuleGroup = { ruleGroup1.guid };
		GS::Array<API_Guid> rulesInGroup1;
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleGroup (queriedRuleGroup, &rulesInGroup1));
		ASSERT (queriedRuleGroup.name == ruleGroup1.name);
		ASSERT (rulesInGroup1.GetSize () == 2);
		ASSERT (rulesInGroup1[0] == newRule.guid);
		ASSERT (rulesInGroup1[1] == anotherTestRule.guid);
//![ACAPI_GraphicalOverride_GetOverrideRuleGroup-Example-Snippet]

//![ACAPI_GraphicalOverride_SetOverrideRuleOrder-Example-Snippet]
		// We can reorder the rules in a rule group using the ACAPI_GraphicalOverride_SetOverrideRuleOrder function
		rulesInGroup1[0] = anotherTestRule.guid;
		rulesInGroup1[1] = newRule.guid;
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_SetOverrideRuleOrder (ruleGroup1.guid, rulesInGroup1));
		rulesInGroup1.Clear ();
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup1, &rulesInGroup1));
		ASSERT (rulesInGroup1.GetSize () == 2);
		ASSERT (rulesInGroup1[0] == anotherTestRule.guid);
		ASSERT (rulesInGroup1[1] == newRule.guid);
//![ACAPI_GraphicalOverride_SetOverrideRuleOrder-Example-Snippet]
		
//![ACAPI_GraphicalOverride_MoveOverrideRule-Example-Snippet]
		// We can move a rule from a rule group to a target rule group using the ACAPI_GraphicalOverride_MoveOverrideRule function
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_MoveOverrideRule (newRule.guid, ruleGroup2.guid, nullptr));

		// Checking that the move was successful (group 1 is empty, group 2 contains the moved rule)
		rulesInGroup1.Clear ();
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup1, &rulesInGroup1));
		ASSERT (rulesInGroup1.GetSize () == 1);
		ASSERT (rulesInGroup1[0] == anotherTestRule.guid);

		GS::Array<API_Guid> rulesInGroup2;
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup2, &rulesInGroup2));
		ASSERT (rulesInGroup2.GetSize () == 1);
		ASSERT (rulesInGroup2[0] == newRule.guid);
//![ACAPI_GraphicalOverride_MoveOverrideRule-Example-Snippet]

		// A rule can be deleted using the ACAPI_GraphicalOverride_DeleteOverrideRule function
		ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRule (newRule.guid));
	}

	// A rule group can be deleted using the ACAPI_GraphicalOverride_DeleteOverrideRuleGroup function
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRuleGroup (ruleGroup1.guid));

	// A combination can be deleted using the ACAPI_GraphicalOverride_DeleteOverrideRule function
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid));
}


// ------------------------------------------------------------------------------------
// This test shows what error causes the specific error codes using visual override API
// ------------------------------------------------------------------------------------
static void TestErrorCodes ()
{
	{ // ensure that the data this test should create do not exist
		API_OverrideCombination combination = {APINULLGuid, "Just another non-sense override combination name (at least it is likely to be unique)."};
		if (ACAPI_GraphicalOverride_GetOverrideCombination (combination, nullptr) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid));
		}
		
		API_OverrideRuleGroup ruleGroup = {APINULLGuid, "TestGroup1"};
		if (ACAPI_GraphicalOverride_GetOverrideRuleGroup (ruleGroup) == NoError) {
			ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRuleGroup (ruleGroup.guid));
		}
	}

	// Creating a combination with an empty name causes APIERR_BADNAME
	GS::Array<API_Guid> ruleList;
	API_OverrideCombination combination = {APINULLGuid, ""};
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList) == APIERR_BADNAME);

	// Creating a combination with a whitespace only name also causes APIERR_BADNAME
	combination.name = " \t\n ";
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList) == APIERR_BADNAME);

	// A combination name longer than 255 characters also triggers APIERR_BADNAME
	GS::UniString tooLongName;
	for (USize i = 0; i < 1000; ++i) {
		tooLongName += "a";
	}
	combination.name = tooLongName;
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList) == APIERR_BADNAME);

	// Everything else is considered valid
	combination.name = "Just another non-sense override combination name (at least it is likely to be unique).";

	// adding an invalid rule guid to the rule list also causes an APIERR_BADID error
	ruleList.Push(OverrideTestHelpers::RandomGuid ());
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList) == APIERR_BADID);

	// If the name and the rule list are both valid, then the ACAPI_GraphicalOverride_CreateOverrideCombination will succeed
	ruleList.Clear ();
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList));
	ASSERT (combination.guid != APINULLGuid);

	// However, names are required to be unique. An attempt to violate this will cause APIERR_NAMEALREADYUSED
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList) == APIERR_NAMEALREADYUSED);

	// The functions that return with an error do not modify ARCHICAD's state or the function's
	// parameters, so the result of the previous calls is the same as calling only the one function,
	// that returned with a GS::NoError
	GS::Array<API_Guid> combinationList;
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombinationList (combinationList));
	ASSERT (combinationList.Contains (combination.guid));

	// Trying to get the data of a rule, without an invalid guid, or a null guid and an invalid name causes APIERR_BADID
	API_OverrideCombination combination2 = {APINULLGuid, ""};
	ASSERT (ACAPI_GraphicalOverride_GetOverrideCombination (combination2) == APIERR_BADID);

	// All of the other cases work with ACAPI_GraphicalOverride_GetOverrideCombination
	API_OverrideCombination combination3 = {APINULLGuid, combination.name};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideCombination (combination3));
	ASSERT (combination == combination3);

	// The naming rule groups are the same override rules
	
	// valid rule group for testing
	API_OverrideRuleGroup ruleGroup1 = {APINULLGuid, "TestGroup1"};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRuleGroup (ruleGroup1));
	ASSERT (ruleGroup1.guid != APINULLGuid);

	// Empty rule group name is invalid
	API_OverrideRuleGroup invalidRuleGroup = {APINULLGuid, ""};
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideRuleGroup (invalidRuleGroup) == APIERR_BADNAME);

	// Rule groups must have unique names
	API_OverrideRuleGroup alreadyExistingRuleGroup = {APINULLGuid, "TestGroup1"};
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideRuleGroup (alreadyExistingRuleGroup) == APIERR_NAMEALREADYUSED);

	// The naming rules are the same override rules

	// Empty rule name is invalid
	API_OverrideRule rule = {APINULLGuid, ""};
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideRule (rule, ruleGroup1.guid) == APIERR_BADNAME);

	// Whitespace only rule is also invalid
	rule.name = "\t\n    \t";
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideRule (rule, ruleGroup1.guid) == APIERR_BADNAME);

	// And valid names' length must not exceed 255 characters
	rule.name = tooLongName;
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideRule (rule, ruleGroup1.guid) == APIERR_BADNAME);

	// A name that is taken as combination name can be also used as rule name.
	rule.name = combination.name;

	// Let's create a simple criterion XML (saved from ARCHICAD)
	rule.criterionXML =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>"
		"<CriterionExpression Mv=\"2\" Sv=\"0\">"
			"<TextMatchType>1</TextMatchType>"
			"<Size>1</Size>"
			"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
			"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
				"<LogicalOperator>2</LogicalOperator>"
				"<CriteriaCount>1</CriteriaCount>"
				"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
				"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
					"<LogicalOperator>1</LogicalOperator>"
					"<CriteriaCount>1</CriteriaCount>"
					"<ClassGuid>B4B7B134-EC56-4D40-8D4C-71D7C5A2493A</ClassGuid>"
					"<ElemTypeCriterion Mv=\"2\" Sv=\"3\">"
						"<VBEF::CritToolType Mv=\"1\" Sv=\"0\">"
							"<ElemRegistryRefId>0</ElemRegistryRefId>"
							"<ElemCreatorToolUnID>"
								"<MainGuid>00000000-0000-0000-0000-000000000000</MainGuid>"
								"<RevGuid>00000000-0000-0000-0000-000000000000</RevGuid>"
							"</ElemCreatorToolUnID>"
						"</VBEF::CritToolType>"
						"<LogicalOperator>1</LogicalOperator>"
					"</ElemTypeCriterion>"
				"</CompositeCriterion>"
			"</CompositeCriterion>"
		"</CriterionExpression>";

	// And also create an override rule style
	rule.style.fillBackgroundPenOverride = static_cast<short> (1);
	rule.style.fillForegroundPenOverride = static_cast<short> (2);
	rule.style.fillTypeForegroundPen.overrideCoverFill = false;
	rule.style.fillTypeForegroundPen.overrideCutFill = true;
	rule.style.fillTypeForegroundPen.overrideDraftingFill = true;
	rule.style.fillOverride = ACAPI_CreateAttributeIndex (3);
	rule.style.fillType.overrideCoverFill = true;
	rule.style.fillType.overrideCutFill = false;
	rule.style.fillType.overrideDraftingFill = true;
	rule.style.lineType = ACAPI_CreateAttributeIndex (4);
	rule.style.lineMarkerTextPen = APINullValue;
	rule.style.surfaceOverride = API_RGBColor {0.0, 0.0, 1.0};
	rule.style.surfaceType.overrideCutSurface = true;
	rule.style.surfaceType.overrideUncutSurface = false;
	rule.style.overridePenColorAndThickness = false;
	rule.style.showSkinSeparators = false;
	rule.style.hiddenContours.overrideCutSurface = true;
	rule.style.hiddenContours.overrideUncutSurface = false;
	rule.style.overrideContours = true;

	// Now the rule can be created without an error
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRule (rule, ruleGroup1.guid));
	// and a guid is generated for the new rule
	ASSERT (rule.guid != APINULLGuid);

	// An attempt to generate a rule with the same name within a rule group causes APIERR_NAMEALREADYUSED,
	// rule names have to be unique within a rule group
	ASSERT (ACAPI_GraphicalOverride_CreateOverrideRule (rule, ruleGroup1.guid) == APIERR_NAMEALREADYUSED);

	// The list of all the rules will contain the newly created one
	ruleList.Clear ();
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleList (ruleList));
	ASSERT (ruleList.Contains (rule.guid));

	// Trying to get the data of a rule, without an invalid guid, or a null guid and an invalid name causes APIERR_BADID
	API_OverrideRule rule2 = {APINULLGuid, ""};
	ASSERT (ACAPI_GraphicalOverride_GetOverrideRuleByName (rule2, ruleGroup1.guid) == APIERR_BADID);

	// All of the other cases work for ACAPI_GraphicalOverride_GetOverrideRule
	rule2.guid = rule.guid;
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_GetOverrideRuleById (rule2));

	// The retrieved criterion xml might be different (for ex. whitespaces are likely to differ)
	rule2.criterionXML = rule.criterionXML;

	// But all the other fields are equal
	ASSERT (rule2 == rule);

	// Deleting an existing rule might fail with APIERR_NOACCESSRIGHT, if called from
	// TeamWork without the proper rights. Otherwise, it will always succeed.
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideRule (rule.guid));

	// Deleting a non existent rule however fails with APIERR_BADID
	ASSERT (ACAPI_GraphicalOverride_DeleteOverrideRule (rule.guid) == APIERR_BADID);

	// The same rules apply for combinations. Deleting a combination does not affect the rules that it contained.
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid));
	ASSERT (ACAPI_GraphicalOverride_DeleteOverrideCombination (combination.guid) == APIERR_BADID);
}


// ------------------------------------------------------------------------------------
// This test shows the currently used override for a navigator view can be
// queried and modified through the overrideCombination field of API_NavigatorView
// ------------------------------------------------------------------------------------
static void TestNavigatorView ()
{
	Int32 navigatorSetCount;
	ASSERT_NO_ERROR (ACAPI_Navigator_GetNavigatorSetNum (&navigatorSetCount));
	ASSERT (navigatorSetCount > 0);

	API_NavigatorSet navigatorSet;
	BNZeroMemory (&navigatorSet, sizeof (API_NavigatorSet));
	navigatorSet.mapId = API_PublicViewMap;
	Int32 navigatorSetId = 0;
	ASSERT_NO_ERROR (ACAPI_Navigator_GetNavigatorSet (&navigatorSet, &navigatorSetId));

	API_NavigatorItem item = {};

	item.guid = navigatorSet.rootGuid;
	item.mapId = API_PublicViewMap;

	for (int i = 0; i < 2; ++i) {
		GS::Array<API_NavigatorItem> items;
		ASSERT_NO_ERROR (ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items));
		ASSERT (items.GetSize () > 0);

		item = items[0];
	}

	API_NavigatorView view;
	ASSERT_NO_ERROR (ACAPI_Navigator_GetNavigatorView (&item, &view));

	ASSERT (!GS::UniString (view.overrideCombination).IsEmpty ());

	GS::UniString newCombinationName = "Override Test Add-on Combination Name";
	GS::ucsncpy (view.overrideCombination, newCombinationName.ToUStr (), API_UniLongNameLen);

	ASSERT_NO_ERROR (ACAPI_Navigator_ChangeNavigatorView (&item, &view));

	BNZeroMemory (&view, sizeof (API_NavigatorView));
	ASSERT_NO_ERROR (ACAPI_Navigator_GetNavigatorView (&item, &view));
	ASSERT (newCombinationName.IsEqual (view.overrideCombination));
}


/*----------------------------------------------------------------**
**			Runs all the previously defined test cases.           **
**----------------------------------------------------------------*/
static GSErrCode RunOverrideTests ()
{
	try {
		TestNormalUsage ();
		TestErrorCodes ();
		TestNavigatorView ();

		DGAlert (DG_INFORMATION, "Test success", "Tests executed successfully.", "", "Ok");
		return NoError;
	} catch (GS::Exception&) {
		return Error;
	}
}


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
static GS::UniString API_OverriddenPenToString (const API_OverriddenPen& overriddenPen)
{
	GS::UniString str;
	str = GS::UniString::Printf ("%d, %s", overriddenPen.value, overriddenPen.hasValue ? "overridden" : "");
	return str;

}

static GS::UniString API_OverriddenPenOrRGBToString (const API_OverriddenPenOrRGB& overriddenPen)
{
	if (overriddenPen.value.IsType<short> ()) {
		return GS::UniString::Printf ("%d, %s", overriddenPen.value.Get<short> (), overriddenPen.hasValue ? "overridden" : "");
	} else {
		const API_RGBColor rgbColor = overriddenPen.value.Get<API_RGBColor> ();
		return GS::UniString::Printf ("[%f, %f, %f], %s", rgbColor.f_red, rgbColor.f_green, rgbColor.f_blue, overriddenPen.hasValue ? "overridden" : "");
	}
}

static GS::UniString API_OverriddenAttributeToString (const API_OverriddenAttribute& overriddenAttr)
{
	GS::UniString str;
	str = GS::UniString::Printf ("%s, %s", overriddenAttr.value.ToUniString ().ToCStr ().Get (), overriddenAttr.hasValue ? "overridden" : "");
	return str;

}

static GS::UniString API_OverriddenAttributeOrRGBToString (const API_OverriddenAttributeOrRGB& overriddenAttr)
{
	if (overriddenAttr.value.IsType<API_AttributeIndex> ()) {
		return GS::UniString::Printf ("%s, %s", overriddenAttr.value.Get<API_AttributeIndex> ().ToUniString ().ToCStr ().Get (), overriddenAttr.hasValue ? "overridden" : "");
	} else {
		const API_RGBColor rgbColor = overriddenAttr.value.Get<API_RGBColor> ();
		return GS::UniString::Printf ("[%f, %f, %f], %s", rgbColor.f_red, rgbColor.f_green, rgbColor.f_blue, overriddenAttr.hasValue ? "overridden" : "");
	}
}


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
static GSErrCode DumpElementOverrideStyle ()
{
	API_ElemType	type;
	API_Guid		guid;

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return NoError;
	}
	API_OverrideStyle		elemOverrideStyle;
	GSErrCode err = ACAPI_Element_GetActualOverrideStyle (guid, elemOverrideStyle);
	if (err == NoError) {
		WriteReport ("  elem guid                       : %s", APIGuidToString (guid).ToCStr ().Get ());
		WriteReport ("  lineType                        : %s", API_OverriddenAttributeToString (elemOverrideStyle.lineType).ToCStr ().Get ());
		WriteReport ("  lineMarkerTextPen               : %s", API_OverriddenPenToString (elemOverrideStyle.lineMarkerTextPen).ToCStr ().Get ());
		WriteReport ("  overrideLinePenThickness        : %d", elemOverrideStyle.overrideLinePenThickness);

		WriteReport ("  cutFillType                     : %s", API_OverriddenAttributeToString (elemOverrideStyle.cutFillType).ToCStr ().Get ());
		WriteReport ("  cutFillFGPen                    : %s", API_OverriddenPenToString (elemOverrideStyle.cutFillFGPen).ToCStr ().Get ());
		WriteReport ("  cutFillBGPen                    : %s", API_OverriddenPenOrRGBToString (elemOverrideStyle.cutFillBGPen).ToCStr ().Get ());
		WriteReport ("  overrideCutFillFGThickness      : %d", elemOverrideStyle.overrideCutFillFGThickness);

		WriteReport ("  coverFillType                   : %s", API_OverriddenAttributeToString (elemOverrideStyle.coverFillType).ToCStr ().Get ());
		WriteReport ("  coverFillFGPen                  : %s", API_OverriddenPenToString (elemOverrideStyle.coverFillFGPen).ToCStr ().Get ());
		WriteReport ("  coverFillBGPen                  : %s", API_OverriddenPenOrRGBToString (elemOverrideStyle.coverFillBGPen).ToCStr ().Get ());
		WriteReport ("  overrideCoverFillFGThickness    : %d", elemOverrideStyle.overrideCoverFillFGThickness);

		WriteReport ("  draftFillType                   : %s", API_OverriddenAttributeToString (elemOverrideStyle.draftFillType).ToCStr ().Get ());
		WriteReport ("  draftFillFGPen                  : %s", API_OverriddenPenToString (elemOverrideStyle.draftFillFGPen).ToCStr ().Get ());
		WriteReport ("  draftFillBGPen                  : %s", API_OverriddenPenOrRGBToString (elemOverrideStyle.draftFillBGPen).ToCStr ().Get ());
		WriteReport ("  overrideDraftFillFGThickness    : %d", elemOverrideStyle.overrideDraftFillFGThickness);

		WriteReport ("  cut surfaceType                 : %s", API_OverriddenAttributeOrRGBToString (elemOverrideStyle.cutSurfaceType).ToCStr ().Get ());
		WriteReport ("  uncut surfaceType               : %s", API_OverriddenAttributeOrRGBToString (elemOverrideStyle.uncutSurfaceType).ToCStr ().Get ());
		WriteReport ("  showSkinSeparators              : %d", elemOverrideStyle.showSkinSeparators);

		WriteReport ("  cut surface contours            : %s", elemOverrideStyle.hideCutContours ? "hidden" : "not hidden");
		WriteReport ("  uncut surface contours          : %s", elemOverrideStyle.hideUncutContours ? "hidden" : "not hidden");

	} else {
		WriteReport ("ACAPI_Element_GetOverrideStyle ERROR");
	}
	return err;
}


// -----------------------------------------------------------------------------
// Creates a new override rule
//  - criterion: element type is window or door
//  - override style: sets surfaces to transparent, sets fill background pen to transparent
// -----------------------------------------------------------------------------
static GSErrCode CreateTransparentWDOverrideRule (API_Guid* newGuid = nullptr)
{
	// valid rule group for testing
	API_OverrideRuleGroup wdRuleGroup = {APINULLGuid, "Window and Door Rules"};
	ASSERT_NO_ERROR (ACAPI_GraphicalOverride_CreateOverrideRuleGroup (wdRuleGroup));
	ASSERT (wdRuleGroup.guid != APINULLGuid);

	API_OverrideRule rule;
	rule.name = "Transparent surface for Windows and Doors";

	const GS::Guid doorGuid	= BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetMainGuid (BL::BuiltInLibPartID::DoorWallLibPartID);
	const GS::Guid windowGuid = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetMainGuid (BL::BuiltInLibPartID::WindowWallLibPartID);

	// NOTE: to get the proper criterion XML, go to ARCHICAD and save the wanted override rule as XML
	// Criterion XML: Element Type is Window or Door
	rule.criterionXML =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>"
		"<CriterionExpression Mv=\"2\" Sv=\"1\">"
			"<TextMatchType>1</TextMatchType>"
			"<Size>1</Size>"
			"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
			"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
				"<LogicalOperator>2</LogicalOperator>"
				"<CriteriaCount>1</CriteriaCount>"
				"<ClassGuid>C6EBD1BD-7702-46FF-8ED9-9CC37648A7C7</ClassGuid>"
				"<CompositeCriterion Mv=\"2\" Sv=\"1\">"
					"<LogicalOperator>1</LogicalOperator>"
					"<CriteriaCount>2</CriteriaCount>"
					"<ClassGuid>B4B7B134-EC56-4D40-8D4C-71D7C5A2493A</ClassGuid>"
					"<ElemTypeCriterion Mv=\"2\" Sv=\"3\">"
						"<VBEF::CritToolType Mv=\"1\" Sv=\"0\">"
							"<ElemRegistryRefId>1464421956</ElemRegistryRefId>"
							"<ElemCreatorToolUnID>"
								"<MainGuid>" + windowGuid.ToUniString () + "</MainGuid>"
								"<RevGuid>5585F21A-DE1C-164C-A64E-92CB48F79FA5</RevGuid>"
							"</ElemCreatorToolUnID>"
						"</VBEF::CritToolType>"
						"<LogicalOperator>1</LogicalOperator>"
					"</ElemTypeCriterion>"
					"<ClassGuid>B4B7B134-EC56-4D40-8D4C-71D7C5A2493A</ClassGuid>"
					"<ElemTypeCriterion Mv=\"2\" Sv=\"3\">"
						"<VBEF::CritToolType Mv=\"1\" Sv=\"0\">"
							"<ElemRegistryRefId>1146048338</ElemRegistryRefId>"
							"<ElemCreatorToolUnID>"
								"<MainGuid>" + doorGuid.ToUniString () + "</MainGuid>"
								"<RevGuid>91A61E93-32A8-BC46-A11A-5FB89256C651</RevGuid>"
							"</ElemCreatorToolUnID>"
						"</VBEF::CritToolType>"
						"<LogicalOperator>1</LogicalOperator>"
					"</ElemTypeCriterion>"
				"</CompositeCriterion>"
			"</CompositeCriterion>"
		"</CriterionExpression>";

	rule.style = API_OverrideRuleStyle ();
	// Override surface to 131 (Air material = transparent)
	rule.style.surfaceOverride = ACAPI_CreateAttributeIndex (131);
	rule.style.surfaceType.overrideCutSurface = true;
	rule.style.surfaceType.overrideUncutSurface = true;
	// Override fill background pen to 0 (transparent)
	rule.style.fillBackgroundPenOverride = static_cast<short> (0);

	GSErrCode err = ACAPI_GraphicalOverride_CreateOverrideRule (rule, wdRuleGroup.guid);
	if (err == NoError && newGuid != nullptr)
		*newGuid = rule.guid;

	return err;
}


// -----------------------------------------------------------------------------
// Creates a new override combination with the given name and rules
// -----------------------------------------------------------------------------
static GSErrCode CreateOverrideCombination (const GS::UniString& name, const GS::Array<API_Guid>& ruleList, API_Guid* newGuid = nullptr)
{
	API_OverrideCombination combination;
	combination.name = name;

	GSErrCode err = ACAPI_GraphicalOverride_CreateOverrideCombination (combination, ruleList);
	if (err == NoError && newGuid != nullptr)
		*newGuid = combination.guid;

	return err;
}

// -----------------------------------------------------------------------------
// Sets perspective view's (from Public View Map) override combination
// -----------------------------------------------------------------------------
static GSErrCode SetPerspectiveViewOverrideCombination ()
{
	API_Guid ruleGuid;
	GSErrCode err = CreateTransparentWDOverrideRule (&ruleGuid);
	if (err != NoError)
		return err;

	GS::UniString combinationName ("API Custom Override Combination");
	GS::Array<API_Guid> ruleList;
	ruleList.Push (ruleGuid);
	err = CreateOverrideCombination (combinationName, ruleList);
	if (err != NoError)
		return err;

	GS::Array<API_NavigatorItem> navItems;
	API_NavigatorItem navItem = {};

	navItem.mapId = API_PublicViewMap;
	navItem.itemType = API_PerspectiveNavItem;

	err = ACAPI_Navigator_SearchNavigatorItem (&navItem, &navItems);
	if (err != NoError)
		return err;

	API_NavigatorItem& perspNavItem = navItems[0];
	perspNavItem.mapId = API_PublicViewMap;

	err = ACAPI_Navigator_GetNavigatorItem (&perspNavItem.guid, &perspNavItem);
	if (err != NoError)
		return err;

	API_NavigatorView view = {};
	err = ACAPI_Navigator_GetNavigatorView (&perspNavItem, &view);
	if (err != NoError)
		return err;

	GS::ucsncpy (view.overrideCombination, combinationName.ToUStr (), API_UniLongNameLen);

	return ACAPI_Navigator_ChangeNavigatorView (&perspNavItem, &view);
}


// -----------------------------------------------------------------------------
// Returns the name of the attribute given by type and index
// -----------------------------------------------------------------------------
static const char* GetAttributeName (API_AttrTypeID inType, API_AttributeIndex inIndex)
{
	API_Attribute	attr;
	static char buffer[256] = {0};

	*buffer			   = {};
	attr			   = {};
	attr.header.typeID = inType;
	attr.header.index  = inIndex;
	if (ACAPI_Attribute_Get (&attr) == NoError) {
		CHTruncate (attr.header.name, buffer, sizeof (buffer));
		if (inType == API_MaterialID)
			delete attr.material.texture.fileLoc;
	} else
		buffer[0] = ' ';

	return buffer;
}


// -----------------------------------------------------------------------------
// Dump the primitives from the given vector image
// -----------------------------------------------------------------------------
static void DumpVectorImage (const API_VisualOverriddenImage& api_image)
{
	if (api_image.vectorImageHandle == nullptr)
		return;

	try {
		VectorImage vectorImage;
		vectorImage.ImportFromHandle (api_image.vectorImageHandle);

		ConstVectorImageIterator vectorImageIt (vectorImage);

		while (!vectorImageIt.IsEOI ()) {
			switch (vectorImageIt->item_Typ) {
				case SyHots: {
						const Sy_HotType* pSyHot = ((const Sy_HotType*) vectorImageIt);
						WriteReport ("\tHotspot");
						WriteReport ("\t\tCoordinate: %.3f, %.3f", pSyHot->c.x, pSyHot->c.y);
					}
					break;

				case SyLine: {
						const Sy_LinType* pSyLine = static_cast <const Sy_LinType*> (vectorImageIt);
						WriteReport ("\tLine");
						WriteReport ("\t\tLayer: %d; SpecForProfile: %d", pSyLine->sy_fragmentIdx, pSyLine->specForProfile);
					}
					break;

				case SyPolyLine: {
						const Sy_PolyLinType* pSyPolyLine = static_cast <const Sy_PolyLinType*> (vectorImageIt);
						WriteReport ("\tLine");
						WriteReport ("\t\tLayer: %d", pSyPolyLine->sy_fragmentIdx);
					}
					break;

				case SyHatch: {
						const HatchObject& pSyHatch = vectorImageIt;

						WriteReport ("\tHatch");
						WriteReport ("\t\tCoordinates: %ld", pSyHatch.GetCoords ().GetSize ());
						UInt32 index = 0;
						for (Coord coord : pSyHatch.GetCoords ()) {
							WriteReport ("\t\t\tCoord #%-2d (%5.2f, %5.2f)", index++, coord.x, coord.y);
						}
						if (pSyHatch.HasProfileInfo ()) {
							const std::vector<ProfileEdgeData>& profileEdgeData = pSyHatch.GetProfileEdgeData ();
							DBASSERT (pSyHatch.GetCoords ().GetSize () == profileEdgeData.size ());
							// Don't skip first elem
							for (auto edgeIt = profileEdgeData.begin(); edgeIt != profileEdgeData.end(); ++edgeIt) {
								WriteReport ("\t\t\tEdge data #%-2d surface = %32s (%3s) pen = (%3ld)", std::distance(profileEdgeData.begin (), edgeIt), GetAttributeName (API_MaterialID, AC2API_AttrIndex (edgeIt->GetMaterial ())),
											 edgeIt->GetMaterial ().ToUniString ().ToCStr ().Get (), edgeIt->GetPen ());
							}
						}
					}
					break;

				case SyArc:
				case SyText:
				case SySpline:
				case SyPicture:
				case SyPixMap:
				case SyPointCloud:
					break;
			}
			++vectorImageIt;
		}
	} catch (const GS::Exception&) {
		WriteReport_Alert ("DumpVectorImage failed");
	}
}


// -----------------------------------------------------------------------------
// Creates visual overridden images for the clicked element
//  - dumps the vector image into the Report Window
//  - shows the native image in a modal dialog
// -----------------------------------------------------------------------------
static GSErrCode CreateElemVisualOverriddenImages (void)
{
	API_Guid	guid;
	GSErrCode	err = NoError;

	if (!ClickAnElem ("Click an elem to get vector image", API_ZombieElemID, nullptr, nullptr, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return err;
	}

	UserItemDialog dialog (3, 250, 250, [&] (const DG::UserItem& userItem, const UIndex index) {
		NewDisplay::NativeImage nativeImage (userItem.GetClientWidth (), userItem.GetClientHeight (), 32, nullptr, false, 0, true, userItem.GetResolutionFactor ());

		API_VisualOverriddenImage image = {};
		image.nativeImagePtr = &nativeImage;

		switch (index) {
			case 0:
				WriteReport ("\t APIImage_Section:");
				image.view = APIImage_Section;
				image.overrideStyle.uncutSurfaceType = ACAPI_CreateAttributeIndex (5);		// Stone - Limestone Fine
				break;
			case 1:
				WriteReport ("\t APIImage_Model2D:");
				image.view = APIImage_Model2D;
				break;
			default:
				WriteReport ("\t APIImage_Model3D:");
				image.view = APIImage_Model3D;
				image.overrideStyle.uncutSurfaceType = ACAPI_CreateAttributeIndex (1);	// Brick - Running Bond
				break;
		}

		if (image.overrideStyle.uncutSurfaceType.hasValue && image.overrideStyle.uncutSurfaceType.value.IsType<API_AttributeIndex> ()) {
			const API_AttributeIndex surfaceIndex = image.overrideStyle.uncutSurfaceType.value.Get<API_AttributeIndex> ();
			WriteReport ("image.overrideStyle.uncutSurfaceType = %32s (%3ld)", GetAttributeName (API_MaterialID, surfaceIndex), surfaceIndex);
		}

		err = ACAPI_GraphicalOverride_GetVisualOverriddenImage (guid, &image);
		if (err != NoError)
			ErrorBeep ("ACAPI_GraphicalOverride_GetVisualOverriddenImage (APIImage_Section)", err);
		else
			DumpVectorImage (image);
		BMhFree (image.vectorImageHandle);

		return nativeImage;
	});
	dialog.Invoke ();

	return err;
}


// -----------------------------------------------------------------------------
// Add-on entry point definition
// -----------------------------------------------------------------------------
GSErrCode APIMenuCommandProc_Main (const API_MenuParams *menuParams)
{
	if (menuParams->menuItemRef.menuResID == 32500) {
		return ACAPI_CallUndoableCommand ("Override Test API Function",
			[&] () -> GSErrCode {
				switch (menuParams->menuItemRef.itemIndex) {
					case  1: return RunOverrideTests ();
					case  2: return DumpElementOverrideStyle ();
					case  3: return SetPerspectiveViewOverrideCombination ();
					case  4: return CreateElemVisualOverriddenImages ();
					default: return NoError;
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
	return ACAPI_MenuItem_InstallMenuHandler (32500, APIMenuCommandProc_Main);
}


// -----------------------------------------------------------------------------
// Called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData	(void)
{
	return NoError;
}
