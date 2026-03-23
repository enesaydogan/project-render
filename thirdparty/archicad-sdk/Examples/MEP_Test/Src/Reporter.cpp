#include "Reporter.hpp"

#include "ACAPinc.h"


Reporter::Reporter () :
	reportString (GS::EmptyUniString),
	tabCount (0)
{}


Reporter::Reporter (short tabCount) :
	reportString (GS::EmptyUniString),
	tabCount (tabCount)
{}


Reporter::~Reporter () = default;


GS::UniString Reporter::Indent (short tabCount)
{
	GS::UniString indentString = GS::EmptyUniString;

	for (short i = 0; i < tabCount; ++i)
		indentString += "\t";

	return indentString;
}


void Reporter::Add (const GS::UniString& format, double value, bool closeWithNewLine /*= true*/)
{
	reportString += GS::UniString::SPrintf (Indent (tabCount) + format + ": %.4f", value);
	if (closeWithNewLine) AddNewLine ();
}


void Reporter::Add (const GS::UniString& format, Int32 value, bool closeWithNewLine /*= true*/) 
{
	reportString += GS::UniString::SPrintf (Indent (tabCount) + format + ": %d", value);
	if (closeWithNewLine) AddNewLine ();
}


void Reporter::Add (const GS::UniString& text, bool closeWithNewLine /*= true*/)
{
	reportString += { Indent (tabCount) + text };
	if (closeWithNewLine) AddNewLine ();
}


void Reporter::Add (const API_Coord3D& coord, bool closeWithNewLine /*= true*/)
{
	reportString += GS::UniString::SPrintf ("{ %.4f, %.4f, %.4f }", coord.x, coord.y, coord.z);
	if (closeWithNewLine) AddNewLine ();
}


void Reporter::Add (const GS::UniString& format, const GS::UniString& subject, bool closeWithNewLine /*= true*/)
{
	reportString += GS::UniString::SPrintf (GS::UniString (Indent (tabCount) + format + ": %s"), subject.ToCStr ().Get ());
	if (closeWithNewLine) AddNewLine ();
}


void Reporter::AddNewLine ()
{
	reportString += "\n";
}


void Reporter::SetTabCount (short tabCount)
{
	this->tabCount = tabCount;
}


void Reporter::Write ()
{
	ACAPI_WriteReport (reportString, false);
	reportString = GS::EmptyUniString;
}


GS::UniString ToString (const ACAPI::UniqueID& id)
{
	return APIGuidToString (GSGuid2APIGuid (id.GetGuid ()));
}


GS::UniString ToString (ACAPI::MEP::ConnectorShape shape)
{
	switch (shape) {
		case ACAPI::MEP::ConnectorShape::Rectangular:
			return u"Rectangular"sv;
		case ACAPI::MEP::ConnectorShape::Circular:
			return u"Circular"sv;
		case ACAPI::MEP::ConnectorShape::UShape:
			return u"U Shape"sv;
	}

	return GS::EmptyUniString;
}


GS::UniString ToString (ACAPI::MEP::PreferredTransitionPlacement transitionPlacement)
{
	switch (transitionPlacement) {
		case ACAPI::MEP::PreferredTransitionPlacement::SmallToLarge:
			return u"Small To Large"sv;
		case ACAPI::MEP::PreferredTransitionPlacement::LargeToSmall:
			return u"Large To Small"sv;
	}

	return GS::EmptyUniString;
}


GS::UniString ToString (ACAPI::MEP::Domain domain)
{
	switch (domain) {
		case ACAPI::MEP::Domain::Ventilation:
			return u"Ventilation"sv;
		case ACAPI::MEP::Domain::Piping:
			return u"Piping"sv;
		case ACAPI::MEP::Domain::CableCarrier:
			return u"CableCarrier"sv;
	}

	return GS::EmptyUniString;
}


GS::UniString ToString (ACAPI::MEP::FlowDirection flowDirection)
{
	switch (flowDirection) {
		case ACAPI::MEP::FlowDirection::FlowNotDefined:
			return u"Flow Not Defined"sv;
		case ACAPI::MEP::FlowDirection::FlowIn:
			return u"Flow In"sv;
		case ACAPI::MEP::FlowDirection::FlowOut:
			return u"Flow Out"sv;
	}

	return GS::EmptyUniString;
}


GS::UniString ToString (ACAPI::MEP::ConnectionSex sex)
{
	switch (sex) {
		case ACAPI::MEP::ConnectionSex::Unisex:
			return u"Unisex"sv;
		case ACAPI::MEP::ConnectionSex::Male:
			return u"Male"sv;
		case ACAPI::MEP::ConnectionSex::Female:
			return u"Female"sv;
	}

	return GS::EmptyUniString;
}


GS::UniString ToString (const API_Coord3D& coord)
{
	return GS::UniString::SPrintf ("{ %.4f, %.4f, %.4f }", coord.x, coord.y, coord.z);
}


GS::UniString ToString (const API_AttributeIndex& attributeIndex)
{
	if (attributeIndex == APIInvalidAttributeIndex)
		return u"Undefined"sv;

	GS::UniString result;

	API_Attribute attribute;
	attribute.header.typeID = API_AttrTypeID::API_MEPSystemID;
	attribute.header.index = attributeIndex;
	attribute.header.uniStringNamePtr = &result;

	if (ACAPI_Attribute_Get (&attribute) != NoError) {
		ACAPI_WriteReport ("Failed to get MEPSystem attribute.", false);
		result = u"Undefined"sv;
	}

	return result;
}


GS::UniString ToString (uint32_t index)
{
	return GS::UniString::SPrintf ("%u", index);
}


GS::UniString ToString (ACAPI::MEP::ConnectionStyle connectionStyle)
{
	switch (connectionStyle) {
		case ACAPI::MEP::ConnectionStyle::SimpleBody:
			return u"SimpleBody"sv;
		case ACAPI::MEP::ConnectionStyle::Flanged:
			return u"Flanged"sv;
		case ACAPI::MEP::ConnectionStyle::Welded:
			return u"Welded"sv;
	}

	return GS::EmptyUniString;
}
