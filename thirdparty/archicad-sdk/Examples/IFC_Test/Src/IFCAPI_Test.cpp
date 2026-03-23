#include "Resources.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APICommon.h"
#include "MDIDs_Public.h"

#include "DGFolderDialog.hpp"
#include "DGModule.hpp"

#include "File.hpp"
#include "Folder.hpp"

#include "ObjectStateJSONConversion.hpp"

#include "ACAPI/IFCAssignments.hpp"
#include "ACAPI/IFCAttribute.hpp"
#include "ACAPI/IFCClassificationReference.hpp"
#include "ACAPI/IFCObjectAccessor.hpp"
#include "ACAPI/IFCObjectID.hpp"
#include "ACAPI/IFCProperty.hpp"
#include "ACAPI/IFCPropertyAccessor.hpp"

#include "UDTextInputDialog.hpp"


static GS::ErrorStatus ToStatus (const ACAPI::Error& error)
{
	if (error.kind == NoError)
		return GS::ErrorStatus (error.kind);

	return GS::ErrorStatus (error.kind, GS::UniString (error.text));
}


static void ShowStatusDialog (GS::ErrorStatus status, bool showOnSuccess)
{
	if (status.IsError ()) {
		GS::UniString errorMessage ("unknown error");
		if (status.HasErrorMessage ())
			errorMessage = status.GetErrorMessage ();

		DGAlert (DG_ERROR, GS::UniString ("Error"), GS::UniString ("API command error:"), errorMessage, GS::UniString ("OK"));
	} else if (showOnSuccess) {
		DGAlert (DG_INFORMATION, GS::UniString ("Done"), GS::UniString ("API command successful."), GS::UniString (), GS::UniString ("OK"));
	}
}


static GS::HashTable<API_ElemTypeID, GS::UniString> typeIDTable{
	{ API_WallID, "Wall" },
	{ API_ColumnID, "Column" },
	{ API_BeamID, "Beam" },
	{ API_WindowID, "Window" },
	{ API_DoorID, "Door" },
	{ API_ObjectID, "Object" },
	{ API_LampID, "Lamp" },
	{ API_SlabID, "Slab" },
	{ API_RoofID, "Roof" },
	{ API_MeshID, "Mesh" },
	{ API_DimensionID, "Dimension" },
	{ API_RadialDimensionID, "RadialDimension" },
	{ API_LevelDimensionID, "LevelDimension" },
	{ API_AngleDimensionID, "AngleDimension" },
	{ API_TextID, "Text" },
	{ API_LabelID, "Label" },
	{ API_ZoneID, "Zone" },
	{ API_HatchID, "Hatch" },
	{ API_LineID, "Line" },
	{ API_PolyLineID, "PolyLine" },
	{ API_ArcID, "Arc" },
	{ API_CircleID, "Circle" },
	{ API_SplineID, "Spline" },
	{ API_HotspotID, "Hotspot" },
	{ API_CutPlaneID, "CutPlane" },
	{ API_CameraID, "Camera" },
	{ API_CamSetID, "CamSet" },
	{ API_GroupID, "Group" },
	{ API_SectElemID, "SectElem" },
	{ API_DrawingID, "Drawing" },
	{ API_PictureID, "Picture" },
	{ API_DetailID, "Detail" },
	{ API_ElevationID, "Elevation" },
	{ API_InteriorElevationID, "InteriorElevation" },
	{ API_WorksheetID, "Worksheet" },
	{ API_HotlinkID, "Hotlink" },
	{ API_CurtainWallID, "CurtainWall" },
	{ API_CurtainWallSegmentID, "CurtainWallSegment" },
	{ API_CurtainWallFrameID, "CurtainWallFrame" },
	{ API_CurtainWallPanelID, "CurtainWallPanel" },
	{ API_CurtainWallJunctionID, "CurtainWallJunction" },
	{ API_CurtainWallAccessoryID, "CurtainWallAccessory" },
	{ API_ShellID, "Shell" },
	{ API_SkylightID, "Skylight" },
	{ API_MorphID, "Morph" },
	{ API_ChangeMarkerID, "ChangeMarker" },
	{ API_StairID, "Stair" },
	{ API_RiserID, "Riser" },
	{ API_TreadID, "Tread" },
	{ API_StairStructureID, "StairStructure" },
	{ API_RailingID, "Railing" },
	{ API_RailingToprailID, "RailingToprail" },
	{ API_RailingHandrailID, "RailingHandrail" },
	{ API_RailingRailID, "RailingRail" },
	{ API_RailingPostID, "RailingPost" },
	{ API_RailingInnerPostID, "RailingInnerPost" },
	{ API_RailingBalusterID, "RailingBaluster" },
	{ API_RailingPanelID, "RailingPanel" },
	{ API_RailingSegmentID, "RailingSegment" },
	{ API_RailingNodeID, "RailingNode" },
	{ API_RailingBalusterSetID, "RailingBalusterSet" },
	{ API_RailingPatternID, "RailingPattern" },
	{ API_RailingToprailEndID, "RailingToprailEnd" },
	{ API_RailingHandrailEndID, "RailingHandrailEnd" },
	{ API_RailingRailEndID, "RailingRailEnd" },
	{ API_RailingToprailConnectionID, "RailingToprailConnection" },
	{ API_RailingHandrailConnectionID, "RailingHandrailConnection" },
	{ API_RailingRailConnectionID, "RailingRailConnection" },
	{ API_RailingEndFinishID, "RailingEndFinish" },
	{ API_BeamSegmentID, "BeamSegment" },
	{ API_ColumnSegmentID, "ColumnSegment" },
	{ API_OpeningID, "Opening" }
};

static GS::HashTable<API_ElemVariationID, GS::UniString> varIDTable
{
	{ APIVarId_WallEnd, "WallEnd" },
	{ APIVarId_Door, "Door" },
	{ APIVarId_Skylight, "Skylight" },
	{ APIVarId_Object, "Object" },
	{ APIVarId_GridElement, "GridElement" },
	{ APIVarId_Light, "Light" },
	{ APIVarId_CornerWindow, "CornerWindow" }
};


enum class PropertiesMode { Local, Preview };


static GS::ErrorStatus SerializeToJSON (const IO::Location& resultFileLocation, const GS::ObjectState& objectState)
{
	IO::File resultFile (resultFileLocation, IO::File::OnNotFound::Create);
	GS::ErrorStatus status = resultFile.Open (IO::File::OpenMode::WriteEmptyMode);
	if (status.IsOK ())
		status = JSON::CreateFromObjectState (objectState, resultFile, true);

	resultFile.Close ();
	
	return status;
}


static std::optional<GS::UniString> GetNameAttribute (const IFCAPI::ObjectID& objectID)
{
	auto apiAttributes = IFCAPI::PropertyAccessor (objectID).GetAttributes ();
	if (apiAttributes.IsOk ()) {
		auto apiAttributeIt = std::find_if (
			apiAttributes->begin (), apiAttributes->end (),
			[&](const IFCAPI::Attribute& apiAttribute) {
				return apiAttribute.GetName () == "Name";
			}
		);

		if (apiAttributeIt != apiAttributes->end ())
			return apiAttributeIt->GetValue ();
	}

	return std::nullopt;
}


static void AddObjectIDToObjectState (const IFCAPI::ObjectID& objectID, GS::ObjectState& osToAdd)
{
	osToAdd.Add ("Type", IFCAPI::GetObjectAccessor ().GetIFCType (objectID).Unwrap ());

	auto name = GetNameAttribute (objectID);
	if (name.has_value ())
		osToAdd.Add ("Name", *name);

	osToAdd.Add ("GlobalId", IFCAPI::GetObjectAccessor ().GetGlobalId (objectID).Unwrap ());
}


static GS::ObjectState ConvertAssignmentsNodeToObjectState (const IFCAPI::ObjectID& objectId, const std::unordered_map<IFCAPI::ObjectID, std::unordered_set<IFCAPI::ObjectID>>& childObjectTable)
{
	GS::ObjectState os;
	AddObjectIDToObjectState (objectId, os);

	if (childObjectTable.find (objectId) != childObjectTable.end ()) {
		const auto& children = os.AddList<GS::ObjectState> ("Children");
		for (const auto& child : childObjectTable.at (objectId))
			children (ConvertAssignmentsNodeToObjectState (child, childObjectTable));
	}

	return os;
};


static GS::ObjectState ConvertAssignmentsToObjectState (const IFCAPI::Assignments& assignments)
{
	GS::ObjectState os;
	const auto& roots = os.AddList<GS::ObjectState> ("Roots");
	const auto rootGroups = assignments.GetRootGroups ();
	const auto childObjectTable = assignments.GetChildObjectTable ();

	for (const auto& objectId : rootGroups)
		roots (ConvertAssignmentsNodeToObjectState (objectId, childObjectTable));

	return os;
};


static GS::ErrorStatus AddBuildingStories (GS::HashSet<IFCAPI::ObjectID>& result)
{
//![IFCAPI CreateIfcBuildingStoreyObjectID example]
	API_StoryInfo storyInfo;
	GS::ErrorStatus status = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
	if (status.IsOK ()) {
		for (short storyIndex = storyInfo.firstStory; storyIndex <= storyInfo.lastStory; ++storyIndex) {
			auto objectID = IFCAPI::GetObjectAccessor ().CreateIfcBuildingStoreyObjectID (storyIndex);
			if (objectID.IsOk ())
				result.Add (objectID.Unwrap ());
		}
	}

	BMKillHandle ((GSHandle*)&storyInfo.data);
//![IFCAPI CreateIfcBuildingStoreyObjectID example]

	return status;
}


static GS::UniString GetValueTypePostfix (const GS::UniString& valueType)
{
	return GS::UniString::Printf (" (%T)", valueType.ToPrintf ());
}


static GS::ErrorStatus AddClassificationsToOS (const IFCAPI::PropertyAccessor& propertyAccessor, PropertiesMode propertiesMode, GS::ObjectState& objectState)
{
	const auto& classificationReferenceList = objectState.AddList<GS::ObjectState> ("ClassificationReferences");

	auto AddOptionalUniStringToObjectState = [](GS::ObjectState& objectState, const GS::String& fieldName, const std::optional<GS::UniString>& value) {
		if (value.has_value ())
			objectState.Add (fieldName, *value);
	};

	auto AddClassificationReferencesToObjectStateList = [&classificationReferenceList, AddOptionalUniStringToObjectState](const std::vector<IFCAPI::ClassificationReference>& classificationReferences) {
		for (const auto& classificationReference : classificationReferences) {
			GS::ObjectState classificationReferenceObjectState;
			{
				GS::ObjectState classificationObjectState;
				{
					auto classification = classificationReference.GetReferencedSource ();

					AddOptionalUniStringToObjectState (classificationObjectState, "Source", classification.GetSource ());
					AddOptionalUniStringToObjectState (classificationObjectState, "Edition", classification.GetEdition ());
					AddOptionalUniStringToObjectState (classificationObjectState, "EditionDate", classification.GetEditionDate ());
					classificationObjectState.Add ("Name", classification.GetName ());
					AddOptionalUniStringToObjectState (classificationObjectState, "Description", classification.GetDescription ());
					AddOptionalUniStringToObjectState (classificationObjectState, "Location", classification.GetLocation ());
				}

				classificationReferenceObjectState.Add ("IfcRelAssociatesClassification", classificationReference.GetIfcRelAssociatesClassificationName ());
				AddOptionalUniStringToObjectState (classificationReferenceObjectState, "Location", classificationReference.GetLocation ());
				AddOptionalUniStringToObjectState (classificationReferenceObjectState, "Identification", classificationReference.GetIdentification ());
				AddOptionalUniStringToObjectState (classificationReferenceObjectState, "Name", classificationReference.GetName ());
				classificationReferenceObjectState.Add ("ReferencedSource", classificationObjectState);
			}

			classificationReferenceList (classificationReferenceObjectState);
		}
	};

	switch (propertiesMode)
	{
	case PropertiesMode::Local:
	{
//![IFCAPI GetLocalClassificationReferences example]
		auto classificationReferences = propertyAccessor.GetLocalClassificationReferences ();
		if (classificationReferences.IsErr ())
			return ToStatus (classificationReferences.UnwrapErr ());

		AddClassificationReferencesToObjectStateList (classificationReferences.Unwrap ());
//![IFCAPI GetLocalClassificationReferences example]
		return NoError;
	}
	case PropertiesMode::Preview:
	{
//![IFCAPI GetPreviewClassificationReferences example]
		auto classificationReferences = propertyAccessor.GetPreviewClassificationReferences ();
		if (classificationReferences.IsErr ())
			return ToStatus (classificationReferences.UnwrapErr ());

		AddClassificationReferencesToObjectStateList (classificationReferences.Unwrap ());
//![IFCAPI GetPreviewClassificationReferences example]
		return NoError;
	}
	default:
		return APIERR_BADPARS;
	}
}


static GS::ErrorStatus AddAttributesToOS (const IFCAPI::PropertyAccessor& propertyAccessor, GS::ObjectState& objectState)
{
	const auto& OSAdder = objectState.AddList<GS::ObjectState> ("Attributes");
	auto AddAttributeArrayToOS = [&](const std::vector<IFCAPI::Attribute>& attributes) {
		for (const auto& attribute : attributes) {
			GS::ObjectState OSItem;

			GS::UniString name = attribute.GetName () + GetValueTypePostfix (attribute.GetValueType ());
			OSItem.Add ("Name", name);
			auto value = attribute.GetValue ();
			if (value.has_value ())
				OSItem.Add ("Value", value.value ());

			OSAdder (OSItem);
		}
	};

//![IFCAPI GetAttributes example]
	auto attributes = propertyAccessor.GetAttributes ();
	if (attributes.IsErr ())
		return ToStatus (attributes.UnwrapErr ());
//![IFCAPI GetAttributes example]

	AddAttributeArrayToOS (attributes.Unwrap ());
	return NoError;
}


GS::UniString TrueString ("True");
GS::UniString FalseString ("False");
GS::UniString UnknownString ("Unknown");
GS::UniString SeparatorString ("; ");


static GS::UniString PropertyValueToString (const IFCAPI::Value& value)
{
	GS::UniString result;

	auto anyValue = value.GetAnyValue ();
	if (anyValue.has_value ()) {
		if (std::holds_alternative<Int64> (*anyValue)) {
			result.Append (GS::ValueToUniString (std::get<Int64> (*anyValue)));
		} else if (std::holds_alternative<double> (*anyValue)) {
			result.Append (GS::ValueToUniString (std::get<double> (*anyValue)));
		} else if (std::holds_alternative<bool> (*anyValue)) {
			result.Append (std::get<bool> (*anyValue) ? TrueString : FalseString);
		} else if (std::holds_alternative<IFCAPI::IfcLogical> (*anyValue)) {
			switch (std::get<IFCAPI::IfcLogical> (*anyValue)) {
			case IFCAPI::IfcLogical::Unknown: result.Append (UnknownString); break;
			case IFCAPI::IfcLogical::False: result.Append (FalseString); break;
			case IFCAPI::IfcLogical::True: result.Append (TrueString); break;
			}
		} else if (std::holds_alternative<GS::UniString> (*anyValue)) {
			result.Append (std::get<GS::UniString> (*anyValue));
		}
	}

	if (!result.IsEmpty ())
		result.Append (GetValueTypePostfix (value.GetType ()));

	return result;
}


GS::UniString Concatenate (const GS::Array<GS::UniString>& strings, const GS::UniString& separator)
{
	GS::UniString result;

	for (const auto& item : strings) {
		if (!result.IsEmpty ())
			result.Append (separator);

		result.Append (item);
	}

	return result;
}


static GS::UniString ConcatenatePropertyValues (const std::vector<IFCAPI::Value>& values, const GS::UniString& separator)
{
	GS::Array<GS::UniString> valueStrings;

	for (const auto& value: values)
		valueStrings.Push (PropertyValueToString (value));

	return Concatenate (valueStrings, separator);
}


static GS::UniString PropertyBoundedValueToString (const IFCAPI::PropertyBoundedValue& boundedValue)
{
	GS::UniString result;
	GS::UniString lowerBoundValue = PropertyValueToString (boundedValue.GetLowerBoundValue ());
	GS::UniString upperBoundValue = PropertyValueToString (boundedValue.GetUpperBoundValue ());
	GS::UniString setPointValue = PropertyValueToString (boundedValue.GetSetPointValue ());

	if (!lowerBoundValue.IsEmpty () || !upperBoundValue.IsEmpty ()) {
		result.Append (lowerBoundValue);
		result.Append (" ... ");
		result.Append (upperBoundValue);
	}

	if (!setPointValue.IsEmpty ()) {
		if (!result.IsEmpty ())
			result.Append (SeparatorString);
		result.Append (setPointValue);
	}

	return result;
}


static GS::UniString PropertyTableValueToString (const IFCAPI::PropertyTableValue& tableValue)
{
	GS::Array<GS::UniString> names;
	auto definingValues = tableValue.GetDefiningValues ();
	auto definedValues = tableValue.GetDefinedValues ();

	if (!DBVERIFY (definingValues.size () == definedValues.size ()))
		return "";

	for (size_t i = 0; i < definingValues.size (); ++i)
		names.Push (PropertyValueToString (definingValues[i]) + ": " + PropertyValueToString (definedValues[i]));

	return Concatenate (names, SeparatorString);
}


static GS::UniString GetPropertyValueString (const IFCAPI::Property& property)
{
	auto propertyVariant = property.GetTyped ();
	
	if (std::holds_alternative<IFCAPI::PropertySingleValue> (propertyVariant)) {
		const auto& propertyCast = std::get<IFCAPI::PropertySingleValue> (propertyVariant);
		return PropertyValueToString (propertyCast.GetNominalValue ());
	} else if (std::holds_alternative<IFCAPI::PropertyListValue> (propertyVariant)) {
		const auto& propertyCast = std::get<IFCAPI::PropertyListValue> (propertyVariant);
		return ConcatenatePropertyValues (propertyCast.GetListValues (), SeparatorString);
	} else if (std::holds_alternative<IFCAPI::PropertyBoundedValue> (propertyVariant)) {
		const auto& propertyCast = std::get<IFCAPI::PropertyBoundedValue> (propertyVariant);
		return PropertyBoundedValueToString (propertyCast);
	} else if (std::holds_alternative<IFCAPI::PropertyEnumeratedValue> (propertyVariant)) {
		const auto& propertyCast = std::get<IFCAPI::PropertyEnumeratedValue> (propertyVariant);
		GS::Array<GS::UniString> names;
		for (const auto& value : propertyCast.GetEnumerationValues ())
			names.Push (PropertyValueToString (value));
		return Concatenate (names, SeparatorString);
	} else {
		const auto& propertyCast = std::get<IFCAPI::PropertyTableValue> (propertyVariant);
		return PropertyTableValueToString (propertyCast);
	}
}


static GS::ErrorStatus AddPropertiesToOS (const IFCAPI::PropertyAccessor& propertyAccessor, PropertiesMode propertiesMode, GS::ObjectState& objectState)
{
	const auto& OSAdder = objectState.AddList<GS::ObjectState> ("Properties");
	auto AddPropertyArrayToOS = [&OSAdder](const std::vector<IFCAPI::Property>& properties) {
		for (const auto& property : properties) {
			GS::ObjectState OSItem;
			OSItem.Add ("PSet Name", property.GetPropertySetName ());
			OSItem.Add ("Name", property.GetName ());
			OSItem.Add ("Value", GetPropertyValueString (property));
			OSAdder (OSItem);
		}
	};

	switch (propertiesMode)
	{
	case PropertiesMode::Local:
	{
//![IFCAPI GetLocalProperties example]
		auto properties = propertyAccessor.GetLocalProperties ();
		if (properties.IsErr ())
			return ToStatus (properties.UnwrapErr ());

		AddPropertyArrayToOS (properties.Unwrap ());
//![IFCAPI GetLocalProperties example]
		return NoError;
	}
	case PropertiesMode::Preview:
	{
//![IFCAPI GetPreviewProperties example]
		auto properties = propertyAccessor.GetPreviewProperties ();
		if (properties.IsErr ())
			return ToStatus (properties.UnwrapErr ());

		AddPropertyArrayToOS (properties.Unwrap ());
//![IFCAPI GetPreviewProperties example]
		return NoError;
	}
	default:
		return APIERR_BADPARS;
	}
}


static GS::ErrorStatus CollectObjectIDsFromPlan (GS::HashSet<IFCAPI::ObjectID>& objectIDs)
{
	auto AddObjectID = [&](ACAPI::Result<IFCAPI::ObjectID> objectID) {
		if (objectID.IsOk ())
			objectIDs.Add (*objectID);
	};

	//![IFCAPI GetAssignments example]
//![IFCAPI Assignments GetRootGroups example]
//![IFCAPI Assignments GetChildObjectTable example]
	auto assignments = IFCAPI::GetObjectAccessor ().GetAssignments ();

	const auto rootGroups = assignments->GetRootGroups ();
	for (const auto& root : rootGroups)
		objectIDs.Add (root);

	for (const auto& [childKey, childValue] : assignments->GetChildObjectTable ())
		objectIDs.Add (childKey);
//![IFCAPI Assignments GetChildObjectTable example]
//![IFCAPI Assignments GetRootGroups example]
//![IFCAPI GetAssignments example]

	objectIDs.Clear ();

//![IFCAPI CreateIfcProjectObjectID example]
//![IFCAPI CreateIfcSiteObjectID example]
//![IFCAPI CreateIfcBuildingObjectID example]
	AddObjectID (IFCAPI::GetObjectAccessor ().CreateIfcProjectObjectID ());
	AddObjectID (IFCAPI::GetObjectAccessor ().CreateIfcSiteObjectID ());
	AddObjectID (IFCAPI::GetObjectAccessor ().CreateIfcBuildingObjectID ());
//![IFCAPI CreateIfcBuildingObjectID example]
//![IFCAPI CreateIfcSiteObjectID example]
//![IFCAPI CreateIfcProjectObjectID example]

	auto status = AddBuildingStories (objectIDs);

	{
//![IFCAPI CreateElementObjectID example]
		GS::Array<API_Guid> elementGuids;
		ACAPI_Element_GetElemList (API_ElemTypeID::API_ZombieElemID, &elementGuids);

		for (const auto& elementGuid : elementGuids) {
			API_Elem_Head head{};
			head.guid = elementGuid;
			auto objectID = IFCAPI::GetObjectAccessor ().CreateElementObjectID (head);
			AddObjectID (objectID);
		}
//![IFCAPI CreateElementObjectID example]
	}

	return status;
}


static GS::Optional<IO::Location> GetExportJSONFileLocation ()
{
	DG::FileDialog fileDialog (DG::FileDialog::Type::Save);
	FTM::FileTypeManager fManager ("SerializeToJSONFileBrowser");
	FTM::FileType fType ("JSON Files", "json", 'ttxt', 'TEXT', -1);
	FTM::TypeID fTypeID = fManager.AddType (fType);
	fileDialog.AddFilter (fTypeID);
	if (fileDialog.Invoke ())
		return fileDialog.GetSelectedFile ();

	return GS::NoValue;
}


void GetStories ()
{
	auto resultFileLocation = GetExportJSONFileLocation ();
	if (resultFileLocation.HasValue ()) {
		GS::ObjectState objectState;
		const auto& OSAdder = objectState.AddList<GS::ObjectState> ("Stories");

		GS::HashSet<IFCAPI::ObjectID> stories;
		GS::ErrorStatus status = AddBuildingStories (stories);
		if (status.IsOK ()) {
			for (const auto& storey : stories) {
				GS::ObjectState osItem;
				AddObjectIDToObjectState (storey, osItem);
				OSAdder (osItem);
			}

			status = SerializeToJSON (*resultFileLocation, objectState);
		}

		ShowStatusDialog (status, true);
	}
}


void GetAssignmentTree ()
{
	auto resultFileLocation = GetExportJSONFileLocation ();
	if (resultFileLocation.HasValue ()) {
		GS::ErrorStatus status;

		auto assignments = IFCAPI::GetObjectAccessor ().GetAssignments ();
		if (assignments.IsOk ()) {
			auto os = ConvertAssignmentsToObjectState (assignments.Unwrap ());
			status = SerializeToJSON (*resultFileLocation, os);
		} else {
			status = ToStatus (assignments.UnwrapErr ());
		}

		ShowStatusDialog (status, true);
	}
}


void GetAllObjectsProperties (PropertiesMode propertiesMode)
{
	GS::ErrorStatus status;
	IO::Location resultFolderLocation;
	{
		DG::FolderDialog folderDialog;
		if (!folderDialog.Invoke ())
			return;

		resultFolderLocation = folderDialog.GetFolder ();
	}

	GS::Array<API_Guid> failedElements;

//![IFCAPI GetIFCType example]
//![IFCAPI GetGlobalId example]
	GS::HashSet<IFCAPI::ObjectID> objectIDs;
	status = CollectObjectIDsFromPlan (objectIDs);
	if (status.IsOK ()) {
		for (const auto& objectID : objectIDs) {
			auto ifcTypeResult = IFCAPI::GetObjectAccessor ().GetIFCType (objectID);
			if (ifcTypeResult.IsOk ()) {
				auto globalId (IFCAPI::GetObjectAccessor ().GetGlobalId (objectID));
				if (globalId.IsOk ()) {
//![IFCAPI GetGlobalId example]
//![IFCAPI GetIFCType example]
					GS::ErrorStatus itemStatus;
					GS::ObjectState os;
					AddObjectIDToObjectState (objectID, os);

					IFCAPI::PropertyAccessor propertyAccessor = IFCAPI::PropertyAccessor (objectID);

					if (itemStatus.IsOK ())
						itemStatus = AddAttributesToOS (propertyAccessor, os);

					if (itemStatus.IsOK ())
						itemStatus = AddPropertiesToOS (propertyAccessor, propertiesMode, os);

					if (itemStatus.IsOK ())
						itemStatus = AddClassificationsToOS (propertyAccessor, propertiesMode, os);

					if (itemStatus.IsOK ()) {
						IO::Name fileName (globalId.Unwrap ());
						fileName.AppendExtension ("json");
						IO::Name ifcTypeSubFolder (ifcTypeResult.Unwrap ());
						IO::Folder (resultFolderLocation).CreateFolder (ifcTypeSubFolder);
						IO::Location resultFileLocation (resultFolderLocation);
						resultFileLocation.AppendToLocal (ifcTypeSubFolder);
						resultFileLocation.AppendToLocal (fileName);
						itemStatus = SerializeToJSON (resultFileLocation, os);
						if (itemStatus.IsError ()) {
							status = itemStatus;
							break;
						} else {
							continue;
						}
					}
				}
			}
			
			auto elementID = IFCAPI::GetObjectAccessor ().GetAPIElementID (objectID);
			if (elementID.IsOk ())
				failedElements.Push (elementID.Unwrap ());
		}
	}

	{
		GS::ObjectState os;
		const auto& elementsAdder = os.AddList<GS::ObjectState> ("Elements");
		for (const auto& failedElement : failedElements) {
			GS::ObjectState elementOS;
			{
				API_Elem_Head head{};
				head.guid = failedElement;
				if (ACAPI_Element_GetHeader (&head) == NoError) {
					GS::UniString typeString;
					if (DBVERIFY (typeIDTable.ContainsKey (head.type.typeID)))
						typeString.Append (typeIDTable[head.type.typeID]);

					if (head.type.variationID != APIVarId_Generic && DBVERIFY (varIDTable.ContainsKey (head.type.variationID))) {
						typeString.Append (SeparatorString);
						typeString.Append (varIDTable[head.type.variationID]);
					}

					elementOS.Add ("Type", typeString);
				}
			}

			elementOS.Add ("ID", APIGuidToString (failedElement));

			elementsAdder (elementOS);
		}

		IO::Name fileName ("invalid elements");
		fileName.AppendExtension ("json");
		IO::Location resultFileLocation (resultFolderLocation);
		resultFileLocation.AppendToLocal (fileName);
		auto itemStatus = SerializeToJSON (resultFileLocation, os);
	}

	ShowStatusDialog (status, true);
}


void GetAllObjectsLocalProperties ()
{
	GetAllObjectsProperties (PropertiesMode::Local);
}


void GetAllObjectsPreviewProperties ()
{
	GetAllObjectsProperties (PropertiesMode::Preview);
}


void FindElementsByIFCGlobalId ()
{
	UD::NameInputDialog textDialog ("Find by IfcGlobalId", "IfcGlobalId:", "", 22);
	if (textDialog.Invoke ()) {
		IFCAPI::IfcGloballyUniqueId globalId = textDialog.GetText ();
//![IFCAPI GetAPIElementID example]
//![IFCAPI FindElementsByGlobalId example]
		auto elementsResult = IFCAPI::GetObjectAccessor ().FindElementsByGlobalId (globalId);
		GS::ErrorStatus status;
		if (elementsResult.IsOk ()) {
			ACAPI_Selection_DeselectAll ();
			GS::Array<API_Neig> neigs;
			for (const auto& elementID : elementsResult.Unwrap ()) {
				auto elementGuid = IFCAPI::GetObjectAccessor ().GetAPIElementID (elementID);
				if (elementGuid.IsOk ()) {
					API_Neig neig{};
					neig.guid = elementGuid.Unwrap ();
					neigs.Push (neig);
				}
			}

			ACAPI_Selection_Select (neigs, true);
//![IFCAPI FindElementsByGlobalId example]
//![IFCAPI GetAPIElementID example]
		} else {
			status = ToStatus (elementsResult.UnwrapErr ());
		}

		ShowStatusDialog (status, false);
	}
}


void GetContainingGroupsOfSelection ()
{
	API_SelectionInfo info{};
	GS::Array<API_Neig> selection;
	GS::ErrorStatus status = ACAPI_Selection_Get (&info, &selection, false);
	if (status.IsOK ()) {
		GS::HashSet<IFCAPI::ObjectID> objectIDs;
		for (const auto& neig : selection) {
			if (neig.guid != APINULLGuid) {
				API_Elem_Head head{};
				head.guid = neig.guid;
				auto objectID = IFCAPI::GetObjectAccessor ().CreateElementObjectID (head);
				if (objectID.IsOk ())
					objectIDs.Add (objectID.Unwrap ());
			}
		}

		if (!objectIDs.IsEmpty ()) {
			auto resultFileLocation = GetExportJSONFileLocation ();
			if (resultFileLocation.HasValue ()) {
				auto assignments = IFCAPI::GetObjectAccessor ().GetAssignments ();
				if (assignments.IsOk ()) {
//![IFCAPI Assignments GetParentObjectTable example]
					const auto parentObjectTable = assignments->GetParentObjectTable ();

					GS::ObjectState os;
					const auto& objects = os.AddList<GS::ObjectState> ("Objects");

					for (const auto& objectId : objectIDs) {
						GS::ObjectState objectOS;
						AddObjectIDToObjectState (objectId, objectOS);

						const auto& groups = objectOS.AddList<GS::ObjectState> ("Groups");
						if (parentObjectTable.find (objectId) != parentObjectTable.end ()) {
							for (const auto& relation : parentObjectTable.at (objectId)) {
								if (parentObjectTable.find (relation) != parentObjectTable.end ()) {
									for (const auto& group : parentObjectTable.at (relation)) {
//![IFCAPI Assignments GetParentObjectTable example]
										GS::ObjectState groupOS;
										AddObjectIDToObjectState (group, groupOS);
										groups (groupOS);
									}
								}
							}
						}

						objects (objectOS);
					}

					status = SerializeToJSON (*resultFileLocation, os);
				} else {
					status = ToStatus (assignments.UnwrapErr ());
				}
			} else {
				return;
			}
		} else {
			status = GS::ErrorStatus (Error, "No valid selection");
		}
	} else {
		status = GS::ErrorStatus (Error, "No selection");
	}

	ShowStatusDialog (status, true);
}
