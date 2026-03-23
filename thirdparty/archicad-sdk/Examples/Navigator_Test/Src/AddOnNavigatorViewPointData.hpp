// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#if !defined (ADDONNAVIGATORVIEWPOINTDATA_HPP)
#define ADDONNAVIGATORVIEWPOINTDATA_HPP

#include "UniString.hpp"
#include "APIdefs_Elements.h"
#include "APIdefs_Navigator.h"
#include "Channel.hpp"

class AddOnNavigatorViewPointData {
private:
	API_ElemTypeID		elemType;
	UInt32				elemNum;

	GS::UniString		id;
	GS::UniString		name;
	API_Guid			guid;

public:
	AddOnNavigatorViewPointData (const GS::UniString& id = GS::EmptyUniString, const GS::UniString& name = GS::EmptyUniString, const API_Guid& guid = APINULLGuid, API_ElemTypeID elemType = API_LineID, UInt32 elemNum = 5);
	virtual ~AddOnNavigatorViewPointData ();

	bool operator==(const AddOnNavigatorViewPointData& rhs) const;
	bool operator!=(const AddOnNavigatorViewPointData& rhs) const;

	const API_ElemTypeID& GetElemType (void) const;
	const UInt32& GetElemNum (void) const;
	const GS::UniString& GetId (void) const;
	const GS::UniString& GetName (void) const;
	const API_Guid& GetGuid (void) const;

	void SetElemType (const API_ElemTypeID& newElemType);
	void SetElemNum (const UInt32& newElemNum);
	void SetId (const GS::UniString& newId);
	void SetName (const GS::UniString& newName);

	GSHandle	GetHandle (void) const;
	void		SetFromHandle (const GSHandle& handle);

	API_NavigatorAddOnViewPointData		ConvertToAPINavigatorAddOnViewPointData (void) const;
	void ConvertFromAPINavigatorAddOnViewPointData (const API_NavigatorAddOnViewPointData& data);

	GSErrCode	Read (GS::IChannel& ic);
	GSErrCode	Write (GS::OChannel& oc) const;
};

#endif