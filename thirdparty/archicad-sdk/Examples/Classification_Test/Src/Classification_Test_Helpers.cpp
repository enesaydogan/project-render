// *****************************************************************************
// File:			Classification_Test_Helper.cpp
// Description:		Classification_Test add-on helper macros and functions
// Project:			APITools/Classification_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

#include "Classification_Test_Helpers.hpp"
#include "APICommon.h"

#include "StringConversion.hpp"


API_Guid ClassificationTestHelpers::RandomGuid () {
	GS::Guid guid;
	guid.Generate ();
	return GSGuid2APIGuid (guid);
}


GS::UniString ClassificationTestHelpers::GenearteUniqueName ()
{
	GS::Guid guid;
	guid.Generate ();
	GS::UniString guidString = guid.ToUniString ();
	guidString.Truncate (8);
	return guidString;
}

static GS::Array<API_Guid>	GetSelectedElements (bool assertIfNoSel /* = true*/, bool onlyEditable /*= true*/)
{
	GSErrCode            err;
	API_SelectionInfo    selectionInfo;
	GS::Array<API_Neig>  selNeigs;

	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, onlyEditable);
	BMKillHandle ((GSHandle *)&selectionInfo.marquee.coords);
	if (err == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
		if (assertIfNoSel) {
			DGAlert (DG_ERROR, "Error", "Please select an element!", "", "Ok");
		}
	}

	if (err != NoError) {
		return GS::Array<API_Guid>();
	}

	GS::Array<API_Guid> guidArray;
	for (const API_Neig& neig : selNeigs) {
		guidArray.Push (neig.guid);
	}

	return guidArray;
}


void ClassificationTestHelpers::CallOnSelectedElem (void (*function)(const API_Guid&), bool assertIfNoSel /* = true*/, bool onlyEditable /* = true*/)
{
	GS::Array<API_Guid> guidArray = GetSelectedElements (assertIfNoSel, onlyEditable);
	if (!guidArray.IsEmpty ()) {
		function (guidArray[0]);
	} else if (!assertIfNoSel) {
		function (APINULLGuid);
	}
}

void ClassificationTestHelpers::DebugAssert (bool success, GS::UniString expression, const char* file, UInt32 line, const char* function)
{
	if (success) {
		return;
	}

	expression += " is false.";

#if defined (DEBUVERS)
	DBBreak (file, line, expression.ToCStr ().Get (), nullptr, function, nullptr);
#else
	UNUSED_PARAMETER (function);
	DGAlert (DG_ERROR, "Assertion",	expression,
			 "At: " + GS::UniString (file) + ":" + GS::ValueToUniString (line), "Ok");
#endif

	throw GS::Exception (expression);
}


void ClassificationTestHelpers::DebugAssertNoError (GSErrCode error, GS::UniString expression, const char* file, UInt32 line, const char* function)
{
	if (error == NoError) {
		return;
	}

	expression += GS::UniString (" returned with ") + ErrID_To_Name (error) + ".";

#if defined (DEBUVERS)
	DBBreak (file, line, expression.ToCStr ().Get (), nullptr, function, nullptr);
#else
	UNUSED_PARAMETER (function);
	DGAlert (DG_ERROR, "Assertion",	expression,
			 "At: " + GS::UniString (file) + ":" + GS::ValueToUniString (line), "Ok");
#endif

	throw GS::Exception (expression);
}

GSDateRecord ClassificationTestHelpers::GetCurrentDate ()
{
	GSTimeRecord gsTimeRecord;
	TIGetTimeRecord (GSTime (), &gsTimeRecord, TI_CURRENT_TIME);
	return GSDateRecord (gsTimeRecord.year, gsTimeRecord.month, gsTimeRecord.day);
}


bool operator== (const API_ClassificationItem& lhs, const API_ClassificationItem& rhs)
{
	return lhs.guid == rhs.guid && lhs.name == rhs.name && lhs.description == rhs.description;
}


bool operator== (const API_ClassificationSystem& lhs, const API_ClassificationSystem& rhs)
{
	return lhs.guid == rhs.guid && lhs.name == rhs.name && lhs.editionVersion == rhs.editionVersion && lhs.editionDate == rhs.editionDate;
}
