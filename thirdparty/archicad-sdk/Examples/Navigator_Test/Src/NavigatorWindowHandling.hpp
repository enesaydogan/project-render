// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#ifndef	NAVIGATOR_WINDOW_HANDLING_HPP
#define	NAVIGATOR_WINDOW_HANDLING_HPP

#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"


namespace NavigatorWindowHandling {

	class APIWindowGuard
	{
	public:
		APIWindowGuard ();
		~APIWindowGuard ();
	};

	GSErrCode		OpenWindow (const API_Guid& guid, bool newWindow);
	GSErrCode		OpenTestDialog (API_Guid viewPointID);
	void			RegenerateContentIfAppropriate ();
};

#endif