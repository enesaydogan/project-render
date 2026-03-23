#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Element_Test.h"

constexpr bool ShowAlertDialog = true;
constexpr bool WriteToSessionReport = false;

// =============================================================================
//
// Hotlink management
//
// =============================================================================

// -----------------------------------------------------------------------------
// Ask the user to click on an element to select a hotlink instance
// -----------------------------------------------------------------------------

static GS::Optional<API_Element>	GetPlacedHotlinkByClickOnElement ()
{
	API_Guid guid = APINULLGuid;

	if (!ClickAnElem ("Click on an element to select a hotlink instance", API_ZombieElemID, nullptr, nullptr, &guid)) {
		ACAPI_WriteReport ("No element was selected", ShowAlertDialog);
		return GS::NoValue;
	}

	API_Element hotlinkElem = {};
	GSErrCode err = ACAPI_Hotlink_GetContainingHotlinkGuid (&guid, &hotlinkElem.header.guid);

	if (err != NoError || hotlinkElem.header.guid == APINULLGuid) {
		ACAPI_WriteReport ("The selected element is not part of any hotlink instance", ShowAlertDialog);
		return GS::NoValue;
	}

	err = ACAPI_Element_Get (&hotlinkElem);
	if (err != NoError) {
		return GS::NoValue;
	}

	return hotlinkElem;
}


static GS::Optional<API_Guid>	CreateHotlinkNode (const IO::Location& sourceFileLoc)
{
	API_HotlinkNode hotlinkNode = {};
	hotlinkNode.type = APIHotlink_Module;
	hotlinkNode.storyRangeType = APIHotlink_AllStories;
	hotlinkNode.sourceLocation = new IO::Location (sourceFileLoc);
	hotlinkNode.refFloorInd = 0;
	GS::ucsncpy (hotlinkNode.name, L("Hotlink created by test add-on"), API_UniLongNameLen - 1);

	GSErrCode err = ACAPI_Hotlink_GetHotlinkStoryInfo (&hotlinkNode);

	if (err != NoError) {
		ACAPI_WriteReport ("Failed to get Hotlink story info from: {%T}", WriteToSessionReport,
							sourceFileLoc.ToLogText ().ToPrintf ());
		return GS::NoValue;
	}

	err = ACAPI_Hotlink_CreateHotlinkNode (&hotlinkNode);

	if (err != NoError) {
		ACAPI_WriteReport ("Failed to create Hotlink node from: {%T}", WriteToSessionReport,
							sourceFileLoc.ToLogText ().ToPrintf ());
		return GS::NoValue;
	}

	ACAPI_WriteReport ("Hotlink node is created successfully: {%T}", WriteToSessionReport,
						APIGuidToString (hotlinkNode.guid).ToPrintf ());
	return hotlinkNode.guid;
}


static API_Tranmat CreateTransformation (const API_Coord3D& origin, const double rotAngle)
{
	API_Tranmat transformation = {};

	const double	co = cos (rotAngle);
	const double	si = sin (rotAngle);
	// we need an orthogonal transformation
	// origin
	transformation.tmx[3]  = origin.x;
	transformation.tmx[7]  = origin.y;
	transformation.tmx[11] = origin.z;
	// X axis
	transformation.tmx[0]  = co;
	transformation.tmx[4]  = si;
	transformation.tmx[8]  = 0.0;
	// Y axis
	transformation.tmx[1]  = -si;
	transformation.tmx[5]  = co;
	transformation.tmx[9]  = 0.0;
	// we need a valid Z axis as well
	transformation.tmx[2]  = 0.0;
	transformation.tmx[6]  = 0.0;
	transformation.tmx[10] = 1.0;

	return transformation;
}


// -----------------------------------------------------------------------------
// Create a hotlink node and place hotlink element
// -----------------------------------------------------------------------------

GSErrCode	Do_CreateHotlink ()
{
	IO::Location sourceFileLoc;
	API_SpecFolderID specID = API_ApplicationFolderID;
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &sourceFileLoc);
	sourceFileLoc.AppendToLocal (IO::Name ("Archicad Examples"));
	sourceFileLoc.AppendToLocal (IO::Name ("Residential House"));
	sourceFileLoc.AppendToLocal (IO::Name ("Residential House.pln"));

	API_Element element = {};
	element.header.type = API_HotlinkID;
	element.header.layer = APIApplicationLayerAttributeIndex;
	element.hotlink.type = APIHotlink_Module;

	element.hotlink.transformation = CreateTransformation ({10.0, 4.0, 0.0}, PI / 6.0);

	element.hotlink.ignoreTopFloorLinks	= true;
	element.hotlink.relinkWallOpenings	= false;
	element.hotlink.adjustLevelDiffs	= false;

	GSErrCode err = ACAPI_CallUndoableCommand ("Element Test - Create Hotlink Node and Place an Instance", [&] () -> GSErrCode {
		const GS::Optional<API_Guid> hotlinkNodeGuid = CreateHotlinkNode (sourceFileLoc);

		if (hotlinkNodeGuid.IsEmpty ()) {
			return APIERR_GENERAL;
		}

		element.hotlink.hotlinkNodeGuid = hotlinkNodeGuid.Get ();

		return ACAPI_Element_Create (&element, nullptr);
	});

	if (err == NoError) {
		ACAPI_WriteReport ("Hotlink instance is created successfully: {%T}", WriteToSessionReport,
							APIGuidToString (element.header.guid).ToPrintf ());
	} else {
		ACAPI_WriteReport ("Failed to Hotlink instance from: {%T}", WriteToSessionReport,
							sourceFileLoc.ToLogText ().ToPrintf ());
	}

	return err;
}


// -----------------------------------------------------------------------------
// Changes the settings of all hotlinks
// -----------------------------------------------------------------------------

GSErrCode	Do_ChangeHotlink ()
{
	GS::Array<API_Guid> elemList;
	ACAPI_Element_GetElemList (API_HotlinkID, &elemList);

	return ACAPI_CallUndoableCommand ("Element Test - Change Hotlink Instances", [&] () -> GSErrCode {
		for (const API_Guid& guid : elemList) {
			API_Element element = {};
			element.header.guid = guid;
			GSErrCode err = ACAPI_Element_Get (&element);

			if (err == NoError) {
				API_Element mask;

				ACAPI_ELEMENT_MASK_CLEAR (mask);
				ACAPI_ELEMENT_MASK_SET (mask, API_HotlinkType, skipNested);
				ACAPI_ELEMENT_MASK_SET (mask, API_HotlinkType, transformation);
				ACAPI_ELEMENT_MASK_SET (mask, API_HotlinkType, hotlinkNodeGuid);

				element.hotlink.skipNested = !element.hotlink.skipNested;
				element.hotlink.transformation = CreateTransformation ({20.0, 8.0, 0.0}, PI / 3.0);

				err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

				if (err == NoError) {
					ACAPI_WriteReport ("Hotlink instance is changed successfully: {%T}", WriteToSessionReport,
										APIGuidToString (element.header.guid).ToPrintf ());
				}
			}

			if (err != NoError) {
				ACAPI_WriteReport ("Failed to change Hotlink instance: {%T}. All previous modifications will be reverted.", WriteToSessionReport,
									APIGuidToString (element.header.guid).ToPrintf ());
				return err;
			}
		}

		return NoError;
	});
}


// -----------------------------------------------------------------------------
// Update the content of a hotlink
// -----------------------------------------------------------------------------

GSErrCode	Do_UpdateHotlink ()
{
	const GS::Optional<API_Element> hotlinkElem = GetPlacedHotlinkByClickOnElement ();

	GSErrCode err = NoError;
	if (hotlinkElem.HasValue ()) {
		err = ACAPI_Hotlink_UpdateHotlinkCache (&hotlinkElem->hotlink.hotlinkNodeGuid);

		if (err == NoError) {
			ACAPI_WriteReport ("Hotlink node is updated successfully: {%T}", WriteToSessionReport,
								APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
		} else {
			ACAPI_WriteReport ("Failed to update Hotlink node: {%T}", WriteToSessionReport,
								APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
		}
	}

	return err;
}


// -----------------------------------------------------------------------------
// Delete a hotlink node with all its hotlinked elements
// -----------------------------------------------------------------------------

GSErrCode	Do_DeleteHotlink ()
{
	const GS::Optional<API_Element> hotlinkElem = GetPlacedHotlinkByClickOnElement ();

	GSErrCode err = NoError;
	if (hotlinkElem.HasValue ()) {
		err = ACAPI_CallUndoableCommand ("Element Test - Delete Hotlink Node", [&] () -> GSErrCode {
			return ACAPI_Hotlink_DeleteHotlinkNode (const_cast<API_Guid*> (&hotlinkElem->hotlink.hotlinkNodeGuid));
		});

		if (err == NoError) {
			ACAPI_WriteReport ("Hotlink reference deleted: {%T}", WriteToSessionReport,
								APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
		} else {
			ACAPI_WriteReport ("Failed to delete Hotlink reference: {%T}", WriteToSessionReport,
								APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
		}
	}

	return err;
}


// -----------------------------------------------------------------------------
// Delete a hotlink node, but keep the hotlinked elements in the project
// -----------------------------------------------------------------------------

GSErrCode	Do_BreakHotlink ()
{
	const GS::Optional<API_Element> hotlinkElem = GetPlacedHotlinkByClickOnElement ();

	GSErrCode err = NoError;
	if (hotlinkElem.HasValue ()) {
		err = ACAPI_CallUndoableCommand ("Element Test - Break Hotlink Node", [&] () -> GSErrCode {
			return ACAPI_Hotlink_BreakHotlinkNode (const_cast<API_Guid*> (&hotlinkElem->hotlink.hotlinkNodeGuid));
		});

		if (err == NoError) {
			ACAPI_WriteReport ("Hotlink reference broken: {%T}", WriteToSessionReport,
								APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
		} else {
			ACAPI_WriteReport ("Failed to break Hotlink reference: {%T}", WriteToSessionReport,
								APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
		}
	}

	return err;
}


// -----------------------------------------------------------------------------
// Print the details of the hotlink nodes in a tree hierarchy
// -----------------------------------------------------------------------------

static GS::UniString	GetLocationDetailsOfHotlinkNode (const API_HotlinkNode& hotlinkNode)
{
	switch (hotlinkNode.sourceType) {
		case APIHotlink_LocalFile:
			return GS::UniString::Printf ("%T (Local File)",
					hotlinkNode.sourceLocation->ToDisplayText ().ToPrintf ());
		case APIHotlink_TWProject:
		case APIHotlink_TWFS: {
			GS::UniString serverUrl;
			GS::UniString projectName;
			GS::UniString userName;
			ACAPI_Teamwork_GetTeamworkProjectDetails (*hotlinkNode.sourceLocation,
															 &serverUrl,
															 &projectName,
															 &userName);
			return GS::UniString::Printf ("%T/%T (Teamwork Project, Server Name: %T, Project Name: %T, User Name: %T)",
					serverUrl.ToPrintf (),
					projectName.ToPrintf (),
					serverUrl.ToPrintf (),
					projectName.ToPrintf (),
					userName.ToPrintf ());
		}
		default:
			return "Unknown";
	}
}

static GS::UniString	GetStringDetailsOfHotlinkNode (const API_Guid& hotlinkNodeGuid)
{
	GS::UniString hotlinkNodeInfo = GS::UniString::Printf ("{%T} - ", APIGuidToString (hotlinkNodeGuid).ToPrintf ());

	API_HotlinkNode hotlinkNode = {};
	hotlinkNode.guid = hotlinkNodeGuid;
	bool enableUnplaced = true;

	GSErrCode err = ACAPI_Hotlink_GetHotlinkNode (&hotlinkNode, &enableUnplaced);
	if (err == NoError) {
		const GS::UniString updateTime = TIGetTimeString (hotlinkNode.updateTime, TI_LONG_DATE_FORMAT | TI_SHORT_TIME_FORMAT);

		hotlinkNodeInfo += GS::UniString::Printf ("Type: %s, Source Location: %T, Update Time: %T",
												  hotlinkNode.type == APIHotlink_Module ? "Hotlink Module" : "XRef",
												  GetLocationDetailsOfHotlinkNode (hotlinkNode).ToPrintf (),
												  updateTime.ToPrintf ());
	} else {
		hotlinkNodeInfo += "Failed to gather details";
	}

	return hotlinkNodeInfo;
}


static GS::UniString	GetTreeNodePrefix (short treeHierarchyLevel)
{
	GS::UniString treeNodePrefix;
	for (short i = 0; i < treeHierarchyLevel; ++i) {
		treeNodePrefix.Append ("\t");
	}
	return treeNodePrefix + "o";
}


static void	DumpHotlinkNodeWithChildren (const API_Guid&								hotlinkNodeGuid,
										 GS::HashTable<API_Guid, GS::Array<API_Guid>>&	hotlinkNodeTree,
										 short											treeHierarchyLevel)
{
	ACAPI_WriteReport ("%T %T", WriteToSessionReport,
						GetTreeNodePrefix (treeHierarchyLevel).ToPrintf (),
						GetStringDetailsOfHotlinkNode (hotlinkNodeGuid).ToPrintf ());

	for (const API_Guid& childNodeGuid : hotlinkNodeTree.Retrieve (hotlinkNodeGuid)) {
		DumpHotlinkNodeWithChildren (childNodeGuid, hotlinkNodeTree, treeHierarchyLevel + 1);
	}
}


void	Do_ListHotlinks ()
{
	ACAPI_WriteReport ("=== Element Test Add-On : List Hotlink Nodes ===", WriteToSessionReport);

	for (API_HotlinkTypeID type : {APIHotlink_Module, APIHotlink_XRef}) {
		API_Guid hotlinkRootNodeGuid = APINULLGuid;
		if (ACAPI_Hotlink_GetHotlinkRootNodeGuid (&type, &hotlinkRootNodeGuid) == NoError) {
			GS::HashTable<API_Guid, GS::Array<API_Guid>> hotlinkNodeTree;
			if (ACAPI_Hotlink_GetHotlinkNodeTree (&hotlinkRootNodeGuid, &hotlinkNodeTree) == NoError) {
				for (const API_Guid& childNodeGuid : hotlinkNodeTree.Retrieve (hotlinkRootNodeGuid)) {
					DumpHotlinkNodeWithChildren (childNodeGuid, hotlinkNodeTree, 0);
				}
			}
		}
	}

	ACAPI_WriteReport ("===========================", WriteToSessionReport);
}


// -----------------------------------------------------------------------------
// List the proxy elements and their source element pairs from the clicked hotlink.
// -----------------------------------------------------------------------------

void	Do_ListHotlinkProxyElements ()
{
	const GS::Optional<API_Element> hotlinkElem = GetPlacedHotlinkByClickOnElement ();

	if (hotlinkElem.IsEmpty ()) {
		return;
	}

	GS::HashTable<API_Guid, API_Guid> proxyElementTable;
	GSErrCode err = ACAPI_Hotlink_GetHotlinkProxyElementTable (const_cast<API_Guid*> (&hotlinkElem->header.guid), &proxyElementTable);

	if (err == NoError) {
		ACAPI_WriteReport ("List of the proxy elements with their source element pairs from Hotlink {%T}:", WriteToSessionReport,
							APIGuidToString (hotlinkElem->header.guid).ToPrintf ());
		for (const auto& pair : proxyElementTable) {
			ACAPI_WriteReport ("\tProxy element: {%T} - Source element: {%T}", WriteToSessionReport,
								APIGuidToString (pair.key).ToPrintf (),
								APIGuidToString (pair.value).ToPrintf ());
		}
	} else {
		ACAPI_WriteReport ("Failed to retrieve the list of Hotlink proxy elements: {%T}", WriteToSessionReport,
							APIGuidToString (hotlinkElem->header.guid).ToPrintf ());
	}
}


// -----------------------------------------------------------------------------
// Modify the clicked hotlink node.
// -----------------------------------------------------------------------------

void	Do_ModifiyHotlinkNode ()
{
	GS::Optional<API_Element> hotlinkElem = GetPlacedHotlinkByClickOnElement ();
	if (!hotlinkElem.HasValue ())
		return;

	API_HotlinkNode hotlinkNode = {};
	hotlinkNode.guid = hotlinkElem->hotlink.hotlinkNodeGuid;

	GSErrCode err = ACAPI_Hotlink_GetHotlinkNode (&hotlinkNode);
	if (err != NoError)
		return;

	if (hotlinkNode.type != APIHotlink_Module)
		return;

	switch (hotlinkNode.storyRangeType)
	{
		case APIHotlink_SingleStory:
			hotlinkNode.storyRangeType = APIHotlink_AllStories;
			break;
		case APIHotlink_AllStories:
		default:
			hotlinkNode.storyRangeType = APIHotlink_SingleStory;
			hotlinkNode.refFloorInd = 0;
			break;
	}

	err = ACAPI_CallUndoableCommand ("Element Test - Modify Hotlink Node", [&] () -> GSErrCode {
		return ACAPI_Hotlink_ModifyHotlinkNode (&hotlinkNode);
	});

	if (err == NoError) {
		ACAPI_WriteReport ("Hotlink node modified: {%T}", WriteToSessionReport,
							APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
	} else {
		ACAPI_WriteReport ("Failed to modify hotlink node: {%T}", WriteToSessionReport,
							APIGuidToString (hotlinkElem->hotlink.hotlinkNodeGuid).ToPrintf ());
	}
}