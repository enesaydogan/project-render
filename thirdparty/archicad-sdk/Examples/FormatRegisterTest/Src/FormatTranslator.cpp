#include "FormatTranslator.hpp"

FormatTranslator::FormatTranslator (const GS::Guid& guid, const GS::UniString& name)
	: name (name)
	, guid (guid)
{
}


const GS::Guid& FormatTranslator::GetGuid () const 
{
	return guid;
}


void FormatTranslator::SetGuid (const GS::Guid& guid) 
{
	this->guid = guid;
}


const GS::UniString& FormatTranslator::GetName () const
{
	return name;
}


void FormatTranslator::SetName (const GS::UniString& name)
{
	this->name = name;
}


GSErrCode FormatTranslator::Write (GS::OChannel& oc) const
{
	return oc.Write (guid, name);
}


GSErrCode FormatTranslator::Read (GS::IChannel& ic)
{
	return ic.Read (guid, name);
}

