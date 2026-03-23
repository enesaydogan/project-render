// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#ifndef	NAVIGATOR_UTILITY_HPP
#define	NAVIGATOR_UTILITY_HPP

#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "NavigatorCallbackInterface.hpp"


class NavigatorUtility {
public:
	static NavigatorCallbackInterface& getNavigatorCallbackInterface () {
		static NavigatorCallbackInterface navigatorCallbackInterface;
		return navigatorCallbackInterface;
	}

	static GSErrCode APINavigatorAddOnViewPointDataMergeHandler (const GS::Array<API_NavigatorAddOnViewPointData>&	sourceVPDataArray);
	static GSErrCode APINavigatorAddOnViewPointDataSaveOldFormatHandler (API_FTypeID /*planFileType*/,
		const GS::Array<API_NavigatorAddOnViewPointData>&		currentFormatVPDataArray,
		GS::Array<API_NavigatorAddOnViewPointData>&				oldFormatVPDataArray);
	static GSErrCode APINavigatorAddOnViewPointDataConvertNewFormatHandler (API_FTypeID /*planFileType*/,
		const GS::Array<API_NavigatorAddOnViewPointData>&	oldFormatVPDataArray,
		GS::Array<API_NavigatorAddOnViewPointData>&			currentFormatVPDataArray);

	static GSErrCode		RegisterNavigatorRootGroup (void);
	static GSErrCode		CreateNavigatorChildGroup (void);
	static GSErrCode		CloneProjectMapToViewMap (void);
	static void				OpenAPINavigator (void);

	static GSErrCode		GetFloorplanElements (const API_Guid& guid, GS::Array<API_Guid>& elems);
	static GSErrCode		CopyFloorplanElements (const API_Guid& guid);

	static void				ResolveNameConflict (GS::HashTable<GS::UniString, API_NavigatorAddOnViewPointData> itemNames, API_NavigatorAddOnViewPointData conflictingVpData);
	static bool				IsTeamworkProject (short* currentUserId = nullptr);
	static bool				HasDeleteModifyRight ();
};

#endif