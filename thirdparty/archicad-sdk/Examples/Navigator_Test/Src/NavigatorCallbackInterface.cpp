// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include "NavigatorCallbackInterface.hpp"

#include "DG.h"
//#include "StringConversion.hpp"

#include "AddRenameViewPointItemDialog.hpp"
#include "INavigatorCallbackInterface.hpp"
#include "Resources.hpp"

#include "AddOnNavigatorViewPointData.hpp"
#include "NavigatorWindowHandling.hpp"
#include "NavigatorUtility.hpp"

// ---------------------------------- Static -----------------------------------

GSErrCode	NavigatorCallbackInterface::OpenView (const API_Guid& viewPointId, bool newWindow) const
{
	NavigatorWindowHandling::OpenWindow (viewPointId, newWindow);

	NavigatorWindowHandling::APIWindowGuard apiWindowGuard;
	NavigatorUtility::CopyFloorplanElements (viewPointId);

	return NoError;
}


GSErrCode	NavigatorCallbackInterface::OpenSettings (const API_Guid& viewPointID) const
{
	return NavigatorWindowHandling::OpenTestDialog (viewPointID);
}

GSErrCode	NavigatorCallbackInterface::CreateIDFStore (const API_Guid& viewPointID, double scale, double& /*clipBoxWidth*/, double& /*clipBoxHeight*/, GSPtr& idfStore, API_Box& boundingBox, double& /*paddingX*/, double& /*paddingY*/, GS::Array<API_Guid>& elems) const
{
	GSErrCode err = ACAPI_Drawing_StartDrawingData (&scale);
	if (err == NoError) {
		NavigatorUtility::CopyFloorplanElements (viewPointID);
		NavigatorUtility::GetFloorplanElements (viewPointID, elems);
	}
	err = ACAPI_Drawing_StopDrawingData (&idfStore, &boundingBox);

	return err;
}


GSErrCode	NavigatorCallbackInterface::GetElemsForDrawingCheck (const API_Guid& viewPointID, GS::Array<API_Guid>& elems) const
{
	return NavigatorUtility::GetFloorplanElements (viewPointID, elems);
}


GSErrCode	NavigatorCallbackInterface::NewItem (const API_Guid& viewPointID) const
{
	API_NavigatorAddOnViewPointData vpData;
	vpData.guid = viewPointID;
	GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&vpData);
	if (err == NoError) {
		AddRenameViewPointItemDialog newItemDialog (ACAPI_GetOwnResModule (), AddRenameViewPointItemDialog::NewItemDialogType);
		if (newItemDialog.Invoke ()) {
			AddOnNavigatorViewPointData newViewPontData (newItemDialog.GetViewPointID (), newItemDialog.GetViewPointName ());
			API_NavigatorAddOnViewPointData data = newViewPontData.ConvertToAPINavigatorAddOnViewPointData ();
			data.parentGuid = (vpData.itemType == API_NavigatorAddOnViewPointNodeID) ? vpData.parentGuid : viewPointID;
			return ACAPI_Navigator_CreateNavigatorVPItem (&data);
		}
	}

	return err;
}


GSErrCode	NavigatorCallbackInterface::DeleteItem (const GS::Array<API_Guid>& viewPointIDList) const
{
	for (const API_Guid& guid : viewPointIDList) {
		API_NavigatorAddOnViewPointData vp;
		vp.guid = guid;

		GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&vp);
		if (err != NoError)
			return err;

		if (vp.itemType != API_NavigatorAddOnViewPointRootID) {
			err = ACAPI_Navigator_DeleteNavigatorVPItem (&vp.guid);
			if (err != NoError)
				return err;
		}
	}

	return NoError;
}


DG::Icon	NavigatorCallbackInterface::GetIcon (Int32 iconId, IconTWMode iconTWMode, IconLinkness linkness, IconSize /*size*/, IconContext /*context*/) const
{
	if (iconId == static_cast<Int32>(API_NavigatorAddOnViewPointNodeID)) {
		if (iconTWMode == INavigatorCallbackInterface::TWNormal) {
			if (linkness == IconLinkness::Normal)
				return DG::Icon (ACAPI_GetOwnResModule (), NodeIconID);
			else if (linkness == IconLinkness::Link)
				return DG::Icon (ACAPI_GetOwnResModule (), NodeLinkIconID);
		} else if (iconTWMode == INavigatorCallbackInterface::TWReserved) {
			if (linkness == IconLinkness::Normal)
				return DG::Icon (ACAPI_GetOwnResModule (), NodeReservedIconID);
			else if (linkness == IconLinkness::Link)
				return DG::Icon (ACAPI_GetOwnResModule (), NodeLinkTWIconID);
		} else if (iconTWMode == INavigatorCallbackInterface::TWBeforeSend) {
			return DG::Icon (ACAPI_GetOwnResModule (), NodeBeforeSendIconID);
		}
	} else { // group
		if (iconTWMode == INavigatorCallbackInterface::TWNormal) {
			if (linkness == IconLinkness::Normal)
				return DG::Icon (ACAPI_GetOwnResModule (), GroupIconID);
			else if (linkness == IconLinkness::Link)
				return DG::Icon (ACAPI_GetOwnResModule (), GroupLinkIconID);
		} else if (iconTWMode == INavigatorCallbackInterface::TWReserved) {
			if (linkness == IconLinkness::Normal)
				return DG::Icon (ACAPI_GetOwnResModule (), GroupReservedIconID);
			else if (linkness == IconLinkness::Link)
				return DG::Icon (ACAPI_GetOwnResModule (), GroupLinkTWIconID);
		} else if (iconTWMode == INavigatorCallbackInterface::TWBeforeSend) {
			return DG::Icon (ACAPI_GetOwnResModule (), GroupBeforeSendIconID);
		}
	}

	return DG::Icon ();
}


bool	NavigatorCallbackInterface::GetContextMenuCommandString (ContextMenuCommand cmd, const API_Guid& /*vpId*/, GS::UniString& text) const
{
	if (cmd == INavigatorCallbackInterface::DeleteItemCommand) {
		static bool asd = false;
		asd = !asd;
		if (asd) {
			text = "Delete API item";
			return true;
		}
	}

	return false;
}


GSErrCode	NavigatorCallbackInterface::RenameItem (const API_Guid& viewPointID) const
{
	API_NavigatorAddOnViewPointData vpData;
	vpData.guid = viewPointID;
	GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&vpData);
	if (err == NoError && vpData.itemType != API_NavigatorAddOnViewPointRootID) {
		AddRenameViewPointItemDialog renameItemDialog (ACAPI_GetOwnResModule (), AddRenameViewPointItemDialog::RenameItemDialogType, vpData.displayId, vpData.displayName);
		if (renameItemDialog.Invoke ()) {
			vpData.displayId = renameItemDialog.GetViewPointID ();
			vpData.displayName = renameItemDialog.GetViewPointName ();
			ACAPI_Navigator_ChangeNavigatorVPItem (&vpData);
		}
	}

	return NoError;
}


bool	NavigatorCallbackInterface::IsContextMenuCommandEnabled (ContextMenuCommand cmd, const API_Guid& viewPointID) const
{
	// disable delete command for root group
	if (cmd == INavigatorCallbackInterface::DeleteItemCommand) {
		API_NavigatorAddOnViewPointData vpData;
		vpData.guid = viewPointID;
		GSErrCode err = ACAPI_Navigator_GetNavigatorVPItem (&vpData);

		if (err == NoError && vpData.itemType == API_NavigatorAddOnViewPointRootID) {
			return false;
		}
	}

	return true;
}


bool NavigatorCallbackInterface::GetTWCommandMenuString (TWCommandMenu cmd, const API_Guid& /*viewPointId*/, GS::UniString& text) const
{
	if (cmd == INavigatorCallbackInterface::ReserveNode) {
		text = "API Reserve";
		return true;
	} else if (cmd == INavigatorCallbackInterface::ReleaseNode) {
		text = "API Release";
		return true;
	} else if (cmd == INavigatorCallbackInterface::ReleaseGroupChildren) {
		text = "API Release group";
		return true;
	} else if (cmd == INavigatorCallbackInterface::ReserveGroupChildren) {
		text = "API Reserve group";
		return true;
	} else if (cmd == INavigatorCallbackInterface::RequestNode) {
		text = "API Request";
		return true;
	}

	return false;
}


bool NavigatorCallbackInterface::GetTWCommandMailString (TWCommandMail cmd, const API_Guid& /*viewPointId*/, GS::UniString& text) const
{
	if (cmd == INavigatorCallbackInterface::Grant1Mail) {
		text = "API item granted";
		return true;
	} else if (cmd == INavigatorCallbackInterface::Request1Mail) {
		text = "Requesting API item";
		return true;
	}

	return false;
}


GSErrCode NavigatorCallbackInterface::ExecuteMergePostProcess () const
{
	GSErrCode err = NoError;
	short currentUserId = 0;
	bool isTeamwork = NavigatorUtility::IsTeamworkProject (&currentUserId);
	if (!isTeamwork)
		return NoError;

	GS::Array<API_Guid> children;
	err = ACAPI_Navigator_GetNavigatorVPItemChildren (const_cast<API_Guid*> (&RootGuid), &children);
	if (err != NoError)
		return err;

	GS::Array<API_NavigatorAddOnViewPointData> conflictingVpItems;
	GS::HashTable<GS::UniString, API_NavigatorAddOnViewPointData> names;

	for (const auto& childGuid : children) {
		API_NavigatorAddOnViewPointData vpData;
		vpData.guid = childGuid;

		err = ACAPI_Navigator_GetNavigatorVPItem (&vpData);
		if (err != NoError)
			continue;

		GS::UniString currentName = GS::UniString (vpData.displayId + " " + vpData.displayName);
		if (names.ContainsKey (currentName)) {
			short ownerId = 0;
			if (ACAPI_AddOnObject_GetTeamworkOwnerId (childGuid, &ownerId) == NoError && ownerId == currentUserId && NavigatorUtility::HasDeleteModifyRight ()) {
				conflictingVpItems.Push (vpData);
			} else {
				conflictingVpItems.Push (names[currentName]);
			}
		} else {
			names.Add (currentName, vpData);
		}
	}

	for (const auto& vpData : conflictingVpItems) {
		NavigatorUtility::ResolveNameConflict (names, vpData);
	}

	return err;
}