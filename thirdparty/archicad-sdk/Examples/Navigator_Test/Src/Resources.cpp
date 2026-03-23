// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include "Resources.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

GS::UniString GetLocStr (NavigatorAPILocalisedStringIds stringId)
{
	GS::UniString string = RSGetIndString (NAVIGATOR_TEST_RESID_LOCSTRINGS, stringId, ACAPI_GetOwnResModule ());
	return string;
}