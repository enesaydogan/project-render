// *****************************************************************************
// AddOn to test the API interface (Trim Elements)
//
// Classes:        Contact person:
//
// [SG compatible]
// *****************************************************************************

#define _ELEMENT_TRIM_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Element_Test.h"


// ------------------------------ Constants ------------------------------------

// -------------------------------- Types --------------------------------------

// ------------------------------ Variables ------------------------------------

// ------------------------------ Prototypes -----------------------------------

static	char *GetULongStr (char		*uLStr,
						   UInt32	uLong)
{
	Int32	*lp;
	char	t;

	lp  = (Int32 *) uLStr;
	*lp = uLong; uLStr[4] = 0;

	t = uLStr[0]; uLStr[0] = uLStr[3]; uLStr[3] = t;
	t = uLStr[1]; uLStr[1] = uLStr[2]; uLStr[2] = t;

	return (uLStr);
}


void Do_Trim_Elements (void)
{
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;
	API_Element			element;

	// Get selection
	GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}
	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	GS::Array<API_Guid> guid_ElementsToTrim;

	for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
		// Get selected element
		BNZeroMemory (&element, sizeof(element));
		element.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&element) != NoError)
			continue;
		API_ElemType	tmpType = element.header.type;
		API_Guid		tmpGuid = element.header.guid;
		if (tmpType == API_SectElemID) {
			tmpType = element.sectElem.parentType;
			tmpGuid = element.sectElem.parentGuid;
		}

		if (tmpType == API_WallID || tmpType == API_ColumnID || tmpType == API_BeamID ||
			tmpType == API_WindowID || tmpType == API_DoorID || tmpType == API_SlabID ||
			tmpType == API_RoofID || tmpType == API_CurtainWallID || tmpType == API_ShellID ||
			tmpType == API_SkylightID)
			guid_ElementsToTrim.Push (tmpGuid);
	}

	if (!guid_ElementsToTrim.IsEmpty ()) {
		GSErrCode err = ACAPI_Element_Trim_Elements (guid_ElementsToTrim);
		if (err != NoError) {
			if (err == APIERR_NO3D)
				WriteReport_Alert ("Elements To Trim can be construction elements only.");
			else
				WriteReport_Alert ("ACAPI_Element_Trim_Elements failed: %d", err);
		}
	} else {
		WriteReport_Alert ("There are no selected construction elements!\n");
	}

	return;
}		/* Do_Trim_Elements */


void Do_Trim_ElementsWith (void)
{
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;

	// Get selection
	GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}
	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	GS::Array<API_Guid> guid_ElementsToTrim;

	API_Element	tmpElement;
	for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
		// Get selected element
		BNZeroMemory (&tmpElement, sizeof(tmpElement));
		tmpElement.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&tmpElement) != NoError)
			continue;
		API_ElemType	tmpType = tmpElement.header.type;
		API_Guid		tmpGuid = tmpElement.header.guid;
		if (tmpType == API_SectElemID) {
			tmpType = tmpElement.sectElem.parentType;
			tmpGuid = tmpElement.sectElem.parentGuid;
		}

		if (tmpType == API_WallID || tmpType == API_ColumnID || tmpType == API_BeamID ||
			tmpType == API_WindowID || tmpType == API_DoorID || tmpType == API_SlabID ||
			tmpType == API_RoofID || tmpType == API_CurtainWallID || tmpType == API_ShellID ||
			tmpType == API_SkylightID)
			guid_ElementsToTrim.Push (tmpGuid);
	}

	if (!guid_ElementsToTrim.IsEmpty ()) {
		API_ElemType	type;
		API_Guid		guid_Element;

		if (!ClickAnElem ("Click on a Roof or Shell to Trim with!", API_ZombieElemID, nullptr, &type, &guid_Element)) {
			WriteReport_Alert ("No element was clicked.");
			return;
		}

		BNZeroMemory (&tmpElement, sizeof(tmpElement));
		tmpElement.header.guid = guid_Element;
		if (ACAPI_Element_Get (&tmpElement) != NoError) {
			WriteReport_Alert ("No element was clicked.");
			return;
		}
		type = tmpElement.header.type;
		guid_Element = tmpElement.header.guid;
		if (type == API_SectElemID) {
			type = tmpElement.sectElem.parentType;
			guid_Element = tmpElement.sectElem.parentGuid;
		}

		if (type != API_RoofID && type != API_ShellID) {
			WriteReport_Alert ("No Roof or Shell was clicked to Trim with.");
			return;
		}

		GSErrCode err = ACAPI_Element_Trim_ElementsWith (guid_ElementsToTrim, guid_Element, APITrim_KeepInside);
		if (err != NoError) {
			if (err == APIERR_NO3D)
				WriteReport_Alert ("Elements To Trim can be construction elements only.");
			else
				WriteReport_Alert ("ACAPI_Element_Trim_ElementsWith failed: %d", err);
		}
	} else {
		WriteReport_Alert ("There are no selected construction elements!\n");
	}

	return;
}		/* Do_Trim_ElementsWith */


void Do_Trim_Remove (void)
{
	API_ElemType	typeID1;
	API_Guid		guid_Element1;

	if (!ClickAnElem ("Click on a construction element to remove the connection!", API_ZombieElemID, nullptr, &typeID1, &guid_Element1)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	API_Element	tmpElement;
	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element1;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID1 = tmpElement.header.type;
	guid_Element1 = tmpElement.header.guid;
	if (typeID1 == API_SectElemID) {
		typeID1 = tmpElement.sectElem.parentType;
		guid_Element1 = tmpElement.sectElem.parentGuid;
	}

	if (typeID1 != API_WallID && typeID1 != API_ColumnID && typeID1 != API_BeamID &&
		typeID1 != API_WindowID && typeID1 != API_DoorID && typeID1 != API_SlabID &&
		typeID1 != API_RoofID && typeID1 != API_CurtainWallID && typeID1 != API_ShellID &&
		typeID1 != API_SkylightID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	API_ElemType	typeID2;
	API_Guid		guid_Element2;
	if (!ClickAnElem ("Click on a second construction element to remove the connection with the first!", API_ZombieElemID, nullptr, &typeID2, &guid_Element2)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element2;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID2 = tmpElement.header.type;
	guid_Element2 = tmpElement.header.guid;
	if (typeID2 == API_SectElemID) {
		typeID2 = tmpElement.sectElem.parentType;
		guid_Element2 = tmpElement.sectElem.parentGuid;
	}

	if (typeID2 != API_WallID && typeID2 != API_ColumnID && typeID2 != API_BeamID &&
		typeID2 != API_WindowID && typeID2 != API_DoorID && typeID2 != API_SlabID &&
		typeID2 != API_RoofID && typeID2 != API_CurtainWallID && typeID2 != API_ShellID &&
		typeID2 != API_SkylightID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	GSErrCode err = ACAPI_Element_Trim_Remove (guid_Element1, guid_Element2);
	if (err != NoError) {
		if (err == APIERR_NO3D)
			WriteReport_Alert ("Only construction elements have trimming connection!");
		else if (err == APIERR_BADELEMENTTYPE)
			WriteReport_Alert ("One of the selected two elements should be Roof or Shell!");
		else
			WriteReport_Alert ("ACAPI_Element_Trim_Remove failed: %d", err);
	}

	return;
}		/* Do_Trim_Remove */


void Do_Trim_GetTrimType (void)
{
	API_ElemType	typeID1;
	API_Guid		guid_Element1;

	if (!ClickAnElem ("Click on a construction element to get the connection type!", API_ZombieElemID, nullptr, &typeID1, &guid_Element1)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	API_Element	tmpElement;
	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element1;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID1 = tmpElement.header.type;
	guid_Element1 = tmpElement.header.guid;
	if (typeID1 == API_SectElemID) {
		typeID1 = tmpElement.sectElem.parentType;
		guid_Element1 = tmpElement.sectElem.parentGuid;
	}

	if (typeID1 != API_WallID && typeID1 != API_ColumnID && typeID1 != API_BeamID &&
		typeID1 != API_WindowID && typeID1 != API_DoorID && typeID1 != API_SlabID &&
		typeID1 != API_RoofID && typeID1 != API_CurtainWallID && typeID1 != API_ShellID &&
		typeID1 != API_SkylightID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	API_ElemType	typeID2;
	API_Guid		guid_Element2;
	if (!ClickAnElem ("Click on a second construction element to get the connection type with the first!", API_ZombieElemID, nullptr, &typeID2, &guid_Element2)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element2;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID2 = tmpElement.header.type;
	guid_Element2 = tmpElement.header.guid;
	if (typeID2 == API_SectElemID) {
		typeID2 = tmpElement.sectElem.parentType;
		guid_Element2 = tmpElement.sectElem.parentGuid;
	}

	if (typeID2 != API_WallID && typeID2 != API_ColumnID && typeID2 != API_BeamID &&
		typeID2 != API_WindowID && typeID2 != API_DoorID && typeID2 != API_SlabID &&
		typeID2 != API_RoofID && typeID2 != API_CurtainWallID && typeID2 != API_ShellID &&
		typeID2 != API_SkylightID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	API_TrimTypeID	trimType = APITrim_No;
	GSErrCode err = ACAPI_Element_Trim_GetTrimType (guid_Element1, guid_Element2, &trimType);
	if (err != NoError) {
		if (err == APIERR_NO3D)
			WriteReport_Alert ("Only construction elements have trimming connection!");
		else if (err == APIERR_BADELEMENTTYPE)
			WriteReport_Alert ("One of the selected two elements should be Roof or Shell!");
		else
			WriteReport_Alert ("ACAPI_Element_Trim_GetTrimType failed: %d", err);
	} else {
		char	str[256];
		char	trimstr[16];
		sprintf (str, "  Element1: %s  Element2: %s  TrimType: %s",
					(const char *) APIGuid2GSGuid (guid_Element1).ToUniString ().ToCStr (),
					(const char *) APIGuid2GSGuid (guid_Element2).ToUniString ().ToCStr (),
					GetULongStr (trimstr, trimType));
		WriteReport (str);
	}

	return;
}		/* Do_Trim_GetTrimType */


void Do_Trim_GetTrimmedElements (void)
{
	API_ElemType	typeID;
	API_Guid		guid_Element;

	if (!ClickAnElem ("Click on a Roof or Shell element to get the trimmed elements!", API_ZombieElemID, nullptr, &typeID, &guid_Element)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	API_Element	tmpElement;
	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID = tmpElement.header.type;
	guid_Element = tmpElement.header.guid;
	if (typeID == API_SectElemID) {
		typeID = tmpElement.sectElem.parentType;
		guid_Element = tmpElement.sectElem.parentGuid;
	}
	if (typeID != API_RoofID && typeID != API_ShellID) {
		WriteReport_Alert ("No Roof or Shell element was clicked!\n");
		return;
	}

	GS::Array<API_Guid>	guid_TrimmedElements;
	ACAPI_Element_Trim_GetTrimmedElements (guid_Element, &guid_TrimmedElements);
	for (UIndex i = 0; i < guid_TrimmedElements.GetSize (); i++) {
		char	str[256];
		sprintf (str, "  Element: %s  trims Element: %s",
					(const char *) APIGuid2GSGuid (guid_Element).ToUniString ().ToCStr (),
					(const char *) APIGuid2GSGuid (guid_TrimmedElements [i]).ToUniString ().ToCStr ());
		WriteReport (str);
	}
	return;
}		/* Do_Trim_GetTrimmedElements */


void Do_Trim_GetTrimmingElements (void)
{
	API_ElemType	type;
	API_Guid		guid_Element;

	if (!ClickAnElem ("Click on a Roof or Shell element to get the trimming elements!", API_ZombieElemID, nullptr, &type, &guid_Element)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	API_Element	tmpElement;
	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	type = tmpElement.header.type;
	guid_Element = tmpElement.header.guid;
	if (type == API_SectElemID) {
		type = tmpElement.sectElem.parentType;
		guid_Element = tmpElement.sectElem.parentGuid;
	}

	if (type != API_WallID && type != API_ColumnID && type != API_BeamID &&
		type != API_WindowID && type != API_DoorID && type != API_SlabID &&
		type != API_RoofID && type != API_CurtainWallID && type != API_ShellID &&
		type != API_SkylightID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	GS::Array<API_Guid>	guid_TrimmingElements;
	ACAPI_Element_Trim_GetTrimmingElements (guid_Element, &guid_TrimmingElements);
	for (UIndex i = 0; i < guid_TrimmingElements.GetSize (); i++) {
		char	str[256];
		sprintf (str, "  Element: %s  is trimmmed by Element: %s",
					(const char *) APIGuid2GSGuid (guid_Element).ToUniString ().ToCStr (),
					(const char *) APIGuid2GSGuid (guid_TrimmingElements [i]).ToUniString ().ToCStr ());
		WriteReport (str);
	}
	return;
}		/* Do_Trim_GetTrimmingElements */


void Do_Merge_Elements (void)
{
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;
	API_Element			element;

	// Get selection
	GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}
	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	GS::Array<API_Guid> guid_ElementsToMerge;

	for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
		// Get selected element
		BNZeroMemory (&element, sizeof(element));
		element.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&element) != NoError)
			continue;
		API_ElemType	tmpType = element.header.type;
		API_Guid		tmpGuid = element.header.guid;
		if (tmpType == API_SectElemID) {
			tmpType = element.sectElem.parentType;
			tmpGuid = element.sectElem.parentGuid;
		}

		if (tmpType == API_WallID || tmpType == API_ColumnID || tmpType == API_BeamID ||
			tmpType == API_WindowID || tmpType == API_DoorID || tmpType == API_SlabID ||
			tmpType == API_RoofID || tmpType == API_CurtainWallID || tmpType == API_ShellID ||
			tmpType == API_SkylightID || tmpType == API_MorphID)
			guid_ElementsToMerge.Push (tmpGuid);
	}

	if (!guid_ElementsToMerge.IsEmpty ()) {
		GSErrCode err = ACAPI_Element_Merge_Elements (guid_ElementsToMerge);
		if (err != NoError) {
			if (err == APIERR_NO3D)
				WriteReport_Alert ("Elements To Merge can be construction elements only.");
			else
				WriteReport_Alert ("ACAPI_Element_Merge_Elements failed: %d", err);
		}
	} else {
		WriteReport_Alert ("There are no selected construction elements!\n");
	}

	return;
}		/* Do_Merge_Elements */


void Do_Merge_GetMergedElements (void)
{
	API_ElemType	type;
	API_Guid		guid_Element;

	if (!ClickAnElem ("Click on an element to get the merged elements!", API_ZombieElemID, nullptr, &type, &guid_Element)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	API_Element	tmpElement;
	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	type = tmpElement.header.type;
	guid_Element = tmpElement.header.guid;
	if (type == API_SectElemID) {
		type = tmpElement.sectElem.parentType;
		guid_Element = tmpElement.sectElem.parentGuid;
	}

	if (type != API_WallID && type != API_ColumnID && type != API_BeamID &&
		type != API_WindowID && type != API_DoorID && type != API_SlabID &&
		type != API_RoofID && type != API_CurtainWallID && type != API_ShellID &&
		type != API_SkylightID && type != API_MorphID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	GS::Array<API_Guid>	guid_MergedElements;
	ACAPI_Element_Merge_GetMergedElements (guid_Element, &guid_MergedElements);
	for (UIndex i = 0; i < guid_MergedElements.GetSize (); i++) {
		char	str[256];
		sprintf (str, "  Element: %s  is merged with Element: %s",
					(const char *) APIGuid2GSGuid (guid_Element).ToUniString ().ToCStr (),
					(const char *) APIGuid2GSGuid (guid_MergedElements [i]).ToUniString ().ToCStr ());
		WriteReport (str);
	}

	return;
}		/* Do_Merge_GetMergedElements */


void Do_Merge_Remove (void)
{
	API_ElemType	typeID1;
	API_Guid		guid_Element1;

	if (!ClickAnElem ("Click on a construction element to remove the connection!", API_ZombieElemID, nullptr, &typeID1, &guid_Element1)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	API_Element	tmpElement;
	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element1;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID1 = tmpElement.header.type;
	guid_Element1 = tmpElement.header.guid;
	if (typeID1 == API_SectElemID) {
		typeID1 = tmpElement.sectElem.parentType;
		guid_Element1 = tmpElement.sectElem.parentGuid;
	}

	if (typeID1 != API_WallID && typeID1 != API_ColumnID && typeID1 != API_BeamID &&
		typeID1 != API_WindowID && typeID1 != API_DoorID && typeID1 != API_SlabID &&
		typeID1 != API_RoofID && typeID1 != API_CurtainWallID && typeID1 != API_ShellID &&
		typeID1 != API_SkylightID && typeID1 != API_MorphID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	API_ElemType	typeID2;
	API_Guid		guid_Element2;
	if (!ClickAnElem ("Click on a second construction element to remove the connection with the first!", API_ZombieElemID, nullptr, &typeID2, &guid_Element2)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	BNZeroMemory (&tmpElement, sizeof(tmpElement));
	tmpElement.header.guid = guid_Element2;
	if (ACAPI_Element_Get (&tmpElement) != NoError) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	typeID2 = tmpElement.header.type;
	guid_Element2 = tmpElement.header.guid;
	if (typeID2 == API_SectElemID) {
		typeID2 = tmpElement.sectElem.parentType;
		guid_Element2 = tmpElement.sectElem.parentGuid;
	}

	if (typeID2 != API_WallID && typeID2 != API_ColumnID && typeID2 != API_BeamID &&
		typeID2 != API_WindowID && typeID2 != API_DoorID && typeID2 != API_SlabID &&
		typeID2 != API_RoofID && typeID2 != API_CurtainWallID && typeID2 != API_ShellID &&
		typeID2 != API_SkylightID && typeID2 != API_MorphID) {
		WriteReport_Alert ("No construction element was clicked!\n");
		return;
	}
	GSErrCode err = ACAPI_Element_Merge_Remove (guid_Element1, guid_Element2);
	if (err != NoError) {
		if (err == APIERR_NO3D)
			WriteReport_Alert ("Only construction elements have merge connection!");
		else
			WriteReport_Alert ("ACAPI_Element_Merge_Remove failed: %d", err);
	}

	return;
}		/* Do_Merge_Remove */
