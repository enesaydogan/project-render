#include "Utility.hpp"

// API
#include "ACAPinc.h"


namespace MEPExample {


GSErrCode CreateElementSetFromSelectedElements ()
{
	GS::Array<API_Neig>	selNeigs;
	API_SelectionInfo 	selectionInfo = {};

	ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);

	if (selNeigs.IsEmpty ())
		return NoError;

	GS::Array<API_Guid> guids;
	API_Guid setGuid = GenerateAPIGuid ();

	for (const API_Neig& neig : selNeigs)
		guids.PushNew (neig.guid);

	ACAPI_CallUndoableCommand ("Create Element Set", [&]() -> GSErrCode {
		// write a note into the user data
		API_UserData userData = {};
		userData.dataVersion = 1;
		userData.platformSign = GS::Act_Platform_Sign;
		userData.dataHdl = BMAllocateHandle (1024, ALLOCATE_CLEAR, 0);

		return ACAPI_ElementSet_Create (&guids, &userData, &setGuid);
	});

	ACAPI_WriteReport ("Successfully created the Element Set from the selected elements", false);

	GS::Array<API_Guid> outGuids;
	API_UserData userData = {};

	ACAPI_ElementSet_GetData (setGuid, &outGuids, &userData);

	ACAPI_WriteReport ("ElementIds in created set: ", false);

	GSErrCode err = NoError;
	for (UInt32 i = 0; i < guids.GetSize (); ++i) {
		ACAPI_WriteReport (GS::UniString::Printf ("%T\n", APIGuidToString (outGuids[i]).ToPrintf ()), false);

		if (guids[i] != outGuids[i]) {
			err = Error;
			break;
		}
	}

	if (err == NoError)
		ACAPI_WriteReport ("Requested Element Set matches the originally selected elements", false);
	else
		ACAPI_WriteReport ("Requested Element Set does not match the originally selected elements", false);

	return NoError;
}


GSErrCode CreateElementLinksBetweenSelectedElements ()
{
	GS::Array<API_Neig>	selNeigs;
	API_SelectionInfo 	selectionInfo = {};

	ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);

	if (selNeigs.IsEmpty ())
		return NoError;

	GS::Array<API_Guid> guids;

	for (const API_Neig& neig : selNeigs)
		guids.PushNew (neig.guid);

	for (UInt32 i = 1; i < guids.GetSize (); ++i) {
		GSErrCode err = ACAPI_CallUndoableCommand ("Create Element Link", [&]() -> GSErrCode {
			return ACAPI_ElementLink_Link (guids[0], guids[i], 0);
		});

		if (err != NoError) {
			ACAPI_WriteReport ("Failed to Link Element, could be already Linked", false);
			return err;
		}
	}

	GS::Array<API_Guid> guids_linkTo;

	ACAPI_CallUndoableCommand ("Get Element Links", [&]() -> GSErrCode {
		return ACAPI_ElementLink_GetLinks (guids[0], &guids_linkTo);
	});

	if (guids.GetSize () - 1 != guids_linkTo.GetSize ()) {
		ACAPI_WriteReport ("Not all Elements got linked", false);
		return Error;
	}

	ACAPI_WriteReport (GS::UniString::Printf ("Elements linked from %T: \n", APIGuidToString (guids[0]).ToPrintf ()), false);

	for (API_Guid guid_linkTo : guids_linkTo)
		ACAPI_WriteReport (GS::UniString::Printf ("%T\n", APIGuidToString (guid_linkTo).ToPrintf ()), false);

	return NoError;
}


GSErrCode Test () 
{
	GS::Array<API_Neig>	selNeigs;
	API_SelectionInfo 	selectionInfo = {};

	ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);

	if (selNeigs.IsEmpty ())
		return NoError;

	GS::Array<API_Guid> guids;

	for (const API_Neig& neig : selNeigs)
		guids.PushNew (neig.guid);

	GSErrCode err = ACAPI_Element_Filter (guids[0], APIFilt_OnVisLayer);
	if (err != NoError)
		return err;

	return NoError;
}

} // namespace MEPExample