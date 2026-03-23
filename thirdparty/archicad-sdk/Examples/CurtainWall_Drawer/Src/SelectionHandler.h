// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn SelectionHandler class header
//					for managing selections. Base for the drawer modifier classes
// *****************************************************************************

#ifndef _SELECTIONHANDLER_H_
#define _SELECTIONHANDLER_H_

#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"

class SelectionHandler
{
private:
	GSErrCode GetSelectedHeads (const GS::Array<API_Neig>& selNeigs);

protected:
	GSErrCode GetSelection ();

	virtual GSErrCode FilterTypes (const API_Elem_Head& elemhead, bool& elementFound) = 0;
	virtual GSResID GetSelectionErrorMessageResID () const = 0;
	virtual GSErrCode GetSelectedElements () = 0;

public:
	SelectionHandler ();
	virtual ~SelectionHandler ();

};

struct SelectionGeometry
{
	double	width,
			height,
			minX,
			minY,
			maxX,
			maxY;
};

struct SelectedElementHeads
{
	GS::Array<API_Elem_Head>	selectedLineHeads;
	API_Elem_Head				selectedHatchHead;
	API_Elem_Head				selectedCWHead;
};

struct Selection
{
	SelectionGeometry			geometry;
	SelectedElementHeads		elemHeads;
};

#endif

