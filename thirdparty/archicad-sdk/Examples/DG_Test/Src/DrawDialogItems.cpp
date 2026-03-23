// *****************************************************************************
// Source code for drawing dialog items in Building Material Dialog
// *****************************************************************************

#include	"DrawDialogItems.hpp"

#include	"FillEffects.hpp"

//-------------------------- API2AC_RGBColor helper function ----------------------

void	API2AC_RGBColor (Gfx::Color& rgb, const API_RGBColor& api_rgb)
{
	rgb.Set ((unsigned char) (api_rgb.f_red * 255.0),
			 (unsigned char) (api_rgb.f_green * 255.0),
			 (unsigned char) (api_rgb.f_blue * 255.0));
}


//---------------------------- FillRect helper function ---------------------------

void	FillRect (NewDisplay::NativeContext& drawContext, const DG::Rect& rect, const Gfx::Color& color)
{
	drawContext.FillRect (rect.GetLeft (), rect.GetTop (), rect.GetRight (), rect.GetBottom (),
						  color.GetRed (), color.GetGreen (), color.GetBlue ());
}


//--------------------------- Attribute_Get helper function -----------------------

GSErrCode Attribute_Get (API_Attribute* attr, const API_AttrTypeID& typeID, const API_AttributeIndex index)
{
	GSErrCode err = NoError;
	*attr				= {};
	attr->header.typeID = typeID;
	attr->header.index = index;
	// search by type and index
	err = ACAPI_Attribute_Get (attr);
	if (err != NoError)
		ACAPI_WriteReport ("ACAPI_Attribute_Get failed", true);
	return err;
}


GSErrCode Pen_Get (API_Pen& pen, short index)
{
	GSErrCode err = NoError;
	pen = {};
	pen.index = index;
	err = ACAPI_Attribute_GetPen (pen);
	if (err != NoError)
		ACAPI_WriteReport ("ACAPI_Environment_GetPen failed", true);
	return err;
}


// ---------------------------------------------------------------------------------------------------------------------
// Function:	DrawFillPattern
//
// Description:	Draw a normal fill pattern for building material draw
// ---------------------------------------------------------------------------------------------------------------------

bool	DrawFillPattern (NewDisplay::NativeContext& drawContext, const DG::Rect& rect, API_AttributeIndex fillIndex,
						 const Gfx::Color& fgColor, const Gfx::Color& bkColor, const Gfx::Color& frColor)
{
	if (fillIndex == APIInvalidAttributeIndex || fillIndex.IsNegative ())
		return false;

	// Get normal fill data
	API_Attribute attrFill;
	Attribute_Get (&attrFill, API_FilltypeID, fillIndex);

	// Draw normal fill
	short left		= rect.GetLeft ();
	short right		= rect.GetRight ();
	short top		= rect.GetTop ();
	short bottom	= rect.GetBottom ();

	switch (attrFill.filltype.subType) {
		case APIFill_LinearGradient:
			{
				NewDisplay::LinearGradientFillEffect linearGradientEffect (fgColor, bkColor, GS::Point<float> (left, top), GS::Point<float> (right, bottom));
				drawContext.BeginPolygon (&linearGradientEffect);
				break;
			}
		case APIFill_RadialGradient:
			{
				short height = rect.GetHeight ();
				GS::Point<float> centerPoint = GS::Point<float> ((float) (left + right) / 2, (float) (top + bottom) / 2);
				NewDisplay::RadialGradientFillEffect radialGradientEffect (fgColor, bkColor, centerPoint, height / 4, height / 2);
				drawContext.BeginPolygon (&radialGradientEffect);
				break;
			}
		default:
				drawContext.SetBackColor (bkColor.GetRed (), bkColor.GetGreen (), bkColor.GetBlue ());
				drawContext.SetForeColor (fgColor.GetRed (), fgColor.GetGreen (), fgColor.GetBlue ());
				drawContext.SetFillPattern (attrFill.filltype.bitPat);
				drawContext.BeginPolygon ();
				break;
	}

	drawContext.MoveTo (left, top);
	drawContext.LineTo (right, top);
	drawContext.LineTo (right, bottom);
	drawContext.LineTo (left, bottom);
	drawContext.LineTo (left, top);
	drawContext.EndPolygon ();

	drawContext.SetForeColor (frColor.GetRed (), frColor.GetGreen (), frColor.GetBlue ());
	drawContext.FrameRect (left, top, right, bottom);

	return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// Function:	DrawBuildingMaterial
//
// Description:	Draw a building material pattern
// ---------------------------------------------------------------------------------------------------------------------

bool	DrawBuildingMaterial (NewDisplay::NativeContext& drawContext, const DG::Rect& rect, API_AttributeIndex buildMatIndex)
{
	if (buildMatIndex == APIInvalidAttributeIndex || buildMatIndex.IsNegative ())
		return false;

	API_Attribute attrBuildMat;
	Attribute_Get (&attrBuildMat, API_BuildingMaterialID, buildMatIndex);

	API_Pen cutFillPen = {};
	Pen_Get (cutFillPen, attrBuildMat.buildingMaterial.cutFillPen);
	Gfx::Color penColor;
	API2AC_RGBColor (penColor, cutFillPen.rgb);

	Gfx::Color bkPenColor;
	if (attrBuildMat.buildingMaterial.cutFillBackgroundPen > 0) {
		API_Pen cutFillBkPen;
		Pen_Get (cutFillBkPen, attrBuildMat.buildingMaterial.cutFillBackgroundPen);
		API2AC_RGBColor (bkPenColor, cutFillBkPen.rgb);
	} else
		bkPenColor.Set (255, 255, 255, 1);

	API_Attribute attrCutMaterial;
	Attribute_Get (&attrCutMaterial, API_MaterialID, attrBuildMat.buildingMaterial.cutMaterial);
	Gfx::Color surfaceColor;
	API2AC_RGBColor (surfaceColor, attrCutMaterial.material.surfaceRGB);

	short left		= rect.GetLeft ();
	short top		= rect.GetTop ();
	short right		= rect.GetRight ();
	short bottom	= rect.GetBottom ();

	right = rect.GetLeft () + (short) ((rect.GetRight () - rect.GetLeft ()) * 0.48);

	DrawFillPattern (drawContext, DG::Rect (left, top, right, bottom), attrBuildMat.buildingMaterial.cutFill,
					 penColor, bkPenColor, Gfx::Color (0, 0, 0));

	left = rect.GetLeft () + (short) ((rect.GetRight () - rect.GetLeft ()) * 0.52);
	right = rect.GetRight ();

	const Gfx::Color frColor = Gfx::Color (0, 0, 0);

	drawContext.SetBackColor (bkPenColor.GetRed (), bkPenColor.GetGreen (), bkPenColor.GetBlue ());
	FillRect (drawContext, DG::Rect (left, top, right, bottom), surfaceColor);

	if (attrBuildMat.buildingMaterial.showUncutLines) {
		drawContext.SetForeColor (frColor.GetRed (), frColor.GetGreen (), frColor.GetBlue ());
		drawContext.FrameRect (left, top, right, bottom);
	}

	delete attrCutMaterial.material.texture.fileLoc;
	attrCutMaterial.material.texture.fileLoc = nullptr;

	return true;
}


// ---------------------------------------------------------------------------------------------------------------------
// Function:	DrawPen
//
// Description:	Draw a pen pattern
// ---------------------------------------------------------------------------------------------------------------------

bool	DrawPen (NewDisplay::NativeContext& drawContext, const DG::Rect& rect, short penIndex)
{
	if (penIndex <= 0)
		return false;

	FillRect (drawContext, rect, Gfx::Color (0, 0, 0));

	DG::Rect rectThick, rectCol;
	rectThick = rect;
	rectThick.Inflate (-1, -1);
	FillRect (drawContext, rectThick, Gfx::Color (255, 255, 255));

	rectThick = rect;
	rectThick.Inflate (-3, -3);
	if (rectThick.GetWidth () > 10)
		rectThick.SetWidth (10);

	rectCol = rect;
	rectCol.Inflate (-3, -3);
	rectCol.SetLeft (rectThick.GetRight () + 2);

	API_Pen pen = {};
	Pen_Get (pen, penIndex);
	Gfx::Color penColor;
	API2AC_RGBColor (penColor, pen.rgb);

	if (rectThick.GetWidth () > 0) {
		short thick = static_cast<short> (pen.width * 10.0 + 0.5);

		if (thick < 1)
			thick = 1;
		if (thick > rectThick.GetWidth ())
			thick = rectThick.GetWidth ();

		rectThick.Move ((rectThick.GetWidth () - thick) / 2, 0);
		rectThick.SetWidth (thick);

		FillRect (drawContext, rectThick, penColor);
		FillRect (drawContext, rectCol, penColor);
	}

	return true;
}