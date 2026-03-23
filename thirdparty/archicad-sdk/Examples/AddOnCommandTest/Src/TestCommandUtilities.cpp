#include	"TestCommandUtilities.hpp"
#include	"StringConversion.hpp"
#include	"MDIDs_APICD.h"


GS::String		TestCommandUtility::GetCommandNamespace ()
{
	return "AddOnCommandTest";
}


GS::UniString	TestCommandUtility::GetCreateColumnCommandParamsString ()
{
	return R"(
		"position": {
			  "x": 0.8 ,
			  "y": 2.7
		}
	)";
}


GS::UniString TestCommandUtility::GetRequestJSONString (const GS::UniString& commandName, const GS::UniString& parameters)
{
	return	"{ \"command\": \"API.ExecuteAddOnCommand\","									
			" \"parameters\" : { "														
			"  \"addOnCommandId\": {"														
			"   \"commandNamespace\": \"" + GetCommandNamespace () +"\","
			"   \"commandName\" : \"" + commandName + "\""
			"  },"																
			"  \"addOnCommandParameters\": {"	+ parameters +
			"  }"															
			" }"																
			"}";
}