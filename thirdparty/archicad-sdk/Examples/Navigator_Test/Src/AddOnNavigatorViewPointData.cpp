// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include "AddOnNavigatorViewPointData.hpp"
#include "MemoryOChannel32.hpp"
#include "MemoryIChannel32.hpp"
#include "ACAPinc.h"

AddOnNavigatorViewPointData::AddOnNavigatorViewPointData (const GS::UniString& id /*= GS::EmptyUniString*/, const GS::UniString& name /*= GS::EmptyUniString*/, const API_Guid& guid /*= APINULLGuid*/, API_ElemTypeID elemType /*= API_WallID*/, UInt32 elemNum /*= 5*/) :
	id (id),
	name (name),
	guid (guid),
	elemType (elemType),
	elemNum (elemNum)
{
}


AddOnNavigatorViewPointData::~AddOnNavigatorViewPointData ()
{
}


bool AddOnNavigatorViewPointData::operator==(const AddOnNavigatorViewPointData& rhs) const
{
	return guid == rhs.guid;
}


bool AddOnNavigatorViewPointData::operator!=(const AddOnNavigatorViewPointData& rhs) const
{
	return !(*this == rhs);
}


const API_ElemTypeID& AddOnNavigatorViewPointData::GetElemType (void) const
{
	return elemType;
}


const UInt32& AddOnNavigatorViewPointData::GetElemNum (void) const
{
	return elemNum;
}


const GS::UniString& AddOnNavigatorViewPointData::GetId (void) const
{
	return id;
}


const GS::UniString& AddOnNavigatorViewPointData::GetName (void) const
{
	return name;
}


const API_Guid& AddOnNavigatorViewPointData::GetGuid (void) const
{
	return guid;
}


void AddOnNavigatorViewPointData::SetElemType (const API_ElemTypeID& newElemType)
{
	elemType = newElemType;
}


void AddOnNavigatorViewPointData::SetElemNum (const UInt32& newElemNum)
{
	elemNum = newElemNum;
}


void AddOnNavigatorViewPointData::SetId (const GS::UniString& newId)
{
	id = newId;
}


void AddOnNavigatorViewPointData::SetName (const GS::UniString& newName)
{
	name = newName;
}


GSHandle	AddOnNavigatorViewPointData::GetHandle (void) const
{
	GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
	if (this->Write (memChannel) == NoError) {
		GSHandle content = nullptr;
		BMPtrToHandle (memChannel.GetDestination (), &content, memChannel.GetDataSize ());
		return content;
	}
	return nullptr;
}


void		AddOnNavigatorViewPointData::SetFromHandle (const GSHandle& handle)
{
	if (handle != nullptr) {
		GS::MemoryIChannel32 mic (*handle, BMhGetSize (handle));
		this->Read (mic);
	}
}


API_NavigatorAddOnViewPointData AddOnNavigatorViewPointData::ConvertToAPINavigatorAddOnViewPointData () const
{
	API_NavigatorAddOnViewPointData data;

	data.itemType = API_NavigatorAddOnViewPointNodeID;
	data.iconId = static_cast<Int32>(API_NavigatorAddOnViewPointNodeID);
	data.displayId = id;
	data.displayName = name;
	data.guid = guid;
	data.data = GetHandle ();
	data.viewSettingsFlags = API_NavigatorViewSettingsScaleAttributeID | API_NavigatorViewSettingsPenTableAttributeID | API_NavigatorViewSettingsLayerAttributeID;

	return data;
}


void AddOnNavigatorViewPointData::ConvertFromAPINavigatorAddOnViewPointData (const API_NavigatorAddOnViewPointData& data)
{
	id = data.displayId;
	name = data.displayName;
	guid = data.guid;
	SetFromHandle (data.data);
}


GSErrCode	AddOnNavigatorViewPointData::Read (GS::IChannel& ic)
{
	Int32 tmpElemType = 0;
	ic.Read (tmpElemType);
	elemType = static_cast<API_ElemTypeID>(tmpElemType);

	ic.Read (elemNum);

	return ic.GetInputStatus ();
}


GSErrCode	AddOnNavigatorViewPointData::Write (GS::OChannel& oc) const
{
	oc.Write (static_cast<Int32>(elemType));
	oc.Write (elemNum);

	return oc.GetOutputStatus ();
}
