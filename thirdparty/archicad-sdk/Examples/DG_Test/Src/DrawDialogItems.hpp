// *****************************************************************************
// Header file for drawing dialog items in Building Material Dialog
// *****************************************************************************

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h

#include	"DGModule.hpp"
#include	"DGNativeContexts.hpp"


bool	DrawPen					(NewDisplay::NativeContext& drawContext, const DG::Rect& rect, short penIndex);
bool	DrawBuildingMaterial	(NewDisplay::NativeContext& drawContext, const DG::Rect& rect, API_AttributeIndex buildMatIndex);

GSErrCode Attribute_Get (API_Attribute* attr, const API_AttrTypeID& typeID, const API_AttributeIndex index);
GSErrCode Pen_Get (API_Pen& pen, short index);