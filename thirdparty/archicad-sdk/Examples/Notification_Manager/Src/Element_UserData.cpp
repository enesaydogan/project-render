// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************

#include "APIEnvir.h"
#define	_ELEMENT_USERDATA_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"ACAPinc.h"					// also includes APIdefs.h

#include	"APICommon.h"
#include	"Notification_Manager.h"
#include	"UniString.hpp"
#include	"APIdefs_AnalyticalElements.h"
#include	"ACAPI/MEPAdapter.hpp"


// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------

// =============================================================================
//
// Assign custom data to elements
//
// =============================================================================

// -----------------------------------------------------------------------------
// Add user data to all selected elements
//   - store element index
//   - data marked to drop automatically on editing
// -----------------------------------------------------------------------------

void		Do_MarkSelElems (void)
{
	API_SelectionInfo 	selectionInfo;
	API_Elem_Head		elemHead;
	API_ElementUserData userData;
	GS::Array<API_Neig>	selNeigs;
	GSErrCode			err;

	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}

	if (selectionInfo.typeID == API_SelEmpty) {
		WriteReport_Alert ("No selected elements");
		return;
	}

	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	BNZeroMemory (&elemHead, sizeof (API_Elem_Head));
	BNZeroMemory (&userData, sizeof (API_ElementUserData));

	userData.dataVersion = 1;
	userData.platformSign = GS::Act_Platform_Sign;
	userData.dataHdl = BMAllocateHandle (0, ALLOCATE_CLEAR, 0);
	if (userData.dataHdl == nullptr)
		return;

	char	timeStr[128];
	TIGetTimeString (TIGetTime (), timeStr, TI_SHORT_DATE_FORMAT | TI_SHORT_TIME_FORMAT);

	//SpeedTest
	TIReset (0, "ACAPI_Element_SetUserData - SpeedTest");
	TIStart (0);

	for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
		elemHead.type = Neig_To_ElemID (selNeigs[i].neigID);
		elemHead.guid = selNeigs[i].guid;

		//--------------------------------------------------------
		char dataStr [300];
		sprintf (dataStr, "%s GUID:%s <%s>", ElemID_To_Name (elemHead.type).ToCStr (CC_UTF8).Get (),
		         							 APIGuidToString (elemHead.guid).ToCStr ().Get (), timeStr);
		Int32 dataLen = Strlen32 (dataStr) + 1;
		userData.dataHdl = BMReallocHandle (userData.dataHdl, dataLen, REALLOC_FULLCLEAR, 0);
		if (userData.dataHdl == nullptr)
			break;					// error handling...

		CHCopyC (dataStr, *userData.dataHdl);

		userData.flags = APIUserDataFlag_FillWith | APIUserDataFlag_Pickup;

		err = ACAPI_Element_SetUserData (&elemHead, &userData);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_SetUserData", err);

	}

	//SpeedTest
	TIStop (0);
	TIPrintTimers ();

	BMKillHandle (&userData.dataHdl);
}		// Do_MarkSelElems


// -----------------------------------------------------------------------------
// List the user data of all selected elements
//   - element index was stored
//   - data marked to drop automatically on editing
// -----------------------------------------------------------------------------

static void LogUserDataForElem (const API_ElementUserData& userData, const GS::UniString& typeID, const API_Guid& elemGuid)
{
	char	dataStr[256];
	CHTruncate (*userData.dataHdl, dataStr, 256);
	WriteReport ("Userdata in %s {%s}: \"%s\" (version: %d, platform: %s)",
					typeID.ToCStr (CC_UTF8).Get (),
					APIGuidToString (elemGuid).ToCStr ().Get (),
					dataStr,
					userData.dataVersion,
					(userData.platformSign == GS::Win_Platform_Sign) ? "Win" : "Mac");
}

void LogUserDataForElemList (const GS::Array<API_Guid>& elemList, const GS::UniString& typeID)
{
	for (auto elemGuid : elemList) {
		API_Elem_Head elemHead = {};
		API_ElementUserData	userData = {};
		elemHead.guid = elemGuid;
		GSErrCode err = ACAPI_Element_GetUserData (&elemHead, &userData);
		if (err == NoError) {
			LogUserDataForElem (userData, typeID, elemGuid);
			BMKillHandle (&userData.dataHdl);
		}
	}
}


static API_ElemTypeID operator++ (API_ElemTypeID& typeID) {
	typeID = static_cast<API_ElemTypeID>(static_cast<Int32>(typeID) + 1);
	return typeID;
}


void		Do_ListOwnedElements (void)
{
	//SpeedTest
	TIReset (1, "ACAPI_Element_GetUserData - SpeedTest");
	TIStart (1);

	for (API_ElemTypeID typeID = API_FirstElemType; typeID != API_LastElemType; ++typeID) {
		GS::Array<API_Guid> elemList;
		ACAPI_Element_GetElemList (typeID, &elemList);
		LogUserDataForElemList (elemList, ElemID_To_Name (typeID));
	}

	WriteReport ("Userdata in Analytical Elements");

	GS::Array<API_Guid> AnalyticalElementTypes = {
		API_AnalyticalLinkID,
		API_AnalyticalSupportID,
		API_AnalyticalPointLoadID,
		API_AnalyticalEdgeLoadID,
		API_AnalyticalSurfaceLoadID
	};

	for (const auto classID : AnalyticalElementTypes) {
		GS::Array<API_Guid> linkElements;
		ACAPI_Element_GetElemList (classID, &linkElements);
		LogUserDataForElemList (linkElements, ElemID_To_Name (classID));
	}

	WriteReport ("Userdata in MEP Elements");

	GS::Array<API_Guid> MEPElementTypes = {
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

	for (const auto classID : MEPElementTypes) {
		GS::Array<API_Guid> MEPelements;
		ACAPI_Element_GetElemList (classID, &MEPelements);
		LogUserDataForElemList (MEPelements, ElemID_To_Name (classID));
	}




	//SpeedTest
	TIStop (1);
	TIPrintTimers ();
}		// Do_ListOwnedElements


// -----------------------------------------------------------------------------
// Attach a URL to an existing element
//	 - url is stored with the user data mechanism
// -----------------------------------------------------------------------------

void		Do_AttachElementURLRef (void)
{
	API_Elem_Head		elemHead;
	API_ElemURLRef		urlRef;
	GSErrCode			err;

	BNZeroMemory (&elemHead, sizeof (API_Elem_Head));
	BNZeroMemory (&urlRef, sizeof (API_ElemURLRef));

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &elemHead.type, &elemHead.guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	CHTruncate ("http://www.graphisoft.com/ftp/gdl/GDLPubDemo/Watch/object.html", urlRef.urlLink, sizeof (urlRef.urlLink));
	CHTruncate ("Graphisoft - Object Technology - Demo Zone", urlRef.description, sizeof (urlRef.description));

	err = ACAPI_Element_SetElementURLRef (&elemHead, &urlRef);
	if (err != NoError)
		ErrorBeep ("APIDb_SetElementURLRef", err);

	return;
}		// Do_AttachElementURLRef


// -----------------------------------------------------------------------------
// Retrieve the URL assigned to an element
// -----------------------------------------------------------------------------

void		Do_ListElementURLRef (void)
{
	API_Elem_Head		elemHead;
	API_ElemURLRef		urlRef;
	GSErrCode			err;

	BNZeroMemory (&elemHead, sizeof (API_Elem_Head));
	BNZeroMemory (&urlRef, sizeof (API_ElemURLRef));

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &elemHead.type, &elemHead.guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	err = ACAPI_Element_GetElementURLRef (&elemHead, &urlRef);
	if (err == NoError) {
		WriteReport ("%s GUID:%s", ElemID_To_Name (elemHead.type).ToCStr (CC_UTF8).Get (),
		             			   APIGuidToString (elemHead.guid).ToCStr ().Get ());
		WriteReport ("  URL:  %s", urlRef.urlLink);
		WriteReport ("  desc: %s", urlRef.description);
	} else if (err == APIERR_NOUSERDATA) {
		WriteReport_Alert ("No URL linked to the element");
	} else {
		ErrorBeep ("APIDb_GetElementURLRef", err);
	}
}		// Do_ListElementURLRef


// -----------------------------------------------------------------------------
// Link the selected elements to the clicked element
// Remove links of clicked element if no selection
// -----------------------------------------------------------------------------

void		Do_LinkElements (void)
{
	API_Element			element;
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;
	GSErrCode			err;

	element = {};
	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	//-------------------------------------------------
	const API_ElemType   typeIDFrom	= element.header.type;
	const API_Guid		 linkFrom	= element.header.guid;
	GS::Array<API_Guid>	 guids_linkTo;
	GS::UniString		 linkList;

	err = ACAPI_ElementLink_GetLinks (linkFrom, &guids_linkTo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_ElementLink_GetLinks", err);
		return;
	}

	for (const API_Guid& guid_linkTo : guids_linkTo) {
		GSFlags	linkFlags = 0;
		err = ACAPI_ElementLink_GetLinkFlags (linkFrom, guid_linkTo, &linkFlags);
		if (err != NoError)
			linkFlags = 0xFFFFFFFF;

		char flagString[13] = { 0 };
		sprintf (flagString, "[0x%08x]", static_cast<unsigned int> (linkFlags));

		if (!linkList.IsEmpty ()) {
			linkList += GS::UniString (", ");
		}
		linkList += APIGuid2GSGuid (guid_linkTo).ToUniString ();
		linkList += GS::UniString (" ");
		linkList += GS::UniString (flagString);

		err = ACAPI_ElementLink_Unlink (linkFrom, guid_linkTo);
		if (err != NoError) {
			ErrorBeep ("ACAPI_ElementLink_Unlink", err);
		}
	}

	if (guids_linkTo.GetSize () > 0)
		WriteReport ("LINK - Elements previously linked to %s {%s}: %s",
					 ElemID_To_Name (typeIDFrom).ToCStr (CC_UTF8).Get (),
					 APIGuidToString (linkFrom).ToCStr ().Get (),
					 linkList.ToCStr (CC_UTF8).Get ());
	else
		WriteReport ("LINK - No elements were previously linked to %s {%s}",
					 ElemID_To_Name (typeIDFrom).ToCStr (CC_UTF8).Get (),
					 APIGuidToString (linkFrom).ToCStr ().Get ());

	//-------------------------------------------------
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}

	Int32 nLinks = 0;
	linkList.Clear ();

	if (selectionInfo.typeID != API_SelEmpty) {
		for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
			err = ACAPI_ElementLink_Link (linkFrom, selNeigs[i].guid, 0);
			if (err == NoError) {
				if (!linkList.IsEmpty ()) {
					linkList += GS::UniString (", ");
				}
				linkList += APIGuid2GSGuid (selNeigs[i].guid).ToUniString ();

				nLinks++;
			}
		}
	}

	if (nLinks > 0)
		WriteReport ("       Elements currently linked to %s {%s}: %s",
					 ElemID_To_Name (typeIDFrom).ToCStr (CC_UTF8).Get (),
					 APIGuidToString (linkFrom).ToCStr ().Get (),
					 linkList.ToCStr (CC_UTF8).Get ());
	else
		WriteReport ("       No elements are currently linked to %s {%s}",
					 ElemID_To_Name (typeIDFrom).ToCStr (CC_UTF8).Get (),
					 APIGuidToString (linkFrom).ToCStr ().Get ());

	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);
}		// Do_LinkElements
