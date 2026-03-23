// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _ELEMENT_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	<stdio.h>			/* sprintf */

#include	"ACAPinc.h"

#include	"Notification_Manager.h"
#include	"APICommon.h"
#include	"UniString.hpp"

// ------------------------------ Constants ------------------------------------

// -------------------------------- Types --------------------------------------

// ------------------------------ Variables ------------------------------------

static	bool	allNewElements = false;

// ------------------------------ Prototypes -----------------------------------


// -----------------------------------------------------------------------------
// Convert API_ActTranPars to API_EditPars
// -----------------------------------------------------------------------------
static bool	ActTranPars_To_EditPars (const API_ActTranPars *actTranPars, API_EditPars *editPars)
{
	BNZeroMemory (editPars, sizeof (API_EditPars));
	editPars->withDelete = true;
	editPars->typeID = (API_EditCmdID) 0;

	switch (actTranPars->typeID) {
		case APIEdit_Drag:
			editPars->typeID = APIEdit_Drag;
			editPars->endC.x = actTranPars->theDisp.x;
			editPars->endC.y = actTranPars->theDisp.y;
			editPars->endC.z = actTranPars->theDispZ;
			break;
		case APIEdit_Rotate:
			editPars->typeID = APIEdit_Rotate;
			editPars->origC = actTranPars->theOrigo;
			editPars->begC.x = editPars->origC.x + 1.0;
			editPars->begC.y = editPars->origC.y;
			editPars->endC.x = editPars->origC.x + actTranPars->theCosA;
			editPars->endC.y = editPars->origC.y + actTranPars->theSinA;
			break;
		case APIEdit_Mirror:
			editPars->typeID = APIEdit_Mirror;
			editPars->begC.x = actTranPars->theOrigo.x;
			editPars->begC.y = actTranPars->theOrigo.y;
			editPars->endC.x = actTranPars->theOrigo.x + actTranPars->theAxeVect.x;
			editPars->endC.y = actTranPars->theOrigo.y + actTranPars->theAxeVect.y;
			break;
		case APIEdit_Stretch:
			editPars->typeID = APIEdit_Stretch;
			editPars->begC.x = actTranPars->theOrigo.x;
			editPars->begC.y = actTranPars->theOrigo.y;
			editPars->endC.x = actTranPars->theOrigo.x + actTranPars->theDisp.x;
			editPars->endC.y = actTranPars->theOrigo.y + actTranPars->theDisp.y;
			break;
		case APIEdit_PDirStretch:
			editPars->typeID = APIEdit_PDirStretch;
			editPars->begC.x = actTranPars->theOrigo.x;
			editPars->begC.y = actTranPars->theOrigo.y;
			editPars->endC.x = actTranPars->theOrigo.x + actTranPars->theDisp.x;
			editPars->endC.y = actTranPars->theOrigo.y + actTranPars->theDisp.y;
			break;
		case APIEdit_PHeightStretch:
			editPars->typeID = APIEdit_PHeightStretch;
			editPars->begC.x = actTranPars->theOrigo.x;
			editPars->begC.y = actTranPars->theOrigo.y;
			editPars->endC.x = actTranPars->theOrigo.x + actTranPars->theDisp.x;
			editPars->endC.y = actTranPars->theOrigo.y + actTranPars->theDisp.y;
			break;
		case APIEdit_Resize:
			editPars->typeID = APIEdit_Resize;
			editPars->begC.x = actTranPars->theOrigo.x;
			editPars->begC.y = actTranPars->theOrigo.y;
			editPars->endC.x = actTranPars->theOrigo.x + 1;
			editPars->endC.y = actTranPars->theOrigo.y;
			editPars->endC2.x = actTranPars->theOrigo.x + actTranPars->theRatio;
			editPars->endC2.y = actTranPars->theOrigo.y;
			break;
		case APIEdit_Elevate:
			editPars->typeID = APIEdit_Elevate;
			editPars->endC.z = actTranPars->theDispZ;
			break;
		case APIEdit_VertStretch:
			editPars->typeID = APIEdit_VertStretch;
			editPars->endC.z = actTranPars->theDispZ;
			break;
		case APIEdit_General:
			break;
	}

	return editPars->typeID != (API_EditCmdID) 0;
}	// ActTranPars_To_EditPars


// -----------------------------------------------------------------------------
// Do_ElementEdit
//	edit the linked elements
// -----------------------------------------------------------------------------
static GSErrCode Do_ElementEdit (API_Elem_Head *header)
{
	API_Guid			elemGuid = APINULLGuid;
	GS::Array<API_Guid>	guids_linkTo;
	API_ActTranPars		actTranPars = {};
	API_EditPars		editPars = {};
	GSErrCode			err;

	ACAPI_Notification_GetTranParams (&actTranPars);
	ActTranPars_To_EditPars (&actTranPars, &editPars);

	elemGuid = header->guid;
	err = ACAPI_ElementLink_GetLinks (elemGuid, &guids_linkTo);
	if (guids_linkTo.IsEmpty ())
		return NoError;

	if (err == NoError) {
		const USize nLinks = guids_linkTo.GetSize ();
		GS::Array<API_Neig> elemNeigs;
		elemNeigs.SetSize (nLinks);
		for (UIndex ii = 0; ii < nLinks; ++ii) {
			API_Elem_Head elemHead = {};
			elemHead.guid = guids_linkTo[ii];
			ACAPI_Element_GetHeader (&elemHead);
			ElemHead_To_Neig (&elemNeigs[ii], &elemHead);
		}

		err = ACAPI_Element_Edit (&elemNeigs, editPars);
	}

	return err;
}	// Do_ElementEdit


// -----------------------------------------------------------------------------
// ElementEventHandlerProc
//
// -----------------------------------------------------------------------------
GSErrCode ElementEventHandlerProc (const API_NotifyElementType *elemType)
{
	GSErrCode		err = NoError;
	char			msgStr[256] = {};
	char			elemStr[32] = {};

	if (elemType->notifID == APINotifyElement_BeginEvents || elemType->notifID == APINotifyElement_EndEvents) {
		API_DatabaseInfo api_dbPars;
		BNZeroMemory (&api_dbPars, sizeof (API_DatabaseInfo));
		api_dbPars.databaseUnId = elemType->databaseId;
		ACAPI_Window_GetDatabaseInfo (&api_dbPars);
		sprintf (msgStr, "### Element_Manager: %s notification on database \"%s\"",
				(elemType->notifID == APINotifyElement_BeginEvents) ? "Begin Events" : "End Events",
				(const char *) GS::UniString (api_dbPars.title).ToCStr ());

	} else if (GetElementTypeString (elemType->elemHead.type, elemStr)) {
		char				elemGuidStr[64] = {};
		char				parentElemGuidStr[64] = {};
		API_Element			parentElement;
		API_ElementMemo		parentElementMemo;
		API_ElementUserData	parentUserData;

		BNZeroMemory (&parentElement, sizeof (API_Element));
		BNZeroMemory (&parentElementMemo, sizeof (API_ElementMemo));
		BNZeroMemory (&parentUserData, sizeof (API_ElementUserData));
		ACAPI_Notification_GetParentElement (&parentElement, &parentElementMemo, 0, &parentUserData);
		BMKillHandle (&parentUserData.dataHdl);

		CHTruncate (APIGuid2GSGuid (elemType->elemHead.guid).ToUniString ().ToCStr (), elemGuidStr, sizeof (elemGuidStr));
		CHTruncate (APIGuid2GSGuid (parentElement.header.guid).ToUniString ().ToCStr (), parentElemGuidStr, sizeof (parentElemGuidStr));

		switch (elemType->notifID) {
			case APINotifyElement_New:
						if (!allNewElements)
							break;

						if (parentElement.header.guid != APINULLGuid)
							sprintf (msgStr, "### Element_Manager: <%s> created {%s} as a copy of {%s}", elemStr, elemGuidStr, parentElemGuidStr);
						else
							sprintf (msgStr, "### Element_Manager: <%s> created {%s}", elemStr, elemGuidStr);

						err = ACAPI_Element_AttachObserver (elemType->elemHead.guid);
						if (err == APIERR_LINKEXIST)
							err = NoError;
						break;

			case APINotifyElement_Copy:
						if (!allNewElements)
							break;

						if (parentElement.header.guid != APINULLGuid) {
							sprintf (msgStr, "### Element_Manager: <%s> copied {%s} from {%s}", elemStr, elemGuidStr, parentElemGuidStr);

							err = ACAPI_Element_AttachObserver (elemType->elemHead.guid);
							if (err == APIERR_LINKEXIST)
								err = NoError;
						}
						break;

			case APINotifyElement_Change:
						if (parentElement.header.guid != APINULLGuid)
							sprintf (msgStr, "### Element_Manager: <%s> changed {%s} -> {%s}", elemStr, parentElemGuidStr, elemGuidStr);
						else
							sprintf (msgStr, "### Element_Manager: <%s> window/door changed {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Edit:
						if (parentElement.header.guid != APINULLGuid)
							sprintf (msgStr, "### Element_Manager: <%s> edited {%s} -> {%s}", elemStr, parentElemGuidStr, elemGuidStr);
						else
							sprintf (msgStr, "### Element_Manager: <%s> edited in place {%s}", elemStr, elemGuidStr);

						err = Do_ElementEdit (&parentElement.header);
						break;

			case APINotifyElement_Delete:
						sprintf (msgStr, "### Element_Manager: <%s> deleted {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Undo_Created:
						sprintf (msgStr, "### Element_Manager: undone <%s> created {%s} ", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Undo_Modified:
						sprintf (msgStr, "### Element_Manager: undone <%s> modified {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Undo_Deleted:
						sprintf (msgStr, "### Element_Manager: undone <%s> deleted {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Redo_Created:
						sprintf (msgStr, "### Element_Manager: redone <%s> created {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Redo_Modified:
						sprintf (msgStr, "### Element_Manager: redone <%s> modified {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_Redo_Deleted:
						sprintf (msgStr, "### Element_Manager: redone <%s> deleted {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_PropertyValueChange:
						sprintf (msgStr, "### Element_Manager: <%s> property value changed {%s}", elemStr, elemGuidStr);
						break;

			case APINotifyElement_ClassificationChange:
						sprintf (msgStr, "### Element_Manager: <%s> classification changed {%s}", elemStr, elemGuidStr);
						break;

			default:
						break;
		}
		ACAPI_DisposeElemMemoHdls (&parentElementMemo);
	}

	if (strlen (msgStr) > 0)
		ACAPI_WriteReport (msgStr, false);

	return err;
}	// ElementEventHandlerProc


// ============================================================================
// Do_ElementMonitor
//
//	observe all newly created elements
// ============================================================================
void	Do_ElementMonitor (bool switchOn)
{
	if (switchOn) {
		ACAPI_Element_CatchNewElement (nullptr, ElementEventHandlerProc);			// for all elements
		ACAPI_Element_InstallElementObserver	(ElementEventHandlerProc);			// observe all newly created elements
		allNewElements = true;
	} else {
		ACAPI_Element_CatchNewElement (nullptr, nullptr);
		ACAPI_Element_InstallElementObserver	(nullptr);
		allNewElements = false;
	}

	return;
}	// Do_ElementMonitor


// ============================================================================
// Do_ClickedElementMonitor
//
//	observe clicked element
// ============================================================================
void	Do_ClickedElementMonitor (bool switchOn)
{
	API_Guid	elemGuid;
	GSErrCode	err = NoError;
	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, nullptr, &elemGuid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	if (switchOn) {
		ACAPI_Element_InstallElementObserver	(ElementEventHandlerProc);
		err = ACAPI_Element_AttachObserver (elemGuid);
		if (err == APIERR_LINKEXIST)
			err = NoError;
	} else {
		err = ACAPI_Element_DetachObserver (elemGuid);
	}

	return;
}	// Do_ClickedElementMonitor


// ============================================================================
// Do_ListMonitoredElements
//
//	list observed elements
// ============================================================================
void	Do_ListMonitoredElements (void)
{
	GS::Array<API_Elem_Head>	ppHeads;

	GSErrCode err = ACAPI_Notification_GetObservedElements (&ppHeads);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Notification_GetObservedElements", err);
		return;
	}

	for (auto& refHead : ppHeads) {
		WriteReport ("%s guid=%s", ElemID_To_Name (refHead.type).ToCStr (CC_UTF8).Get (),
		             			   APIGuidToString (refHead.guid).ToCStr ().Get ());
	}
}	// Do_ListMonitoredElements
