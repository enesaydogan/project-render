// *****************************************************************************
// Source code for the AddOnObject Manager Add-On
// *****************************************************************************

#if !defined (CUSTOMERDATA_HPP)
#define CUSTOMERDATA_HPP

#include "UniString.hpp"

// ---------------------------------- Includes ---------------------------------
enum CloseReason {
	CloseReason_Nothing,
	CloseReason_Reserve,
	CloseReason_Release,
	CloseReason_Grant,
	CloseReason_Request,
	CloseReason_CreateUnique
};

class IAddOnData {
public:
	IAddOnData ();
	virtual ~IAddOnData ();

	GS::Guid			guid;
	bool				modified;
	bool				created;
	bool				markedAsDeleted;

	virtual GSErrCode		Read (GS::IChannel& ic) = 0;
	virtual GSErrCode		Write (GS::OChannel& oc) const = 0;
	virtual void			GenerateRandomContent (void) = 0;
	virtual GS::UniString	ToString () const = 0;
};

class CustomerData : public IAddOnData {
public:
	GS::UniString		name;
	GS::UniString		city;
	GS::UniString		country;

	CustomerData ();
	virtual ~CustomerData ();

	virtual GSErrCode		Read (GS::IChannel& ic) override;
	virtual GSErrCode		Write (GS::OChannel& oc) const override;
	virtual void			GenerateRandomContent (void) override;
	virtual GS::UniString	ToString () const override;
};

class SettingData : public IAddOnData {
public:
	GS::UniString		name;
	Int32				value;

	SettingData ();
	virtual ~SettingData ();

	virtual GSErrCode		Read (GS::IChannel& ic) override;
	virtual GSErrCode		Write (GS::OChannel& oc) const override;
	virtual void			GenerateRandomContent (void) override;
	virtual GS::UniString	ToString () const override;
};
#endif