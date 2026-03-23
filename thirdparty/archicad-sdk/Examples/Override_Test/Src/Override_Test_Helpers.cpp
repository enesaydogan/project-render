// *****************************************************************************
// File:			Override_Test_Helper.cpp
// Description:		Override_Test add-on helper macros and functions
// Project:			APITools/Override_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

#include "Override_Test_Helpers.hpp"
#include "DG.h"
#include "StringConversion.hpp"
#include "Real.hpp"


API_Guid OverrideTestHelpers::RandomGuid () {
	GS::Guid guid;
	guid.Generate ();
	return GSGuid2APIGuid (guid);
}


void OverrideTestHelpers::DebugAssert (bool success, GS::UniString expression, const char* file, UInt32 line, const char* function)
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


void OverrideTestHelpers::DebugAssertNoError (GSErrCode error, GS::UniString expression, const char* file, UInt32 line, const char* function)
{
	if (error == NoError) {
		return;
	}

	expression += " returned with an error.";

#if defined (DEBUVERS)
	DBBreak (file, line, expression.ToCStr ().Get (), nullptr, function, nullptr);
#else
	UNUSED_PARAMETER (function);
	DGAlert (DG_ERROR, "Assertion",	expression,
			 "ErrorCode: " + GS::ValueToUniString (error) +
			 "\nAt: " + GS::UniString (file) + ":" + GS::ValueToUniString (line), "Ok");
#endif

	throw GS::Exception (expression);
}


bool operator==(const API_OverriddenFillType& a, const API_OverriddenFillType& b)
{
	return a.overrideCutFill == b.overrideCutFill &&
		   a.overrideCoverFill == b.overrideCoverFill &&
		   a.overrideDraftingFill == b.overrideDraftingFill;
}


bool operator==(const API_OverriddenPen& a, const API_OverriddenPen& b)
{
	return a.hasValue == b.hasValue && a.value == b.value;
}


bool operator==(const API_OverriddenAttribute& a, const API_OverriddenAttribute& b)
{
	return a.hasValue == b.hasValue && a.value == b.value;
}


bool IsNear (const API_RGBColor& a, const API_RGBColor& b)
{
	return 
		Geometry::IsNear (a.f_red, b.f_red) &&
		Geometry::IsNear (a.f_green, b.f_green) &&
		Geometry::IsNear (a.f_blue, b.f_blue);
}


bool operator==(const API_OverriddenPenOrRGB& a, const API_OverriddenPenOrRGB& b)
{
	if (a.hasValue != b.hasValue)
		return false;

	if (a.value.IsType<short> ()) {
		return b.value.IsType<short> () && a.value.Get<short> () == b.value.Get<short> ();
	} else {
		return b.value.IsType<API_RGBColor> () && IsNear (a.value.Get<API_RGBColor> (), b.value.Get<API_RGBColor> ());
	}
}


bool operator==(const API_OverriddenAttributeOrRGB& a, const API_OverriddenAttributeOrRGB& b)
{
	if (a.hasValue != b.hasValue)
		return false;

	if (a.value.IsType<API_AttributeIndex> ()) {
		return b.value.IsType<API_AttributeIndex> () && a.value.Get<API_AttributeIndex> () == b.value.Get<API_AttributeIndex> ();
	} else {
		return b.value.IsType<API_RGBColor> () && IsNear (a.value.Get<API_RGBColor> (), b.value.Get<API_RGBColor> ());
	}
}


bool operator==(const API_OverrideRuleStyle& a, const API_OverrideRuleStyle& b)
{
	return a.lineType == b.lineType && a.lineMarkerTextPen == b.lineMarkerTextPen &&
		   a.fillOverride == b.fillOverride && a.fillType == b.fillType &&
		   a.fillForegroundPenOverride == b.fillForegroundPenOverride &&
		   a.fillTypeForegroundPen == b.fillTypeForegroundPen &&
		   a.fillBackgroundPenOverride == b.fillBackgroundPenOverride &&
		   a.fillTypeBackgroundPen == b.fillTypeBackgroundPen &&
		   a.surfaceOverride == b.surfaceOverride &&
		   a.surfaceType == b.surfaceType &&
		   a.showSkinSeparators == b.showSkinSeparators &&
		   a.overridePenColorAndThickness == b.overridePenColorAndThickness;
}


bool operator==(const API_OverriddenSurfaceType& a, const API_OverriddenSurfaceType& b)
{
	return  a.overrideCutSurface == b.overrideCutSurface &&
			a.overrideUncutSurface == b.overrideUncutSurface;
}


static GS::UniString RemoveWhiteSpaces (const GS::UniString& str)
{
	GS::UniString result;
	for (USize i = 0; i < str.GetLength (); ++i) {
		if (!str[i].IsWhiteSpace ()) {
			result += str[i];
		}
	}
	return result;
}


bool operator==(const API_OverrideRule& a, const API_OverrideRule& b)
{
	return a.guid == b.guid &&
		   a.name == b.name &&
		   a.style == b.style &&
		   RemoveWhiteSpaces (a.criterionXML) == RemoveWhiteSpaces (b.criterionXML);
}


bool operator==(const API_OverrideCombination& a, const API_OverrideCombination& b)
{
	return a.name == b.name;
}