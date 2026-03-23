#if !defined (TESTCOMMANDUTILITIES_HPP)
#define TESTCOMMANDUTILITIES_HPP

#pragma once

#include	"UniString.hpp"

namespace TestCommandUtility {

	GS::String					GetCommandNamespace ();

	GS::UniString				GetRequestJSONString (const GS::UniString& commandName,
													  const GS::UniString& parameters);

	GS::UniString				GetCreateColumnCommandParamsString ();
}


#endif