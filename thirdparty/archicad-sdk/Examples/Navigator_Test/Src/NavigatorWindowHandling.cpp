// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include "NavigatorWindowHandling.hpp"
#include "NavigatorUtility.hpp"
#include "NavigatorDialog.hpp"
#include "Md5.hpp"

namespace {


// === Helper functions ================================================================================================

static GSErrCode GetValidatorInfo (const API_Guid& userRefId, API_WindowValidatorInfo& validatorInfo)
{
	validatorInfo.guid = userRefId;
	NavigatorUtility::GetFloorplanElements (userRefId, validatorInfo.elemList);

	API_NavigatorAddOnViewPointData viewPointData;
	viewPointData.guid = userRefId;
	GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&viewPointData);
	if (DBERROR (err != NoError))
		return err;

	MD5::Generator MD5Generator;
	MD5Generator.Update (*viewPointData.data, BMhGetSize (viewPointData.data));
	MD5::FingerPrint addOnFingerPrint;
	MD5Generator.Finish (addOnFingerPrint);

	validatorInfo.checkSumList.Push (addOnFingerPrint);

	return NoError;
}


static void UpdateContentValidator (const API_Guid& userRefId)
{
	API_WindowValidatorInfo validatorInfo;
	DBVERIFY (GetValidatorInfo (userRefId, validatorInfo) == NoError);

	DBVERIFY (ACAPI_Database_RebuildWindowValidator (&validatorInfo) == NoError);
}


static void UpdateContentValidator (API_WindowValidatorInfo& validatorInfo)
{
	DBVERIFY (ACAPI_Database_RebuildWindowValidator (&validatorInfo) == NoError);
}


static GSErrCode		WindowHandlerCallBack (const API_Guid& userRefId, API_NotifyWindowEventID notifID)
{
	switch (notifID) {
	case APINotifyWindow_Activate: {
		bool contentChanged = true;
		API_WindowValidatorInfo validatorInfo;

		DBVERIFY (GetValidatorInfo (userRefId, validatorInfo) == NoError);
		DBVERIFY (ACAPI_Database_CheckWindowValidator (&validatorInfo, &contentChanged) == NoError);

		if (contentChanged) {
			UpdateContentValidator (validatorInfo);
			NavigatorWindowHandling::APIWindowGuard apiWindowGuard;
			NavigatorUtility::CopyFloorplanElements (userRefId);
		}

		break;
	}
	case APINotifyWindow_Rebuild: {
		UpdateContentValidator (userRefId);

		NavigatorWindowHandling::APIWindowGuard apiWindowGuard;
		NavigatorUtility::CopyFloorplanElements (userRefId);
		break;
	}
	case APINotifyWindow_Close:
		DBVERIFY (ACAPI_Window_DestroyWindowValidator (const_cast<API_Guid*> (&userRefId)) == NoError);
		break;
	}

	return NoError;
}


static GS::Array<AddOnNavigatorViewPointData>	GetRootVPData ()
{
	GS::Array<AddOnNavigatorViewPointData> viewPoints;
	API_Guid rootGuid = RootGuid;
	GS::Array<API_Guid> guids;
	if (ACAPI_Navigator_GetNavigatorVPItemChildren (&rootGuid, &guids) == NoError) {
		for (auto it = guids.Enumerate (); it != nullptr; ++it) {
			API_NavigatorAddOnViewPointData data;
			data.guid = *it;
			if (ACAPI_Navigator_GetNavigatorVPItem (&data) == NoError && data.itemType == API_NavigatorAddOnViewPointNodeID) {
				AddOnNavigatorViewPointData addOnData;
				addOnData.ConvertFromAPINavigatorAddOnViewPointData (data);
				viewPoints.Push (addOnData);
			}
		}
	}

	return viewPoints;
}

}	// namespace


// === Implementation of the .hpp ======================================================================================

NavigatorWindowHandling::APIWindowGuard::APIWindowGuard ()
{
	ACAPI_Window_ResetCurrentDatabase ();
}


NavigatorWindowHandling::APIWindowGuard::~APIWindowGuard ()
{
	GSErrCode err = ACAPI_View_SetZoom ();
	if (err == NoError)
		err = ACAPI_Database_RebuildCurrentDatabase ();
}


GSErrCode	NavigatorWindowHandling::OpenWindow (const API_Guid& guid, bool newWindow)
{
	API_NavigatorAddOnViewPointData	navigatorAddOnViewPointData;
	navigatorAddOnViewPointData.guid = guid;

	GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&navigatorAddOnViewPointData);
	if (err != NoError)
		return err;

	const GS::UniString title = navigatorAddOnViewPointData.displayId + " " + navigatorAddOnViewPointData.displayName;

	GS::Array<API_Guid> wdIds;
	API_WindowTypeID wdType = APIWind_MyDrawID;
	err = ACAPI_Window_GetOwnWindows (&wdType, &wdIds);
	if (err != NoError)
		return err;

	bool wdIdsContainsGuid = wdIds.Contains (guid);
	if (wdIdsContainsGuid || (!newWindow && !wdIds.IsEmpty ())) {
		API_WindowInfo windowInfo = {};
		windowInfo.typeID = APIWind_MyDrawID;
		windowInfo.databaseUnId.elemSetId = guid;

		GS::UTruncate (title.ToUStr ().Get (), windowInfo.title, sizeof (windowInfo.title));

		if (!wdIdsContainsGuid) {
			DBVERIFY (ACAPI_Window_SetWindowId (&wdIds[0], const_cast<API_Guid*> (&guid)) == NoError);
		}

		err = ACAPI_Window_ChangeWindow (&windowInfo);
	} else {
		API_NewWindowPars   windowPars = {};
		windowPars.typeID = APIWind_MyDrawID;
		windowPars.userRefId = guid;

		GS::UTruncate (title.ToUStr ().Get (), windowPars.wTitle, sizeof (windowPars.wTitle));

		err = ACAPI_Window_NewWindow (&windowPars, WindowHandlerCallBack);
		API_WindowValidatorInfo validatorInfo;
		DBVERIFY (GetValidatorInfo (guid, validatorInfo) == NoError);
		DBVERIFY (ACAPI_Database_BuildWindowValidator (&validatorInfo) == NoError);
	}

	return err;
}


GSErrCode NavigatorWindowHandling::OpenTestDialog (API_Guid viewPointID)
{
	GSErrCode err = NoError;

	bool invokeDialog = true;
	while (invokeDialog) {
		invokeDialog = false;
		GS::Array<AddOnNavigatorViewPointData> addOnDatas = GetRootVPData ();
		NavigatorDialog navigatorDialog (ACAPI_GetOwnResModule (), ACAPI_GetOwnResModule (), addOnDatas, viewPointID);
		bool accepted = navigatorDialog.Invoke ();
		if (accepted) {
			err = navigatorDialog.HandlePanelAccepted (&invokeDialog, navigatorDialog.GetTWCloseReason ());
			viewPointID = navigatorDialog.GetSelectedViewPointGuid ();
			RegenerateContentIfAppropriate ();
		} else {
			navigatorDialog.HandleClientCreatedViewPointsOnCancel ();
		}
	}

	return err;
}


void	NavigatorWindowHandling::RegenerateContentIfAppropriate ()
{
	const GS::Optional<API_Guid> refId = [&] {
		API_WindowInfo winInfo;
		if (ACAPI_Window_GetCurrentWindow (&winInfo) != NoError || winInfo.typeID != APIWind_MyDrawID) {
			return GS::Optional<API_Guid> ();
		}
		return GS::Optional<API_Guid> (winInfo.databaseUnId.elemSetId);
	}();

	if (!refId.HasValue ())
		return;

	GS::Array<API_Guid> wdIds;
	API_WindowTypeID wdType = APIWind_MyDrawID;
	GSErrCode err = ACAPI_Window_GetOwnWindows (&wdType, &wdIds);
	if (err != NoError)
		return;

	if (wdIds.Contains (*refId))
		WindowHandlerCallBack (*refId, APINotifyWindow_Activate);
}