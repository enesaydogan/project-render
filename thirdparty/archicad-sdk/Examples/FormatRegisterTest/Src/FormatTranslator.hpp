#ifndef	__FORMATTRANSLATOR_HPP__
#define	__FORMATTRANSLATOR_HPP__

#pragma once

#include "UniString.hpp"

class FormatTranslator {
private:
	GS::Guid	  guid;
	GS::UniString name;

public:
	FormatTranslator () = default;
	FormatTranslator (const GS::Guid& guid, const GS::UniString& name);

	const GS::Guid&			GetGuid () const;
	void					SetGuid (const GS::Guid& guid);

	const GS::UniString&	GetName () const;
	void					SetName (const GS::UniString& name);

	GSErrCode		Write (GS::OChannel& oc) const;
	GSErrCode		Read (GS::IChannel& ic);
};

#endif	// __FORMATTRANSLATOR_HPP__
