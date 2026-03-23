// *****************************************************************************
// Source code for the AddOnObject Manager Add-On
// *****************************************************************************

#define	_CUSTOMERDATA_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h

#include	"CustomerData.hpp"

#include	"StringConversion.hpp"
#include	"FixArray.hpp"


IAddOnData::IAddOnData () :
	modified (false),
	created (false),
	markedAsDeleted (false)
{
}


IAddOnData::~IAddOnData ()
{
}


CustomerData::CustomerData () :
	IAddOnData ()
{
}


CustomerData::~CustomerData ()
{

}


GSErrCode	CustomerData::Read (GS::IChannel& ic)
{
	name.Read (ic);
	city.Read (ic);
	country.Read (ic);
	return ic.GetInputStatus ();
}

GSErrCode	CustomerData::Write (GS::OChannel& oc) const
{
	name.Write (oc);
	city.Write (oc);
	country.Write (oc);
	return oc.GetOutputStatus ();
}

void	CustomerData::GenerateRandomContent (void)
{
	constexpr USize NUM_ELEMS = 10;
	static GS::FixArray<GS::UniString, NUM_ELEMS> firstNames = { "Albert", "Alex", "Alfred", "Amy", "Andrew", "Angelina", "Anne", "Anthony", "Arnold", "Arthur" };
	static GS::FixArray<GS::UniString, NUM_ELEMS> familyNames = { "Smith", "Johnson", "Williams", "Brown", "Jones", "Miller", "Davis", "Garcia", "Rodriguez", "Wilson" };
	static GS::FixArray<GS::UniString, NUM_ELEMS> cities = { "San Francisco", "Budapest", "Amsterdam", "Venezia", "Helsinki", "Limassol", "Tokyo", "Barcelona", "Abuja", "Auckland" };
	static GS::FixArray<GS::UniString, NUM_ELEMS> countries = { "United States", "Hungary", "Netherlands", "Italy", "Finland", "Cyprus", "Japan", "Spain", "Nigeria", "New Zealand" };
	static GS::UniString USPACE (" ");

	UInt32 firstNameIndex = rand () % NUM_ELEMS;
	UInt32 familyNameIndex = rand () % NUM_ELEMS;
	UInt32 locationIndex = rand () % NUM_ELEMS;
	name = firstNames[firstNameIndex] + USPACE + familyNames[familyNameIndex];
	city = cities[locationIndex];
	country = countries[locationIndex];
}


GS::UniString CustomerData::ToString () const
{
	return name + " | " + city + " (" + country + ")";
}


SettingData::SettingData () :
	IAddOnData (),
	value (-1)
{
}


SettingData::~SettingData ()
{
}


GSErrCode SettingData::Read (GS::IChannel& ic)
{
	name.Read (ic);
	ic.Read (value);
	return ic.GetInputStatus ();
}


GSErrCode SettingData::Write (GS::OChannel& oc) const
{
	name.Write (oc);
	oc.Write (value);
	return oc.GetOutputStatus ();
}


void SettingData::GenerateRandomContent (void)
{
	value = (rand () % 10) + 1;
}


GS::UniString SettingData::ToString () const
{
	return GS::ValueToUniString (value);
}
