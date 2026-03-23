// *****************************************************************************
// Source code for the IFCHook Test Add-On
// *****************************************************************************

#include "APIEnvir.h"
#define	_IFCHook_Test_TRANSL_

#include "ACAPI/IFCAttribute.hpp"
#include "ACAPI/IFCClassificationReference.hpp"
#include "ACAPI/IFCHookAssignments.hpp"
#include "ACAPI/IFCHookManager.hpp"
#include "ACAPI/IFCObjectAccessor.hpp"
#include "ACAPI/IFCProperty.hpp"
#include "ACAPI/IFCPropertyBuilder.hpp"

#include "ACAPI/MEPAdapter.hpp"

// ---------------------------------- Includes ---------------------------------

#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"DGModule.hpp"

// ---------------------------------- Types ------------------------------------

// ---------------------------------- Variables --------------------------------

const short HookMenuItemsResourceId = 32500;
const short HookEnableToggleMenuItemId = 1;


// ---------------------------------- Prototypes -------------------------------


// =============================================================================
//
// Main functions
//
// =============================================================================


bool IsHookEnabled ()
{
	return GetMenuItemMark (HookMenuItemsResourceId, HookEnableToggleMenuItemId);
}


[[nodiscard]] bool GetAPIElement (const IFCAPI::ObjectID& objectID, API_Element& apiElement, GS::UniString* infoString)
{
	apiElement = {};

	auto elementIDResult = IFCAPI::GetObjectAccessor ().GetAPIElementID (objectID);
	if (elementIDResult.IsOk () && *elementIDResult != APINULLGuid) {
		apiElement.header.guid = *elementIDResult;
		if (ACAPI_Element_Get (&apiElement) == NoError) {
			if (infoString != nullptr) {
				API_ElementMemo memo{};
				ACAPI_Element_GetMemo (apiElement.header.guid, &memo, APIMemoMask_ElemInfoString);
				*infoString = *memo.elemInfoString;
				ACAPI_DisposeElemMemoHdls (&memo);
			}

			return true;
		}
	}

	return false;
}


//![IFCAPI GetStoryIndex example]
[[nodiscard]] bool GetStoryLevel (const IFCAPI::ObjectID& objectID, double& level)
{
	bool success = false;
	level = 0.0;

	auto storyIndex = IFCAPI::GetObjectAccessor ().GetStoryIndex (objectID);
	if (storyIndex.IsOk ()) {
		API_StoryInfo	storyInfo{};
		if (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) == NoError) {
			for (short i = 0; (i <= storyInfo.lastStory - storyInfo.firstStory); ++i) {
				if ((*storyInfo.data)[i].index == *storyIndex) {
					level = (*storyInfo.data)[i].level;
					success = true;
					break;
				}
			}
			BMKillHandle ((GSHandle*)&storyInfo.data);
		}
	}

	return success;
}
//![IFCAPI GetStoryIndex example]


template<typename TValue>
static void AddProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name, const GS::UniString& valueType, const TValue& value)
{
	auto apiValue = IFCAPI::GetPropertyBuilder ().CreateValue (valueType, value);
	if (apiValue.IsOk ()) {
		auto apiProperty = IFCAPI::GetPropertyBuilder ().CreatePropertySingleValue (setName, name, *apiValue);
		if (apiProperty.IsOk ())
			properties.push_back (*apiProperty);
	}
}


static void AddTextProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name, const GS::UniString& valueType, const GS::UniString& value)
{
	AddProperty (properties, setName, name, valueType, value);
}


static void AddDoubleProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name, const GS::UniString& valueType, const double& value)
{
	AddProperty (properties, setName, name, valueType, value);
}


static void AddIntProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name, const GS::UniString& valueType, const Int64& value)
{
	AddProperty (properties, setName, name, valueType, value);
}


static void AddBoundedProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name)
{
	auto lowerValue = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcReal", 1.5);
	auto setPointValue = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcReal", 3.5);
	if (lowerValue.IsOk () && setPointValue.IsOk ()) {
		auto apiProperty = IFCAPI::GetPropertyBuilder ().CreatePropertyBoundedValue (setName, name, *lowerValue, IFCAPI::GetPropertyBuilder ().CreateEmptyValue (), *setPointValue);
		if (apiProperty.IsOk ())
			properties.push_back (*apiProperty);
	}
}


static void AddListProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name)
{
	auto value1 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcText", GS::UniString ("One"));
	auto value2 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcText", GS::UniString ("Two"));
	auto value3 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcText", GS::UniString ("Three"));
	if (value1.IsOk () && value2.IsOk () && value3.IsOk ()) {
		auto apiProperty = IFCAPI::GetPropertyBuilder ().CreatePropertyListValue (setName, name, { *value1, *value2, *value3 });
		if (apiProperty.IsOk ())
			properties.push_back (*apiProperty);
	}
}


static void AddTableProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name)
{
	auto key1 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcInteger", (Int64)1);
	auto key2 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcInteger", (Int64)2);
	auto value1 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcLabel", GS::UniString ("Apple"));
	auto value2 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcLabel", GS::UniString ("Pear"));

	if (key1.IsOk () && key2.IsOk () && value1.IsOk () && value2.IsOk ()) {
		auto apiProperty = IFCAPI::GetPropertyBuilder ().CreatePropertyTableValue (setName, name, { *key1, *key2 }, { *value1, *value2 });
		if (apiProperty.IsOk ())
			properties.push_back (*apiProperty);
	}
}


static void AddEnumeratedProperty (std::vector<IFCAPI::Property>& properties, const GS::UniString& setName, const GS::UniString& name)
{
	auto value1 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcLabel", GS::UniString ("A"));
	auto value2 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcLabel", GS::UniString ("B"));
	auto value3 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcLabel", GS::UniString ("C"));
	auto value4 = IFCAPI::GetPropertyBuilder ().CreateValue ("IfcLabel", GS::UniString ("D"));

	if (value1.IsOk () && value2.IsOk () && value3.IsOk () && value4.IsOk ()) {
		auto apiProperty = IFCAPI::GetPropertyBuilder ().CreatePropertyEnumeratedValue (setName, name, { *value1, *value2, *value3, *value4 }, { *value2, *value3 });
		if (apiProperty.IsOk ())
			properties.push_back (*apiProperty);
	}
}


static void AddAttribute (std::vector<IFCAPI::Attribute>& attributes, const GS::UniString& name, const GS::UniString& value)
{
	auto attribute = IFCAPI::GetPropertyBuilder ().CreateAttribute (name, value);
	if (attribute.IsOk ())
		attributes.push_back (*attribute);
}


static void AddClassificationReference (std::vector<IFCAPI::ClassificationReference>& classificationReferences, const GS::UniString& relName)
{
	auto classification = IFCAPI::GetPropertyBuilder ().CreateClassification ("Name", "Source", "Edition", "2018-01-30", "Description", "Location");
	if (classification.IsOk ()) {
		auto classificationReference = IFCAPI::GetPropertyBuilder ().CreateClassificationReference ("Location", "Identification", "Name", *classification, relName);
		if (classificationReference.IsOk ())
			classificationReferences.push_back (*classificationReference);
	}
}


struct MyGroupingData {
	// deterministic guids to identify the groups with
	API_Guid columnsGroupGuid, beamsGroupGuid, wallsGroupGuid, zonesGroupGuid, slabsGroupGuid, mepGroupGuid;
	// names of the groups
	GS::UniString columnsGroupName, beamsGroupName, wallsGroupName, zonesGroupName, slabsGroupName, mepGroupName;
	// properties to add to all groups
	std::vector<IFCAPI::Property> groupProperties;
	// classification references to add to all groups
	std::vector<IFCAPI::ClassificationReference> groupClassificationReferences;

	// hook-created group objects stored
	std::unordered_set<IFCAPI::ObjectID> groupObjects;
	// hook-created relation objects' naming stored
	std::unordered_map<IFCAPI::ObjectID, GS::UniString> relationToGroupNameTable;

	bool IsHookGroup (const IFCAPI::ObjectID& objectID) {
		return groupObjects.find (objectID) != groupObjects.end ();
	}

	bool IsNamedHookRelation (const IFCAPI::ObjectID& objectID) {
		return relationToGroupNameTable.find (objectID) != relationToGroupNameTable.end ();
	}
};


static MyGroupingData myGroupingData;


void InitializeHookGrouping ()
{
	// setting up data to use in the hook
	myGroupingData.columnsGroupGuid = GSGuid2APIGuid (GS::Guid ("{33029A55-F9BC-4E82-A1BF-0FC60FF94975}"));
	myGroupingData.beamsGroupGuid = GSGuid2APIGuid (GS::Guid ("{0DD3BBF5-D933-4358-BF8A-750A39CE7E09}"));
	myGroupingData.wallsGroupGuid = GSGuid2APIGuid (GS::Guid ("{91A1E739-4D95-457D-897E-CEF5C40440F9}"));
	
	myGroupingData.zonesGroupGuid = GSGuid2APIGuid (GS::Guid ("{3D1B492A-3D86-4311-8258-E8E5EE23F2B4}"));
	myGroupingData.slabsGroupGuid = GSGuid2APIGuid (GS::Guid ("{0EBD0648-21CF-49E5-9034-A734D3433382}"));
	myGroupingData.mepGroupGuid = GSGuid2APIGuid (GS::Guid ("{E3F3BD9F-1636-4473-973C-D7E5C4A67C7C}"));

	myGroupingData.columnsGroupName = GS::UniString ("Columns main group");
	myGroupingData.beamsGroupName = GS::UniString ("Beams main group");
	myGroupingData.wallsGroupName = GS::UniString ("Walls subgroup");

	myGroupingData.zonesGroupName = GS::UniString ("All the Spaces");
	myGroupingData.slabsGroupName = GS::UniString ("System of Slabs");
	myGroupingData.mepGroupName = GS::UniString ("System of all MEP");

	AddTextProperty (myGroupingData.groupProperties, "Group Property Set", "Group Property", "IfcLabel", "I'm some sort of IfcGroup");
	AddClassificationReference (myGroupingData.groupClassificationReferences, "Group Classification Reference");
}


static void PropertyHookCommon (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::Property>& properties, const GS::UniString namePostfix)
{
	API_Element apiElement{};
	GS::UniString infoString;
	if (GetAPIElement (objectID, apiElement, &infoString)) {
		// create scheme property after every infoString character
		for (auto index : GS::InRange ((UIndex)0, infoString.GetLength ())) {
			if (infoString[index].IsWhiteSpace ())
				continue;

			GS::UniString character = infoString.GetSubstring (index, 1);

			API_IFCProperty apiProperty;
			GS::UniString propertySetName = "PSet_InfoString";
			GS::UniString propertyName = character;

			propertySetName.Append (namePostfix);
			propertyName.Append (namePostfix);

			AddTextProperty (properties, propertySetName, propertyName, "IfcLabel", character);
		}
	}
}


static void PropertyHook (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::Property>& properties)
{
	if (!IsHookEnabled ())
		return;

	API_Element apiElement{};
	GS::UniString infoString;
	double storyLevel = 0.0;
	if (GetAPIElement (objectID, apiElement, &infoString)) {
		GS::UniString layerName = "MISSING";
		{
			if (apiElement.header.layer == APIApplicationLayerAttributeIndex) {
				char editableStr[256];
				ACAPI_Resource_GetLocStr (editableStr, 32501, 1);	// "ARCHICAD Layer"
				layerName = editableStr;
			} else {
				API_Attribute attr{};
				attr.header.typeID = API_LayerID;
				attr.header.index = apiElement.header.layer;
				attr.header.uniStringNamePtr = &layerName;
				ACAPI_Attribute_Get (&attr);
			}
		}

		GS::UniString value = "The name of my layer is ";
		value.Append (layerName);
		AddTextProperty (properties, "Sample Property Set", "Sample Property", "IfcLabel", value);

		if (apiElement.header.type == API_ZoneID)
			AddTextProperty (properties, "Room Property Set", "Room Property", "IfcText", apiElement.zone.roomNoStr);

		auto ifcTypeResult = IFCAPI::GetObjectAccessor ().GetIFCType (objectID);
		if (ifcTypeResult.IsOk () && ifcTypeResult->IsEqual ("IFCBeam", GS::CaseInsensitive) && apiElement.header.type == API_BeamID) {
			const GS::UniString BeamPSetName ("My Beam Property Set");
			AddDoubleProperty (properties, BeamPSetName, "Level of the beam", "IfcLengthMeasure", apiElement.beam.level);
			AddBoundedProperty (properties, BeamPSetName, "Bounds");
			AddListProperty (properties, BeamPSetName, "List");
			AddEnumeratedProperty (properties, BeamPSetName, "Options");
			AddTableProperty (properties, BeamPSetName, "Table");
		}
	} else if (myGroupingData.IsHookGroup (objectID)) {
		properties = myGroupingData.groupProperties;
	} else if (GetStoryLevel (objectID, storyLevel)) {
		AddDoubleProperty (properties, "Sample Property Set", "Story Level", "IfcLengthMeasure", storyLevel);
	} else {
		auto ifcType = IFCAPI::GetObjectAccessor ().GetIFCType (objectID);
		if (ifcType.IsOk ()) {
			static const GS::UniString PSBPropertyName ("PSB Property");
			if (ifcType->IsEqual ("IfcProject")) {
				AddTextProperty (properties, "Sample Property Set", PSBPropertyName, "IfcLabel", "Rodent");
			} else if (ifcType->IsEqual ("IfcSite")) {
				AddTextProperty (properties, "Sample Property Set", PSBPropertyName, "IfcLabel", "Mite");
			} else if (ifcType->IsEqual ("IfcBuilding")) {
				AddTextProperty (properties, "Sample Property Set", PSBPropertyName, "IfcLabel", "Gosling");
			}
		}
	}

	PropertyHookCommon (objectID, properties, GS::EmptyUniString);
}


static void TypeObjectPropertyHook (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::Property>& properties)
{
	if (!IsHookEnabled ())
		return;

	API_Element apiElement{};
	GS::UniString infoString;
	if (GetAPIElement (objectID, apiElement, &infoString)) {
		auto elementResult = IFCAPI::GetObjectAccessor ().GetAPIElementID (objectID);
		if (elementResult.IsOk ()) {
			// make sure different ID makes different type
			AddTextProperty (properties, "PSet_Id", "Id", "IfcText", infoString);

			auto ifcTypeResult = IFCAPI::GetObjectAccessor ().GetTypeObjectIFCType (objectID);
			if (ifcTypeResult.IsOk () && ifcTypeResult->IsEqual ("IfcBoilerType", GS::CaseInsensitive)) {
				AddIntProperty (properties, "My BoilerType Pset", "My Boiler Property", "IfcInteger", 42);
			}
		}
	}

	PropertyHookCommon (objectID, properties, " (type)");
}


static void AttributeHookCommon (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::Attribute>& attributes)
{
	if (myGroupingData.IsNamedHookRelation (objectID)) {
		// applies to relations of a named group
		GS::UniString value = "A relation of ";
		value.Append (myGroupingData.relationToGroupNameTable[objectID]);
		AddAttribute (attributes, "Description", value);
	} else {
		// applies to anything else with a description attr
		AddAttribute (attributes, "Description", "Sample description");
	}

	// applies to spatial elements
	AddAttribute (attributes, "CompositionType", "PARTIAL");

	// must not work
	AddAttribute (attributes, "GlobalId", "hahaha!");
}


static void AttributeHook (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::Attribute>& attributes)
{
	if (!IsHookEnabled ())
		return;

	AttributeHookCommon (objectID, attributes);
}


static void TypeObjectAttributeHook (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::Attribute>& attributes)
{
	if (!IsHookEnabled ())
		return;

	AttributeHookCommon (objectID, attributes);
}


static void ClassificationReferenceHookCommon (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::ClassificationReference>& classificationReferences)
{
	API_Element apiElement{};
	GS::UniString infoString;
	if (GetAPIElement (objectID, apiElement, &infoString)) {
		GS::UniString relName = "Sample Classification Reference";
		if (apiElement.header.type == API_SlabID) {
			relName.Append (" - ");
			relName.Append (infoString);
		}

		AddClassificationReference (classificationReferences, relName);
	} else if (myGroupingData.IsHookGroup (objectID)) {
		classificationReferences = myGroupingData.groupClassificationReferences;
	}
}


static void ClassificationReferenceHook (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::ClassificationReference>& classificationReferences)
{
	if (!IsHookEnabled ())
		return;

	ClassificationReferenceHookCommon (objectID, classificationReferences);
}


static void TypeObjectClassificationReferenceHook (const IFCAPI::ObjectID& objectID, std::vector<IFCAPI::ClassificationReference>& classificationReferences)
{
	if (!IsHookEnabled ())
		return;

	ClassificationReferenceHookCommon (objectID, classificationReferences);
}


using ElementsGetter = std::function<void (GS::Array<API_Guid>&)>;
static ACAPI::Result<IFCAPI::ObjectID> AddGroup (IFCAPI::HookAssignments& hookAssignments, const API_Guid& guid, const IFCAPI::IFCType& type, const std::optional<GS::UniString>& name, bool addAsRoot, const ElementsGetter& toAssign, const std::optional<ElementsGetter>& toService = std::nullopt)
{
	auto GetElementObjects = [](const ElementsGetter& getter)
	{
		GS::Array<API_Guid> elementGuids;
		getter (elementGuids);

		std::vector<IFCAPI::ObjectID> objects;
		for (const auto& elementGuid : elementGuids) {
			API_Elem_Head head{};
			head.guid = elementGuid;
			auto elementResult = IFCAPI::GetObjectAccessor ().CreateElementObjectID (head);
			if (elementResult.IsOk ())
				objects.push_back (elementResult.Unwrap ());
		}

		return objects;
	};

// add a group with connected objects and store returned group and relations' ObjectIDs for later identification

//![IFCAPI CreateIfcGroup example]
//![IFCAPI AssignObjects example]
//![IFCAPI ServiceBuildings example]
//![IFCAPI GetIfcRelAssignsToGroup example]
//![IFCAPI GetIfcRelServicesBuildings example]
	auto groupResult = hookAssignments.CreateIfcGroup (guid, type, name, addAsRoot);
	if (groupResult.IsOk ()) {
		auto group = groupResult.Unwrap ();
		myGroupingData.groupObjects.emplace (group);

		hookAssignments.AssignObjects (group, GetElementObjects (toAssign));

		if (toService.has_value ())
			hookAssignments.ServiceBuildings (group, GetElementObjects (*toService));

		if (name.has_value ()) {
			auto relAssigns = hookAssignments.GetIfcRelAssignsToGroup (group);
			if (relAssigns.IsOk ())
				myGroupingData.relationToGroupNameTable.emplace (*relAssigns, *name);

			auto relServices = hookAssignments.GetIfcRelServicesBuildings (group);
			if (relServices.IsOk ())
				myGroupingData.relationToGroupNameTable.emplace (*relServices, *name);
		}
	}
//![IFCAPI CreateIfcGroup example]
//![IFCAPI AssignObjects example]
//![IFCAPI ServiceBuildings example]
//![IFCAPI GetIfcRelAssignsToGroup example]
//![IFCAPI GetIfcRelServicesBuildings example]

	return groupResult;
}


static void AssignmentsHook (IFCAPI::HookAssignments& hookAssignments)
{
	myGroupingData.groupObjects.clear ();
	myGroupingData.relationToGroupNameTable.clear ();

	if (!IsHookEnabled ())
		return;

	auto columnGroup = AddGroup (hookAssignments, myGroupingData.columnsGroupGuid, "IfcGroup", myGroupingData.columnsGroupName, true,
		[](GS::Array<API_Guid>& elements) {
			ACAPI_Element_GetElemList (API_ColumnID, &elements);
		}
	);

	auto beamGroup = AddGroup (hookAssignments, myGroupingData.beamsGroupGuid, "IfcGroup", myGroupingData.beamsGroupName, true,
		[](GS::Array<API_Guid>& elements) {
			ACAPI_Element_GetElemList (API_BeamID, &elements);
		}
	);

	auto wallGroup = AddGroup (hookAssignments, myGroupingData.wallsGroupGuid, "IfcGroup", myGroupingData.wallsGroupName, false,
		[](GS::Array<API_Guid>& elements) {
			ACAPI_Element_GetElemList (API_WallID, &elements);
		}
	);

	if (wallGroup.IsOk ()) {
		if (columnGroup.IsOk ())
			hookAssignments.AssignObjects (*columnGroup, { *wallGroup });
		if (beamGroup.IsOk ())
			hookAssignments.AssignObjects (*beamGroup, { *wallGroup });
	}

	AddGroup (hookAssignments, myGroupingData.zonesGroupGuid, "IfcZone", myGroupingData.zonesGroupName, true,
		[](GS::Array<API_Guid>& elements) {
			ACAPI_Element_GetElemList (API_ZoneID, &elements);
		}
	);

	AddGroup (hookAssignments, myGroupingData.slabsGroupGuid, "IfcBuildingSystem", myGroupingData.slabsGroupName, true,
		[](GS::Array<API_Guid>& elements) {
			ACAPI_Element_GetElemList (API_SlabID, &elements);
		},
		[](GS::Array<API_Guid>& elements) {
			ACAPI_Element_GetElemList (API_ZoneID, &elements);
		}
	);

	AddGroup (hookAssignments, myGroupingData.mepGroupGuid, "IfcDistributionSystem", myGroupingData.mepGroupName, true,
		[](GS::Array<API_Guid>& elements) {
			GS::Array<API_Guid> mepElementTypes = {
				ACAPI::MEP::VentilationBranchID,
				ACAPI::MEP::PipingBranchID,
				ACAPI::MEP::CableCarrierBranchID,
				ACAPI::MEP::VentilationTerminalID,
				ACAPI::MEP::PipingTerminalID,
				ACAPI::MEP::VentilationAccessoryID,
				ACAPI::MEP::PipingAccessoryID,
				ACAPI::MEP::EquipmentID,
				ACAPI::MEP::VentilationRoutingElementID,
				ACAPI::MEP::PipingRoutingElementID,
				ACAPI::MEP::CableCarrierRoutingElementID,
				ACAPI::MEP::VentilationFittingID,
				ACAPI::MEP::PipingFittingID,
				ACAPI::MEP::CableCarrierFittingID,
				ACAPI::MEP::VentilationRoutingSegmentID,
				ACAPI::MEP::PipingRoutingSegmentID,
				ACAPI::MEP::CableCarrierRoutingSegmentID,
				ACAPI::MEP::VentilationRoutingNodeID,
				ACAPI::MEP::PipingRoutingNodeID,
				ACAPI::MEP::CableCarrierRoutingNodeID,
				ACAPI::MEP::VentilationRigidSegmentID,
				ACAPI::MEP::PipingRigidSegmentID,
				ACAPI::MEP::CableCarrierRigidSegmentID,
				ACAPI::MEP::VentilationBendID,
				ACAPI::MEP::PipingBendID,
				ACAPI::MEP::CableCarrierBendID,
				ACAPI::MEP::VentilationTransitionID,
				ACAPI::MEP::PipingTransitionID,
				ACAPI::MEP::CableCarrierTransitionID,
				ACAPI::MEP::VentilationFlexibleSegmentID,
				ACAPI::MEP::PipingFlexibleSegmentID
			};

			for (const auto& classID : mepElementTypes)
				ACAPI_Element_GetElemList (classID, &elements);
		}
	);
}


//![IFCAPI RegisterPropertyHook example]
//![IFCAPI RegisterAttributeHook example]
//![IFCAPI RegisterClassificationReferenceHook example]
//![IFCAPI RegisterTypeObjectPropertyHook example]
//![IFCAPI RegisterTypeObjectAttributeHook example]
//![IFCAPI RegisterTypeObjectClassificationReferenceHook example]
//![IFCAPI RegisterAssignmentsHook example]
static void RegisterHooks ()
{
	IFCAPI::GetHookManager ().RegisterPropertyHook (PropertyHook);
	IFCAPI::GetHookManager ().RegisterAttributeHook (AttributeHook);
	IFCAPI::GetHookManager ().RegisterClassificationReferenceHook (ClassificationReferenceHook);

	IFCAPI::GetHookManager ().RegisterTypeObjectPropertyHook (TypeObjectPropertyHook);
	IFCAPI::GetHookManager ().RegisterTypeObjectAttributeHook (TypeObjectAttributeHook);
	IFCAPI::GetHookManager ().RegisterTypeObjectClassificationReferenceHook (TypeObjectClassificationReferenceHook);

	IFCAPI::GetHookManager ().RegisterAssignmentsHook (AssignmentsHook);
}
//![IFCAPI RegisterPropertyHook example]
//![IFCAPI RegisterAttributeHook example]
//![IFCAPI RegisterClassificationReferenceHook example]
//![IFCAPI RegisterTypeObjectPropertyHook example]
//![IFCAPI RegisterTypeObjectAttributeHook example]
//![IFCAPI RegisterTypeObjectClassificationReferenceHook example]
//![IFCAPI RegisterAssignmentsHook example]


//![IFCAPI UnregisterPropertyHook example]
//![IFCAPI UnregisterAttributeHook example]
//![IFCAPI UnregisterClassificationReferenceHook example]
//![IFCAPI UnregisterTypeObjectPropertyHook example]
//![IFCAPI UnregisterTypeObjectAttributeHook example]
//![IFCAPI UnregisterTypeObjectClassificationReferenceHook example]
//![IFCAPI UnregisterAssignmentsHook example]
static void UnregisterHooks ()
{
	IFCAPI::GetHookManager ().UnregisterPropertyHook ();
	IFCAPI::GetHookManager ().UnregisterAttributeHook ();
	IFCAPI::GetHookManager ().UnregisterClassificationReferenceHook ();

	IFCAPI::GetHookManager ().UnregisterTypeObjectPropertyHook ();
	IFCAPI::GetHookManager ().UnregisterTypeObjectAttributeHook ();
	IFCAPI::GetHookManager ().UnregisterTypeObjectClassificationReferenceHook ();

	IFCAPI::GetHookManager ().UnregisterAssignmentsHook ();
}
//![IFCAPI UnregisterPropertyHook example]
//![IFCAPI UnregisterAttributeHook example]
//![IFCAPI UnregisterClassificationReferenceHook example]
//![IFCAPI UnregisterTypeObjectPropertyHook example]
//![IFCAPI UnregisterTypeObjectAttributeHook example]
//![IFCAPI UnregisterTypeObjectClassificationReferenceHook example]
//![IFCAPI UnregisterAssignmentsHook example]


GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	ACAPI_KeepInMemory (true);
	if (menuParams->menuItemRef.itemIndex == 1) {
		InvertMenuItemMark (HookMenuItemsResourceId, HookEnableToggleMenuItemId);

		if (IsHookEnabled ()) {
			RegisterHooks ();
		} else {
			UnregisterHooks ();
		}
	}

	return NoError;
}


// -------------------------------------------------------------------------------------
// CheckEnvironment
//		Called only once by th server application to register the addon's functionality
// -------------------------------------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}


// -------------------------------------------------------------------------------------
// RegisterInterface
//		Called only once by the server application to register the addon's interface
// -------------------------------------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	// Register command service

	GSErrCode err = NoError;
	err |= ACAPI_MenuItem_RegisterMenu (HookMenuItemsResourceId, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore);

	return err;
}


// -------------------------------------------------------------------------------------
// Initialize
//		Called every time when the addon is loaded into the memory to execute a service
// -------------------------------------------------------------------------------------
GSErrCode Initialize ()
{
	GSErrCode err = NoError;

	err |= ACAPI_MenuItem_InstallMenuHandler (HookMenuItemsResourceId, MenuCommandHandler);
	if (err != NoError) {
		DBPrintf ("IFCHook_Test::Initialize() ACAPI_MenuItem_InstallMenuHandler failed!\n");
		return err;
	}

	InitializeHookGrouping ();

	ACAPI_KeepInMemory (true);

	return err;
}


// -------------------------------------------------------------------------------------
// FreeData
//		Called every time before the addon is unloaded from the memory
// -------------------------------------------------------------------------------------
GSErrCode FreeData ()
{
	UnregisterHooks ();

	return NoError;
}
