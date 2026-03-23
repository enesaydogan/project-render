// *********************************************************************************************************************
// Description:		Interface of the DumpUtils class
// *********************************************************************************************************************

#if !defined (DUMPUTILS_HPP)
#define DUMPUTILS_HPP

#pragma once

// --- Includes	----------------------------------------------------------------

#include	"APIEnvir.h"

#include	"ACAPinc.h"					// also includes APIdefs.h


// --- class DumpUtils --------------------------------------------------------

class DumpUtils {
public:
	static Int32	Element_To_LibPartIndex (const API_Element *elem);
	static void		GetAttrName	   (char* name, API_AttrTypeID typeID, API_AttributeIndex index, GS::UniString* uniName = nullptr);
	static void		Get_DoorVector (const API_DoorType*	door,
									double 				wradius,
									double 				dangle,
									double* 			loc_x,
									double* 			loc_y,
									double* 			angle);
	static void		GetWallData	   (const API_WallType* wall, double* angle, double* radius);
};

#endif
