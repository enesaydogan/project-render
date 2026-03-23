// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include "NavigatorUtility.hpp"
#include "NavigatorPalette.hpp"
#include "NavigatorDialog.hpp"

#include "StringConversion.hpp"
#include "Resources.hpp"


// ---------------------------------- Static -----------------------------------
static const char*						OldPostfix = " Old";

GSErrCode		NavigatorUtility::GetFloorplanElements (const API_Guid& guid, GS::Array<API_Guid>& elems)
{
	GS::Array<API_Guid>				elementList;
	API_NavigatorAddOnViewPointData	navigatorAddOnViewPointData;

	navigatorAddOnViewPointData.guid = guid;
	GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&navigatorAddOnViewPointData);
	if (err != NoError)
		return err;

	AddOnNavigatorViewPointData addOnNavigatorViewPointData;
	addOnNavigatorViewPointData.ConvertFromAPINavigatorAddOnViewPointData (navigatorAddOnViewPointData);

	err = ACAPI_Element_GetElemList (addOnNavigatorViewPointData.GetElemType (), &elementList, APIFilt_FromFloorplan);
	if (err != NoError)
		return err;

	UInt32 idx = 0;
	for (auto it = elementList.Enumerate (); it != nullptr && idx < addOnNavigatorViewPointData.GetElemNum (); ++it, ++idx)
		elems.Push (*it);

	return err;
}


GSErrCode		NavigatorUtility::CopyFloorplanElements (const API_Guid& guid)
{
	API_Element					element;
	API_ElementMemo				memo;
	GS::Array<API_Guid>			elementList;
	GSErrCode					err = NoError;

	GetFloorplanElements (guid, elementList);

	for (auto it = elementList.Enumerate (); it != nullptr; ++it) {
		memo = {};
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element, APIFilt_FromFloorplan);
		if (err == NoError) {
			if (element.header.hasMemo) {
				err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_All | APIMemoMask_FromFloorplan);
				if (err != NoError) {
					return err;
				}
			}

			element.header.guid = APINULLGuid;
			err = ACAPI_Element_Create (&element, &memo);
			if (err != NoError)
				return err;

			ACAPI_DisposeElemMemoHdls (&memo);
		}
	}

	return err;
}


void NavigatorUtility::ResolveNameConflict (GS::HashTable<GS::UniString, API_NavigatorAddOnViewPointData> itemNames, API_NavigatorAddOnViewPointData conflictingVpData)
{
	GS::UniString currentName = conflictingVpData.displayName;
	Int32 index = 1;
	while (itemNames.ContainsKey (conflictingVpData.displayId + " " + currentName) && index < MaxInt32) {
		currentName = GS::UniString::Printf ("%T (%d)", conflictingVpData.displayName.ToPrintf (), index++);
	}
	conflictingVpData.displayName = currentName;

	DBVERIFY (ACAPI_Navigator_ChangeNavigatorVPItem (&conflictingVpData) == NoError);
}


bool NavigatorUtility::IsTeamworkProject (short* currentUserId)
{
	API_ProjectInfo	pi = {};

	GSErrCode err = ACAPI_ProjectOperation_Project (&pi);

	if (err == NoError && currentUserId != nullptr)
		*currentUserId = pi.teamwork ? pi.userId : 0;

	return pi.teamwork != 0;
}


bool NavigatorUtility::HasDeleteModifyRight ()
{
	if (!IsTeamworkProject ())
		return true;

	bool hasRight = false;
	ACAPI_Teamwork_GetTWAccessRight (APISchedulesAndIndexesDeleteModify, &hasRight);		// nincs kulon API-s jog, az IES feltetelei a megfeleloek by design
	return hasRight;
}


GSErrCode NavigatorUtility::APINavigatorAddOnViewPointDataMergeHandler (const GS::Array<API_NavigatorAddOnViewPointData>&	sourceVPDataArray)
{
	GS::HashTable<API_Guid, API_Guid>	guidMap;	// "VP GUID in source plan" -> "VP GUID in this plan"

	for (const API_NavigatorAddOnViewPointData& vp : sourceVPDataArray) {
		if (vp.itemType == API_NavigatorAddOnViewPointRootID) {
			DBVERIFY (guidMap.Add (vp.guid, RootGuid));
		}
		else {
			API_NavigatorAddOnViewPointData createdVP = vp;
			createdVP.parentGuid = guidMap[createdVP.parentGuid];

			DBVERIFY (ACAPI_Navigator_CreateNavigatorVPItem (&createdVP) == NoError);

			DBVERIFY (guidMap.Add (vp.guid, createdVP.guid));
		}
	}

	return NoError;
}


GSErrCode NavigatorUtility::APINavigatorAddOnViewPointDataSaveOldFormatHandler (API_FTypeID												/*planFileType*/,
	const GS::Array<API_NavigatorAddOnViewPointData>&		currentFormatVPDataArray,
	GS::Array<API_NavigatorAddOnViewPointData>&				oldFormatVPDataArray)
{
	for (API_NavigatorAddOnViewPointData vpData : currentFormatVPDataArray) {
		vpData.displayName += OldPostfix;
		oldFormatVPDataArray.Push (vpData);
	}

	return NoError;
}


GSErrCode NavigatorUtility::APINavigatorAddOnViewPointDataConvertNewFormatHandler (API_FTypeID											/*planFileType*/,
	const GS::Array<API_NavigatorAddOnViewPointData>&	oldFormatVPDataArray,
	GS::Array<API_NavigatorAddOnViewPointData>&			currentFormatVPDataArray)
{
	for (API_NavigatorAddOnViewPointData vpData : oldFormatVPDataArray) {
		if (vpData.displayName.EndsWith (OldPostfix))
			vpData.displayName.DeleteLast (OldPostfix);

		currentFormatVPDataArray.Push (vpData);
	}

	return NoError;
}


GSErrCode	NavigatorUtility::RegisterNavigatorRootGroup ()
{
	GSErrCode err = NoError;
	GS::Array<API_Guid> roots;
	err = ACAPI_Navigator_GetNavigatorVPRootGroups (&roots);
	if (!roots.IsEmpty ())
		return err;

	API_NavigatorAddOnViewPointData navigatorAddOnViewPointData;
	navigatorAddOnViewPointData.itemType = API_NavigatorAddOnViewPointRootID;
	navigatorAddOnViewPointData.iconId = static_cast<Int32>(API_NavigatorAddOnViewPointRootID);
	navigatorAddOnViewPointData.guid = RootGuid;
	navigatorAddOnViewPointData.displayName = GetLocStr (RootGroupNameStringId);

	err = ACAPI_Navigator_CreateNavigatorVPItem (&navigatorAddOnViewPointData);
	if (err != NoError)
		ACAPI_WriteReport ("APINavigator_CreateNavigatorVPItemID returned error", false);

	return err;
}


API_Guid	GetDeepestChildFolder (const API_Guid& parentId)
{
	API_Guid guid = parentId;
	GS::Array<API_Guid> children;
	GSErrCode err = ACAPI_Navigator_GetNavigatorVPItemChildren (&guid, &children);
	if (err == NoError) {
		for (auto it = children.Enumerate (); it != nullptr; ++it) {
			API_NavigatorAddOnViewPointData vpData;
			vpData.guid = *it;
			err = ACAPI_Navigator_GetNavigatorVPItem (&vpData);
			if (err == NoError) {
				if (vpData.itemType == API_NavigatorAddOnViewPointGroupID)
					return GetDeepestChildFolder (vpData.guid);
			}
		}
	}

	return guid;

}

GSErrCode	NavigatorUtility::CreateNavigatorChildGroup ()
{
	static UIndex counter = 0;
	API_NavigatorAddOnViewPointData navigatorAddOnViewPointData;
	navigatorAddOnViewPointData.parentGuid	= GetDeepestChildFolder (RootGuid);
	navigatorAddOnViewPointData.itemType	= API_NavigatorAddOnViewPointGroupID;
	navigatorAddOnViewPointData.iconId		= static_cast<Int32>(API_NavigatorAddOnViewPointGroupID);
	navigatorAddOnViewPointData.displayName	= GetLocStr (ChildGroupNameStringId) + " " + GS::ValueToUniString (++counter);

	GSErrCode err = ACAPI_Navigator_CreateNavigatorVPItem (&navigatorAddOnViewPointData);
	if (err != NoError) {
		ACAPI_WriteReport ("APINavigator_CreateNavigatorVPItemID returned error", false);
	}

	return err;
}

GSErrCode NavigatorUtility::CloneProjectMapToViewMap (void)
{
	GSErrCode err = NoError;

	// Source item guid
	API_Guid sourceGuid = {};
	{
		API_NavigatorSet set = {};
		set.mapId = API_ProjectMap;

		err = ACAPI_Navigator_GetNavigatorSet (&set);
		if (err != NoError) {
			return err;
		}

		GS::Array<API_NavigatorItem> items;
		API_NavigatorItem item = {};

		item.guid = set.rootGuid;
		item.mapId = API_ProjectMap;

		err = ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items);
		if (err != NoError) {
			return err;
		}

		API_Guid projectNavGuid = {};
		for (const auto& navItem : items) {
			if (navItem.itemType == API_ProjectNavItem) {
				projectNavGuid = navItem.guid;
				break;
			}
		}

		if (projectNavGuid == APINULLGuid) {
			return Error;
		}

		item.guid = projectNavGuid;
		err = ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items);
		if (err != NoError) {
			return err;
		}

		if (items.IsEmpty ()) {
			return Error;
		}

		sourceGuid = items.GetFirst ().guid;
	}

	// Parent item guid
	API_Guid parentGuid = {};
	{
		API_NavigatorSet set = {};
		set.mapId = API_PublicViewMap;

		err = ACAPI_Navigator_GetNavigatorSet (&set);
		if (err != NoError) {
			return err;
		}

		GS::Array<API_NavigatorItem> items;
		API_NavigatorItem item = {};

		item.guid = set.rootGuid;
		item.mapId = API_PublicViewMap;

		err = ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items);
		if (err != NoError) {
			return err;
		}

		if (items.IsEmpty ()) {
			return Error;
		}

		parentGuid = items.GetFirst ().guid;
	}

	API_Guid clonedGuid = {};
	err = ACAPI_Navigator_CloneProjectMapItemToViewMap (&sourceGuid, &parentGuid, &clonedGuid);
	if (err != NoError) {
		return err;
	}

	if (clonedGuid == APINULLGuid) {
		return Error;
	}

	return NoError;
}

void NavigatorUtility::OpenAPINavigator (void)
{
	NavigatorPaletteTestGlobals::GetInstance ().SetNavigatorDialID (DGModelessInit (ACAPI_GetOwnResModule (), NAVIGATOR_TEST_PALETTE_ID, ACAPI_GetOwnResModule (), NavigatorPalette::APINavigator_Handler, 0, 1));
	DGEnableMessage (NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorDialID (), DG_ALL_ITEMS, DG_MSG_DRAGDROP);
}
