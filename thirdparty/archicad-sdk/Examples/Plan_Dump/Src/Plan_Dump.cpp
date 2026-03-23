// *****************************************************************************
// Description:		Source code for the Plan Dump Add-On
// *****************************************************************************

#define	_PLAN_DUMP_TRANSL_
#define	_Geometry_Test_TRANSL_

// --- Includes ----------------------------------------------------------------

#include	"Resource.h"
#include	"DumpWriter.hpp"
#include	"DumpUtils.hpp"
#include	"APICommon.h"

#include	<time.h>

#include	"GSRoot.hpp"
#include	"BM.hpp"

#include	"DG.h"

#include	"GenArc2DData.h"

// --- Variable declarations ---------------------------------------------------
static DumpWriter*		writer;

static double			dScale;


// =============================================================================
//
// Helper functions
//
// =============================================================================



// -----------------------------------------------------------------------------
//	Return the length unit as text
// -----------------------------------------------------------------------------

static void		SetLengthUnit (API_LengthTypeID lengthUnit, char *s)
{
	switch (lengthUnit) {
		case API_LengthTypeID::Meter:			strcpy (s, "M");	break;
		case API_LengthTypeID::Decimeter:		strcpy (s, "DM");	break;
		case API_LengthTypeID::Centimeter:		strcpy (s, "CM");	break;
		case API_LengthTypeID::Millimeter:		strcpy (s, "MM");	break;
		case API_LengthTypeID::FootFracInch:	strcpy (s, "FFI");	break;
		case API_LengthTypeID::FootDecInch:		strcpy (s, "FDI");	break;
		case API_LengthTypeID::DecFoot:			strcpy (s, "DF");	break;
		case API_LengthTypeID::FracInch:		strcpy (s, "FI");	break;
		case API_LengthTypeID::DecInch:			strcpy (s, "DI");	break;
		default:								strcpy (s, "XXX");	break;
	}
}


// -----------------------------------------------------------------------------
//	Return the angle unit as text
// -----------------------------------------------------------------------------

static	void	SetAngleUnit (API_AngleTypeID angleUnit, char *s)
{
	switch (angleUnit) {
		case API_AngleTypeID::DecimalDegree:	strcpy (s, "DD");	break;
		case API_AngleTypeID::DegreeMinSec:		strcpy (s, "DMS");	break;
		case API_AngleTypeID::Grad:				strcpy (s, "GR");	break;
		case API_AngleTypeID::Radian:			strcpy (s, "RAD");	break;
		case API_AngleTypeID::Surveyors:		strcpy (s, "SURV");	break;
		default:								strcpy (s, "XXX");	break;
	}
}


// =============================================================================
//
// 								I/O functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
//	Save all pens into a file
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Pen (void)
{
	UInt32				nPen;
	GSErrCode			err;

	err = ACAPI_Attribute_GetPenNum (nPen);
	for (UInt32 i = 1; i <= nPen && err == NoError; i++) {
		API_Pen pen = {};
		pen.index = (short) i;
		err = ACAPI_Attribute_GetPen (pen);
		if (err == NoError) {
			writer->WriteStr ("PEN");
			writer->WriteInt (i);
			writer->WriteRGB (&pen.rgb);
			err = writer->WrEndblk ();
		} else {
			if (err == APIERR_BADINDEX)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Pen


// -----------------------------------------------------------------------------
//	Save all linetypes into a file
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Linetype (void)
{
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_LinetypeID, [&] (API_Attribute& attrib) {
		API_AttributeDef defs;
		err = ACAPI_Attribute_GetDef (API_LinetypeID, attrib.header.index, &defs);
		if (err == NoError) {
			writer->WriteBlock ("LINE_TYPE", attrib.header.index);
			writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);
			switch (attrib.linetype.type) {
				case APILine_SolidLine:
					writer->WriteStr ("SOLID_LINE");
					break;
				case APILine_DashedLine:
				{
					writer->WriteStr ("DASHED_LINE", DumpWriter::WithNewLine);
					writer->WriteInt (2 * attrib.linetype.nItems, DumpWriter::WithNewLine);
					for (short j = 0; j < attrib.linetype.nItems; j++) {
						writer->WriteFloat ((*defs.ltype_dashItems)[j].dash / 1000.0);
						writer->WrNewLine ();
						writer->WriteFloat ((*defs.ltype_dashItems)[j].gap / 1000.0);
						if (j != attrib.linetype.nItems)
							writer->WrNewLine ();
					}
					break;
				}
				case APILine_SymbolLine:
					writer->WriteStr ("SYMBOL_LINE");
					break;
			}
			err = writer->WrEndblk ();
			ACAPI_DisposeAttrDefsHdls (&defs);
		}
	});

	return err;
}		// Do_SaveAs_Linetype


// -----------------------------------------------------------------------------
//	Save all fill types
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Filltype (void)
{
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_FilltypeID, [&] (API_Attribute& attrib) {
		API_AttributeDef defs, defs2;
		err = ACAPI_Attribute_GetDef (API_FilltypeID, attrib.header.index, &defs);
		if (err == NoError) {
			writer->WriteBlock ("FILL", attrib.header.index);
			writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);
			switch (attrib.filltype.subType) {
				case APIFill_Vector:
					writer->WriteStr ("VECTOR_FILL", DumpWriter::WithNewLine);

					for (Int32 j = 0; j <= 7; j++)
						writer->WriteInt (attrib.filltype.bitPat[j]);

					writer->WrNewLine ();
					if (attrib.header.flags & APIFill_ScaleWithPlan) {
						attrib.filltype.hXSpac /= (Int32) dScale;
						attrib.filltype.hYSpac /= (Int32) dScale;
					}

					writer->WriteFloat (attrib.filltype.hXSpac);
					writer->WriteFloat (attrib.filltype.hYSpac);
					writer->WriteAngle (attrib.filltype.hAngle);
					writer->WriteInt (attrib.filltype.linNumb);

					err = ACAPI_Attribute_GetDef (API_FilltypeID, attrib.header.index, &defs2);

					if (err == NoError) {

						for (Int32 j = 0; j < attrib.filltype.linNumb; j++) {
							writer->WrNewLine ();
							writer->WriteFloat ((*(defs2.fill_lineItems))[j].lFreq);
							writer->WriteFloat ((*(defs2.fill_lineItems))[j].lDir);
							writer->WriteFloat ((*(defs2.fill_lineItems))[j].lOffsetLine);
							writer->WriteFloat ((*(defs2.fill_lineItems))[j].lOffset.x);
							writer->WriteFloat ((*(defs2.fill_lineItems))[j].lOffset.y);
							writer->WriteInt ((*(defs2.fill_lineItems))[j].lPartNumb);

							if ((*(defs2.fill_lineItems))[j].lPartNumb > 0) {

								writer->WrNewLine ();
								for (short k = 0; k < (*(defs2.fill_lineItems))[j].lPartNumb; k++) {
									writer->WriteFloat ((*(defs2.fill_lineLength))[k +
														(*(defs2.fill_lineItems))[j].lPartOffs]);
								}

							}
						}
					}
					ACAPI_DisposeAttrDefsHdls (&defs2);
					break;

				case APIFill_Symbol:
					writer->WriteStr ("SYMBOL_FILL");
					break;

				case APIFill_Solid:
					writer->WriteStr ("SOLID_FILL");
					writer->WriteFloat (attrib.filltype.percent);
					break;

				case APIFill_Empty:
					writer->WriteStr ("EMPTY_FILL");
					break;

				case APIFill_LinearGradient:
					writer->WriteStr ("LINEAR_GRADIENT");
					break;

				case APIFill_RadialGradient:
					writer->WriteStr ("RADIAL_GRADIENT");
					break;

				case APIFill_Image:
				{
					writer->WriteStr ("IMAGE_FILL");

					char				buffer[512];
					sprintf (buffer, "%s", (const char*) GS::UniString (attrib.filltype.textureName).ToCStr ());
					writer->WriteStr (buffer);
					writer->WriteInt (attrib.filltype.textureStatus);
					writer->WriteFloat (attrib.filltype.textureXSize);
					writer->WriteFloat (attrib.filltype.textureYSize);
					writer->WriteFloat (attrib.filltype.textureRotAng);
				}
				break;
			}
			err = writer->WrEndblk ();
			ACAPI_DisposeAttrDefsHdls (&defs);
		}
	});

	return err;
}		// Do_SaveAs_Filltype


// -----------------------------------------------------------------------------
//	Save all composite structures
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_CompStruct (void)
{
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_CompWallID, [&] (API_Attribute& attrib) {
		API_AttributeDef defs;
		err = ACAPI_Attribute_GetDef (API_CompWallID, attrib.header.index, &defs);
		if (err == NoError) {
			writer->WriteBlock ("COMPOSITE_STRUCTURE", attrib.header.index);
			writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);
			writer->WriteFloat (attrib.compWall.totalThick);
			writer->WrNewLine ();
			writer->WriteInt (attrib.compWall.nComps);
			for (short j = 0; j < attrib.compWall.nComps; j++) {
				writer->WrNewLine ();
				GS::UniString name;
				DumpUtils::GetAttrName (nullptr, API_BuildingMaterialID, (*defs.cwall_compItems)[j].buildingMaterial, &name);
				writer->WriteInt ((Int32) (*defs.cwall_compItems)[j].framePen);
				writer->WriteName (name);
				writer->WriteFloat ((*defs.cwall_compItems)[j].fillThick);
			}
			err = writer->WrEndblk ();
			ACAPI_DisposeAttrDefsHdls (&defs);
		}
	});

	return err;
}		// Do_SaveAs_CompStruct


// -----------------------------------------------------------------------------
//	Save all materials
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Material (void)
{
	char buffer[512];
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_MaterialID, [&] (API_Attribute& attrib) {
		writer->WriteBlock ("MATERIAL", attrib.header.index);
		writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);
		writer->WriteRGB (&attrib.material.surfaceRGB);
		writer->WrNewLine ();
		sprintf (buffer, "%.6lf %.6lf %.6lf %.6lf",
				 attrib.material.ambientPc / 100.0, attrib.material.diffusePc / 100.0,
				 attrib.material.specularPc / 100.0, attrib.material.transpPc / 100.0);
		writer->WriteStr (buffer, DumpWriter::WithNewLine);
		writer->WriteInt (attrib.material.shine / 100);
		writer->WriteInt (attrib.material.transpAtt / 100, DumpWriter::WithNewLine);

		writer->WriteRGB (&attrib.material.specularRGB);
		writer->WrNewLine ();
		writer->WriteRGB (&attrib.material.emissionRGB);
		writer->WrNewLine ();
		sprintf (buffer, "%.6lf", attrib.material.emissionAtt / 100.0);
		writer->WriteStr (buffer, DumpWriter::WithNewLine);

		GS::UniString name;
		DumpUtils::GetAttrName (nullptr, API_FilltypeID, attrib.material.ifill, &name);
		writer->WriteName (name, DumpWriter::WithNewLine);
		writer->WriteInt (attrib.material.fillCol, DumpWriter::WithNewLine);

		if (attrib.material.texture.status != 0 && attrib.material.texture.fileLoc != nullptr && attrib.material.texture.fileLoc->GetLocalLength () != 0) {
			IO::Name	name;
			err = attrib.material.texture.fileLoc->GetLastLocalName (&name);
			writer->WriteName ((const char*) name.ToString ().ToCStr ());
			writer->WriteFloat (attrib.material.texture.xSize);
			writer->WriteFloat (attrib.material.texture.ySize);
			writer->WriteAngle (attrib.material.texture.rotAng);
			writer->WrNewLine ();
		} else {
			writer->WriteName ("???");
		}
		err = writer->WrEndblk ();

		if (attrib.material.texture.fileLoc != nullptr) {
			delete attrib.material.texture.fileLoc;
			attrib.material.texture.fileLoc = nullptr;
		}
	});

	return err;
}		// Do_SaveAs_Material


// -----------------------------------------------------------------------------
//	Save all zone categories
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_ZoneCategories (void)
{
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_ZoneCatID, [&] (API_Attribute& attrib) {
		writer->WriteBlock ("ZONE_CATEGORY", attrib.header.index);
		writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);
		writer->WriteStr (GS::UniString (attrib.zoneCat.catCode).ToCStr ().Get (), DumpWriter::WithNewLine);
		writer->WriteStr (GS::UniString (attrib.zoneCat.stampName).ToCStr ().Get (), DumpWriter::WithNewLine);
		GS::UniString stampUnIDStr = GS::UniString::Printf ("{%T}-{%T}", APIGuidToString (attrib.zoneCat.stampMainGuid).ToPrintf (), APIGuidToString (attrib.zoneCat.stampRevGuid).ToPrintf ());
		writer->WriteStr (stampUnIDStr.ToCStr ().Get (), DumpWriter::WithNewLine);
		writer->WriteRGB (&attrib.zoneCat.rgb);
		writer->WrNewLine ();

		API_AttributeDefExt defs;
		err = ACAPI_Attribute_GetDefExt (API_ZoneCatID, attrib.header.index, &defs);
		if (err == NoError) {
			UInt32 addParNum = BMGetHandleSize (reinterpret_cast<GSHandle> (defs.zone_addParItems)) / sizeof (API_AddParType);
			writer->WriteStr ("ZC_PARAMETERS");
			writer->WriteInt (addParNum);
			writer->WriteParams (defs.zone_addParItems);
			err = writer->WrEndblk ();
			ACAPI_DisposeAttrDefsHdlsExt (&defs);
		}

		err = writer->WrEndblk ();
	});

	return err;
}		// Do_SaveAs_ZoneCategories


#ifndef NO_MEP_FEATURES
// -----------------------------------------------------------------------------
//	Save all MEP Systems
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_MEPSystems (void)
{
	//MEP systems don't have extended data.
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_MEPSystemID, [&] (API_Attribute& attrib) {
		writer->WriteBlock ("MEP_SYSTEM", attrib.header.index);
		writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);

		writer->WriteStr (attrib.mepSystem.isForDuctwork ? "true" : "false");
		writer->WriteStr (attrib.mepSystem.isForPipework ? "true" : "false");
		writer->WriteStr (attrib.mepSystem.isForCabling ? "true" : "false");
		writer->WrNewLine ();
		writer->WriteInt (attrib.mepSystem.contourPen);
		writer->WriteInt (attrib.mepSystem.fillPen);
		writer->WriteInt (attrib.mepSystem.fillBgPen);
		writer->WriteAttrIndex (attrib.mepSystem.fillInd);
		writer->WriteAttrIndex (attrib.mepSystem.centerLTypeInd);
		writer->WriteInt (attrib.mepSystem.centerLinePen);
		writer->WriteAttrIndex (attrib.mepSystem.materialInd);
		writer->WriteAttrIndex (attrib.mepSystem.insulationMatInd);
		err = writer->WrEndblk ();
	});

	return err;
}		// Do_SaveAs_MEPSystems
#endif


// -----------------------------------------------------------------------------
//	Save all building materials
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_BuildingMaterial (void)
{
	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_BuildingMaterialID, [&] (API_Attribute& attrib) {
		writer->WriteBlock ("BUILDING_MATERIAL", attrib.header.index);
		writer->WriteName (attrib.header.name, DumpWriter::WithNewLine);
		Int32 uiPriority = 0;
		ACAPI_Element_Elem2UIPriority (&attrib.buildingMaterial.connPriority, &uiPriority);
		writer->WriteInt (uiPriority, DumpWriter::WithNewLine);
		GS::UniString name;
		DumpUtils::GetAttrName (nullptr, API_FilltypeID, attrib.buildingMaterial.cutFill, &name);
		writer->WriteInt (attrib.buildingMaterial.cutFillPen);
		writer->WriteInt (attrib.buildingMaterial.cutFillBackgroundPen);
		writer->WriteName (name, DumpWriter::WithNewLine);
		DumpUtils::GetAttrName (nullptr, API_MaterialID, attrib.buildingMaterial.cutMaterial, &name);
		writer->WriteName (name, DumpWriter::WithNewLine);
		writer->WriteInt (attrib.buildingMaterial.cutFillOrientation, DumpWriter::WithNewLine);
		writer->WriteFloat (attrib.buildingMaterial.thermalConductivity);
		writer->WriteFloat (attrib.buildingMaterial.density);
		writer->WriteFloat (attrib.buildingMaterial.heatCapacity);
		err = writer->WrEndblk ();
	});

	return err;
}		// Do_SaveAs_CompStruct


// -----------------------------------------------------------------------------
// Save the attributes of the actual project
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Attribute (void)
{
	GSErrCode	err;

	err = Do_SaveAs_Pen ();

	if (err == NoError)
		err = Do_SaveAs_Linetype ();

	if (err == NoError)
		err = Do_SaveAs_Filltype ();

	if (err == NoError)
		err = Do_SaveAs_CompStruct ();

	if (err == NoError)
		err = Do_SaveAs_Material ();

	if (err == NoError)
		err = Do_SaveAs_ZoneCategories ();

#ifndef NO_MEP_FEATURES
	if (err == NoError)
		err = Do_SaveAs_MEPSystems ();
#endif

	if (err == NoError)
		err = Do_SaveAs_BuildingMaterial ();

	return err;

}		// Do_SaveAs_Attribute


// -----------------------------------------------------------------------------
//	Save some parameters of an object based element
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Object (API_ElemTypeID elemsTypeID)
{
	API_Element			element;
	API_LibPart 		libPart;
	GSErrCode			err;

	GS::Array<API_Guid> elemList;
	err = ACAPI_Element_GetElemList (elemsTypeID, &elemList);

	for (GS::Array<API_Guid>::ConstIterator it = elemList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);

		if (err == NoError) {
			libPart = {};

			GS::UniString infoString;
			switch (elemsTypeID) {
				case API_ObjectID:	writer->WriteBlock ("SYMBOL", element.header.guid);
									ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
									writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);
									libPart.index = element.object.libInd;
									break;

				case API_LampID:	writer->WriteBlock ("LIGHT", element.header.guid);
									ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
									writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);
									libPart.index = element.lamp.libInd;
									break;

				case API_ZoneID:	writer->WriteBlock ("ROOM", element.header.guid);
									ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
									writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);
									libPart.index = element.zone.libInd;
									break;

				default:			writer->WriteBlock ("???", element.header.guid);
			}

			err = ACAPI_LibraryPart_Get (&libPart);
			if (libPart.location != nullptr) {
				delete libPart.location;
				libPart.location = nullptr;
			}

			if (err == NoError) {
				writer->WriteName (GS::UniString (libPart.docu_UName), DumpWriter::WithNewLine);

				if (elemsTypeID == API_ZoneID) {
					writer->WriteFloat (element.zone.pos.x);
					writer->WriteFloat (element.zone.pos.y);
					writer->WriteFloat (0.0);
					writer->WriteAngle (0.0);
					writer->WrNewLine ();
					writer->WriteFloat (1.0);
					writer->WriteFloat (1.0);
					writer->WrNewLine ();
					writer->WriteInt (0);
					writer->WriteInt (0, DumpWriter::WithNewLine);
					writer->WriteInt (0, DumpWriter::WithNewLine);
				} else {
					writer->WriteFloat (element.object.pos.x);
					writer->WriteFloat (element.object.pos.y);
					writer->WriteFloat (element.object.level);
					writer->WriteAngle (element.object.angle);
					writer->WrNewLine ();
					writer->WriteFloat (element.object.xRatio);
					writer->WriteFloat (element.object.yRatio);
					writer->WrNewLine ();
					writer->WriteInt (element.object.reflected);
					writer->WriteInt (element.object.lightIsOn, DumpWriter::WithNewLine);
					writer->WriteAttrIndex (element.object.mat, DumpWriter::WithNewLine);
				}

				writer->WriteParams (&element);
				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid);

				if (err == NoError)
					err = writer->WrProperties (element.header.guid);
			}
			if (err == NoError)
				err = writer->WrEndblk ();
		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Object


// -----------------------------------------------------------------------------
//	Save some parameters of walls
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Wall (void)
{
	API_Element			element;
	double				dirangle;
	double				wradius;
	GS::UniString		name;
	GSErrCode			err;

	GS::Array<API_Guid> wallList;
	err = ACAPI_Element_GetElemList (API_WallID, &wallList);

	for (GS::Array<API_Guid>::ConstIterator it = wallList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);

		if (err == NoError) {
			writer->WriteBlock ("WALL", element.header.guid);
			GS::UniString infoString;
			ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
			writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);

			err = writer->Wr2D_Poly (&element, &element.wall.poly);

			DumpUtils::GetWallData (&element.wall, &dirangle, &wradius);

			writer->WriteAngle (dirangle);
			writer->WrNewLine ();
			writer->WriteAngle (element.wall.angle);
			writer->WrNewLine ();

			writer->WriteFloat (element.wall.height);
			writer->WriteFloat (element.wall.bottomOffset);
			writer->WriteFloat (element.wall.topOffset);
			writer->WriteFloat (element.wall.thickness);
			writer->WrNewLine ();

			if (element.wall.modelElemStructureType == API_BasicStructure)
				DumpUtils::GetAttrName (nullptr, API_BuildingMaterialID, element.wall.buildingMaterial, &name);

			writer->WriteName (name, DumpWriter::WithNewLine);

			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.wall.refMat.value, &name);
			writer->WriteName (name);
			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.wall.oppMat.value, &name);
			writer->WriteName (name);
			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.wall.sidMat.value, &name);
			writer->WriteName (name, DumpWriter::WithNewLine);

			API_ElementMemo memo;
			memo = {};
			ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_WallWindows | APIMemoMask_WallDoors);
			if (memo.wallWindows != nullptr) {
				GSSize	nWinds = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.wallWindows)) / sizeof (API_Guid);
				for (GSIndex i = 0; i < nWinds; i++)
					writer->WrAnOpening (API_WindowID, memo.wallWindows[i], &element, dirangle, wradius);
			}
			if (memo.wallDoors != nullptr) {
				GSSize	nDoors = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.wallDoors)) / sizeof (API_Guid);
				for (GSIndex i = 0; i < nDoors; i++)
					writer->WrAnOpening (API_DoorID, memo.wallDoors[i], &element, dirangle, wradius);
			}
			ACAPI_DisposeElemMemoHdls (&memo);

			if (fabs (dirangle) > EPS) {
				writer->WriteFloat (wradius);
				writer->WrNewLine ();
			}
			err = writer->WrCutPlane ( &element);

			if (err == NoError)
				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid);

			if (err == NoError)
				err = writer->WrProperties (element.header.guid);

			if (err == NoError)
				err = writer->WrEndblk ();

		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Wall


// -----------------------------------------------------------------------------
//	Save parameters of columns
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Column (void)
{
	API_Element			element;
	GS::UniString		buffer;
	GSErrCode			err;

	GS::Array<API_Guid> columnList;
	err = ACAPI_Element_GetElemList (API_ColumnID, &columnList);

	for (GS::Array<API_Guid>::ConstIterator it = columnList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);
		if (err == NoError) {

			API_ElementMemo memo;
			memo = {};
			ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_ColumnSegment);

			if (memo.columnSegments == nullptr)
				continue;

			writer->WriteBlock ("COLUMN", element.header.guid);
			GS::UniString infoString;
			ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
			writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);

			writer->WriteFloat (element.column.origoPos.x);
			writer->WriteFloat (element.column.origoPos.y);
			writer->WriteAngle (element.column.axisRotationAngle);
			writer->WrNewLine ();

			writer->WriteFloat (memo.columnSegments[0].assemblySegmentData.nominalWidth);
			writer->WriteFloat (memo.columnSegments[0].assemblySegmentData.nominalHeight);
			writer->WriteFloat (static_cast<Int32> (memo.columnSegments[0].venType));
			writer->WriteFloat (memo.columnSegments[0].venThick);
			writer->WrNewLine ();

			writer->WriteFloat (element.column.height);
			writer->WriteFloat (element.column.bottomOffset);
			writer->WriteFloat (element.column.topOffset);
			writer->WrNewLine ();

			DumpUtils::GetAttrName (nullptr, API_MaterialID, memo.columnSegments[0].extrusionSurfaceMaterial.value, &buffer);
			writer->WriteName (buffer, DumpWriter::WithNewLine);

			if (memo.columnSegments[0].assemblySegmentData.modelElemStructureType == API_BasicStructure) {
				DumpUtils::GetAttrName (nullptr, API_BuildingMaterialID, memo.columnSegments[0].assemblySegmentData.buildingMaterial, &buffer);
				writer->WriteName (buffer, DumpWriter::WithNewLine);

				DumpUtils::GetAttrName (nullptr, API_BuildingMaterialID, memo.columnSegments[0].venBuildingMaterial, &buffer);
				writer->WriteName (buffer, DumpWriter::WithNewLine);
			}

			err = writer->WrCutPlane ( &element);

			if (err == NoError)
				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid);
			if (err == NoError)
				err = writer->WrProperties (element.header.guid);
			if (err == NoError)
				err = writer->WrEndblk ();
		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Column


// -----------------------------------------------------------------------------
//	Save parameters of slabs
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Slab (void)
{
	API_Element			element;
	GS::UniString		name;
	GSErrCode			err;

	GS::Array<API_Guid> slabList;
	err = ACAPI_Element_GetElemList (API_SlabID, &slabList);

	for (GS::Array<API_Guid>::ConstIterator it = slabList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);
		if (err == NoError) {
			writer->WriteBlock ("SLAB", element.header.guid);
			GS::UniString infoString;
			ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
			writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);

			if (element.slab.modelElemStructureType == API_BasicStructure) {
				DumpUtils::GetAttrName (nullptr, API_BuildingMaterialID, element.slab.buildingMaterial, &name);
				writer->WrNewLine ();
				writer->WriteName (name, DumpWriter::WithNewLine);
			}

			err = writer->Wr2D_Poly (&element, &element.slab.poly);

			if (err == NoError) {
				writer->WriteFloat (element.slab.level);
				writer->WrNewLine ();
				writer->WriteFloat (element.slab.thickness);
				writer->WrNewLine ();

				DumpUtils::GetAttrName (nullptr, API_MaterialID, element.slab.topMat.value, &name);
				writer->WriteName (name);
				DumpUtils::GetAttrName (nullptr, API_MaterialID, element.slab.botMat.value, &name);
				writer->WriteName (name);
				DumpUtils::GetAttrName (nullptr, API_MaterialID, element.slab.sideMat.value, &name);
				writer->WriteName (name, DumpWriter::WithNewLine);

				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid);
				if (err == NoError)
					err = writer->WrProperties (element.header.guid);

				if (err == NoError)
					err = writer->WrEndblk ();
			}
		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Slab


// -----------------------------------------------------------------------------
//	Save parameters of roofs
// -----------------------------------------------------------------------------

static GSErrCode Do_SaveAs_Roof (void)
{
	API_Element			element;
	GS::UniString		name;
	GSErrCode			err;

	GS::Array<API_Guid> roofList;
	err = ACAPI_Element_GetElemList (API_RoofID, &roofList);

	for (GS::Array<API_Guid>::ConstIterator it = roofList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);
		if (err == NoError) {
			writer->WriteBlock ("ROOF", element.header.guid);
			GS::UniString infoString;
			ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
			writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);

			writer->WriteFloat (element.roof.shellBase.level);
			writer->WriteFloat (element.roof.shellBase.thickness);
			writer->WrNewLine ();

			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.roof.shellBase.topMat.value, &name);
			writer->WriteName (name);
			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.roof.shellBase.botMat.value, &name);
			writer->WriteName (name);
			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.roof.shellBase.sidMat.value, &name);
			writer->WriteName (name, DumpWriter::WithNewLine);

			if (element.roof.roofClass == API_PlaneRoofID) {
				writer->WriteFloat (element.roof.u.planeRoof.baseLine.c1.x);
				writer->WriteFloat (element.roof.u.planeRoof.baseLine.c1.y);
				writer->WriteFloat (element.roof.u.planeRoof.baseLine.c2.x);
				writer->WriteFloat (element.roof.u.planeRoof.baseLine.c2.y);
				writer->WrNewLine ();
				writer->WriteAngle (element.roof.u.planeRoof.angle);
				writer->WrNewLine ();
				err = writer->Wr2D_Poly (&element, &(element.roof.u.planeRoof.poly));
			} else {
				err = writer->Wr2D_PivotPoly (&element, &(element.roof.u.polyRoof.pivotPolygon));
				if (err == NoError)
					err = writer->Wr2D_Poly (&element, &(element.roof.u.polyRoof.contourPolygon));
			}

			if (err == NoError)
				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid, element.roof.roofClass == API_PlaneRoofID);

			if (err == NoError)
				err = writer->WrProperties (element.header.guid);

			if (err == NoError)
				err = writer->WrEndblk ();

		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Roof


// -----------------------------------------------------------------------------
//	Save parameters of shells
// -----------------------------------------------------------------------------

static GSErrCode Do_SaveAs_Shell (void)
{
	API_Element			element;
	GS::UniString		name;
	GSErrCode			err;

	GS::Array<API_Guid> shellList;
	err = ACAPI_Element_GetElemList (API_ShellID, &shellList);

	for (GS::Array<API_Guid>::ConstIterator it = shellList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);
		if (err == NoError) {
			writer->WriteBlock ("SHELL", element.header.guid);
			GS::UniString infoString;
			ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
			writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);

			writer->WrNewLine ();
			writer->WriteFloat (element.shell.shellBase.level);
			writer->WriteFloat (element.shell.shellBase.thickness);
			writer->WrNewLine ();

			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.shell.shellBase.topMat.value, &name);
			writer->WriteName (name);
			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.shell.shellBase.botMat.value, &name);
			writer->WriteName (name);
			DumpUtils::GetAttrName (nullptr, API_MaterialID, element.shell.shellBase.sidMat.value, &name);
			writer->WriteName (name, DumpWriter::WithNewLine);

			err = writer->Wr2D_ShellShape (&element);

			if (err == NoError)
				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid);

			if (err == NoError)
				err = writer->WrProperties (element.header.guid);

			if (err == NoError)
				err = writer->WrEndblk ();

		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Shell


// -----------------------------------------------------------------------------
//	Save parameters of morph
// -----------------------------------------------------------------------------

static GSErrCode Do_SaveAs_Morph (void)
{
	API_Element			element;
	GSErrCode			err;

	GS::Array<API_Guid> morphList;
	err = ACAPI_Element_GetElemList (API_MorphID, &morphList);

	for (GS::Array<API_Guid>::ConstIterator it = morphList.Enumerate (); it != nullptr && err == NoError; ++it) {
		element = {};
		element.header.guid = *it;
		err = ACAPI_Element_Get (&element);
		if (err == NoError) {
			writer->WriteBlock ("MORPH", element.header.guid);
			GS::UniString infoString;
			ACAPI_Element_GetCompoundInfoString (&element.header.guid, &infoString);
			writer->WriteElemHead (&element, infoString.ToCStr ().Get (), nullptr);

			writer->WrNewLine ();

			err = writer->Wr_Morph (&element);

			if (err == NoError)
				err = writer->WriteSurfVol (element.header.type.typeID, element.header.guid);

			if (err == NoError)
				err = writer->WrProperties (element.header.guid);

			if (err == NoError)
				err = writer->WrEndblk ();

		} else {
			if (err == APIERR_DELETED)
				err = NoError;
		}
	}

	return err;
}		// Do_SaveAs_Morph


// -----------------------------------------------------------------------------
//	Save some element types into a file
// -----------------------------------------------------------------------------

static GSErrCode	Do_SaveAs_Elements (void)
{
	GSErrCode err = Do_SaveAs_Wall ();

	if (err == NoError)
		err = Do_SaveAs_Column ();

	if (err == NoError)
		err = Do_SaveAs_Object (API_ObjectID);

	if (err == NoError)
		err = Do_SaveAs_Object (API_LampID);

	if (err == NoError)
		err = Do_SaveAs_Slab ();

	if (err == NoError)
		err = Do_SaveAs_Roof ();

	if (err == NoError)
		err = Do_SaveAs_Shell ();

	if (err == NoError)
		err = Do_SaveAs_Morph ();

	if (err == NoError)
		err = Do_SaveAs_Object (API_ZoneID);

	return err;
}		// Do_SaveAs_Elements


// -----------------------------------------------------------------------------
//	Dump parts of the plan
// -----------------------------------------------------------------------------

static void		Do_SaveAs (void)
{
	API_StoryInfo		storyInfo;
	time_t				t;
	char				buffer[256];
	char				dateStr[128];
	short 				i;
	GSErrCode			err = NoError;;

	IO::Location		temporaryFolderLoc;
	API_SpecFolderID	temporaryFolderID = API_TemporaryFolderID;
	err = ACAPI_ProjectSettings_GetSpecFolder (&temporaryFolderID, &temporaryFolderLoc);
	if (err == NoError)
		err = writer->Open (IO::Location (temporaryFolderLoc, IO::Name ("PlanDump.txt")));
	if (err == NoError) {
		GS::UniString	path = IO::Location (temporaryFolderLoc, IO::Name ("PlanDump.txt")).ToLogText ();
	    sprintf (buffer, "Plan Dump path is \"%s\"", path.ToCStr ().Get ());
		ACAPI_WriteReport (buffer, false);
	}

	// ========== header ==========
	if (err == NoError) {
		time (&t);
		strcpy (dateStr, ctime (&t));
		dateStr[24] = 0;													// end of string: 0

		sprintf (buffer, "# Archicad PlanDump with API,   %s", dateStr);
		writer->WriteStr (buffer, DumpWriter::WithNewLine);
	}

	// ======= environment ========
	if (err == NoError) {

		writer->WriteStr ("ENVIR");
		writer->WriteInt (0);
		API_WorkingUnitPrefs	prefs;
		err = ACAPI_ProjectSetting_GetPreferences (&prefs,APIPrefs_WorkingUnitsID);
		if (err == NoError) {
			SetLengthUnit (prefs.lengthUnit, buffer);
			writer->WriteStr (buffer);
			writer->WriteInt (prefs.lenDecimals, DumpWriter::WithNewLine);

			SetAngleUnit (prefs.angleUnit, buffer);
			writer->WriteStr (buffer);
			writer->WriteInt (prefs.angleDecimals, DumpWriter::WithNewLine);

			err = ACAPI_Drawing_GetDrawingScale (&dScale);
			if (err == NoError) {
				writer->WriteInt ((Int32) dScale);
				writer->WrEndblk ();
				err = writer->WrNewLine ();
			}
		}
	}

    // ======== attributes ========
    if (err == NoError)
		err = Do_SaveAs_Attribute ();

	// ======== stories ========
	if (err == NoError) {
		err = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
		for (i = 0; (i <= storyInfo.lastStory - storyInfo.firstStory) && (err == NoError); i++) {
			writer->WriteStr ("STORY");
			writer->WriteInt (i + storyInfo.firstStory);

			if (GS::ucslen ((*storyInfo.data)[i].uName) == 0) {
				sprintf (buffer, "%d. Story", i + storyInfo.firstStory);
				writer->WriteName (buffer, DumpWriter::WithNewLine);
			} else
				writer->WriteName ((*storyInfo.data)[i].uName, DumpWriter::WithNewLine);

			if (i == 0)
				sprintf (buffer, "%.6lf", (*storyInfo.data)[i].level);
			else
				sprintf (buffer, "%.6lf", (*storyInfo.data)[i].level - (*storyInfo.data)[i-1].level);

			writer->WriteStr (buffer);
			err = writer->WrEndblk ();
		}
		BMKillHandle ((GSHandle*) &storyInfo.data);
	}

	// ========= elements =========
	if (err == NoError)
		err = Do_SaveAs_Elements ();

	if (err != NoError) {
	    sprintf (buffer, "Error code:  %d", (int) err);
	    ACAPI_WriteReport (buffer, true);
		writer->WriteStr ("ErrorCode: ");
		writer->WriteInt (err);
	}

	writer->Close ();
}		// Do_SaveAs


// -----------------------------------------------------------------------------

static IO::Location GetStoredProjectLoc ()
{
	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::UserDocuments, &fileLocation);
	fileLocation.AppendToLocal (IO::Name ("ACAPI_ProjectCompare"));
	fileLocation.AppendToLocal (IO::Name ("ForCompare.pln"));

	return fileLocation;
}


static IO::Location GetStoredModelLoc ()
{
	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::UserDocuments, &fileLocation);
	fileLocation.AppendToLocal (IO::Name ("ACAPI_ModelCompare"));
	fileLocation.AppendToLocal (IO::Name ("ForCompare"));

	return fileLocation;
}


static IO::Location GetStoredContextLoc ()
{
	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::UserDocuments, &fileLocation);
	fileLocation.AppendToLocal (IO::Name ("ACAPI_ContextCompare"));
	fileLocation.AppendToLocal (IO::Name ("ForCompare"));

	return fileLocation;
}


enum DifferenceTypeID {
	NEW_ELEM = 0,
	MODIFIED_ELEM,
	DELETED_ELEM
};


static void	DumpElem (const API_Guid& guid, DifferenceTypeID diffType, GS::UniString& reportStr)
{
	static GS::HashTable<API_ElemTypeID, GS::UniString> elemNames;
	static GS::HashTable<DifferenceTypeID, GS::UniString> diffNames;

	if (elemNames.GetSize () == 0) {
		for (API_ElemTypeID ii = API_FirstElemType; ii <= API_LastElemType; ++((int &) ii))
			elemNames.Add (ii, ElemID_To_Name (ii));
	}

	if (diffNames.GetSize () == 0) {
		diffNames.Add (NEW_ELEM,	  "--- NEW ELEMENT ---");
		diffNames.Add (MODIFIED_ELEM, "- MODIFIED ELEMENT ");
		diffNames.Add (DELETED_ELEM,  "- DELETED ELEMENT -");
	}

	reportStr += "--------------" + diffNames[diffType] + "-------------\n";
	reportStr += "GUID: " + APIGuidToString (guid) + "\n";

	if (diffType != DELETED_ELEM) {
		API_Elem_Head elemHead = {};
		elemHead.guid = guid;

		if (ACAPI_Element_GetHeader (&elemHead) == NoError && elemNames.ContainsKey (elemHead.type.typeID))
			reportStr += "Type: " + elemNames[elemHead.type.typeID] + "\n";
	}

	reportStr += "----------------------------------------------\n";
}


static void DumpComparisonResult (	const char* diffStr,
									const GS::HashSet<API_Guid>& newElems,
									const GS::HashSet<API_Guid>& modifiedElems,
									const GS::HashSet<API_Guid>& deletedElems)
{
	GS::UniString reportStr = diffStr;
	for (const API_Guid& elem : newElems) {
		DumpElem (elem, NEW_ELEM, reportStr);
	}
	for (const API_Guid& elem : modifiedElems) {
		DumpElem (elem, MODIFIED_ELEM, reportStr);
	}
	for (const API_Guid& elem : deletedElems) {
		DumpElem (elem, DELETED_ELEM, reportStr);
	}
	reportStr += "----------------------------------------------\n";
	ACAPI_WriteReport (reportStr.ToCStr ().Get (), false);
}


// -----------------------------------------------------------------------------
//	Store project for compare
// -----------------------------------------------------------------------------

static void	Do_StoreProjectForCompare ()
{
	API_ElemDifferenceGeneratorState diffGeneratorState = {};
	diffGeneratorState.stateType = APIDiffState_InFile;
	diffGeneratorState.fileLocation = GetStoredProjectLoc ();
	GSErrCode err = ACAPI_DifferenceGenerator_GetState (APIDiff_ModificationStampBased, &diffGeneratorState);
	if (err != NoError)
		ErrorBeep ("APIDiff_ModificationStampBased ACAPI_DifferenceGenerator_GetState failed", err);
}


// -----------------------------------------------------------------------------
//	Compare actual project to the stored project
// -----------------------------------------------------------------------------

static void	Do_CompareToStoredProject ()
{
	API_ElemDifference elemDifference = {};
	API_ElemDifferenceGeneratorState diffGeneratorState = {};
	diffGeneratorState.stateType = APIDiffState_InFile;
	diffGeneratorState.fileLocation = GetStoredProjectLoc ();

	GSErrCode err = ACAPI_DifferenceGenerator_GenerateDifference (APIDiff_ModificationStampBased, &diffGeneratorState, nullptr, elemDifference);
	if (err != NoError) {
		ErrorBeep ("APIDiff_ModificationStampBased  ACAPI_DifferenceGenerator_GenerateDifference failed", err);
	} else {
		DumpComparisonResult ("-------------- APIDiff_ModificationStampBased Difference: ----\n", elemDifference.newElements, elemDifference.modifiedElements, elemDifference.deletedElements);
	}
}


// -----------------------------------------------------------------------------
//	Store model for compare
// -----------------------------------------------------------------------------

static void	Do_StoreModelForCompare ()
{
	API_ElemDifferenceGeneratorState diffGeneratorState = {};
	diffGeneratorState.stateType = APIDiffState_InFile;
	diffGeneratorState.fileLocation = GetStoredModelLoc ();
	GSErrCode err = ACAPI_DifferenceGenerator_GetState (APIDiff_3DModelBased, &diffGeneratorState);
	if (err != NoError)
		ErrorBeep ("APIDiff_3DModelBased ACAPI_DifferenceGenerator_GetState failed", err);
}


// -----------------------------------------------------------------------------
//	Compare actual model to the stored model
// -----------------------------------------------------------------------------

static void	Do_CompareToStoredModel ()
{
	API_ElemDifference elemDifference = {};
	API_ElemDifferenceGeneratorState diffGeneratorState1 = {};
	diffGeneratorState1.stateType = APIDiffState_InFile;
	diffGeneratorState1.fileLocation = GetStoredModelLoc ();
	API_ElemDifferenceGeneratorState diffGeneratorState2 = {};
	diffGeneratorState2.stateType = APIDiffState_CurrentProject;

	GSErrCode err = ACAPI_DifferenceGenerator_GenerateDifference (APIDiff_3DModelBased, &diffGeneratorState1, &diffGeneratorState2, elemDifference);
	if (err != NoError) {
		ErrorBeep ("APIDiff_3DModelBased ACAPI_DifferenceGenerator_GenerateDifference failed", err);
	} else {
		DumpComparisonResult ("-------------- APIDiff_3DModelBased Difference: ------\n", elemDifference.newElements, elemDifference.modifiedElements, elemDifference.deletedElements);
	}
}


// -----------------------------------------------------------------------------
//	Store context for compare
// -----------------------------------------------------------------------------

static void Do_StoreContextForCompare ()
{
	API_Guid currentViewGuid = APINULLGuid;
	GSErrCode err = NoError;
	{
		API_NavigatorItem parentNavigatorItem;
		parentNavigatorItem.itemType = API_PerspectiveNavItem;
		parentNavigatorItem.mapId = API_ProjectMap;

		GS::Array<API_NavigatorItem> navigatorItemsArray;
		err = ACAPI_Navigator_SearchNavigatorItem (&parentNavigatorItem, &navigatorItemsArray);
		if (DBERROR (err != NoError))
			return;

		bool viewGuidFound = false;
		navigatorItemsArray.Enumerate ([&](const API_NavigatorItem& navigatorItem) {
			currentViewGuid = navigatorItem.guid;
			viewGuidFound = true;
		});

		if (DBERROR (!viewGuidFound)) {
			ErrorBeep ("View not found failed", err);
			return;
		}
	}
	API_ElemDifferenceGeneratorState diffGeneratorState = {};
	diffGeneratorState.stateType = APIDiffState_InFile;
	diffGeneratorState.fileLocation = GetStoredContextLoc ();
	diffGeneratorState.viewGuid = currentViewGuid;
	err = ACAPI_DifferenceGenerator_GetState (APIDiff_ContextBased, &diffGeneratorState);
	if (err != NoError)
		ErrorBeep ("APIDiff_ContextBased ACAPI_DifferenceGenerator_GetState failed", err);
}


// -----------------------------------------------------------------------------
//	Compare actual context to the stored context
// -----------------------------------------------------------------------------

static void	Do_CompareToStoredContext ()
{
	API_Guid currentViewGuid = APINULLGuid;
	GSErrCode err = NoError;
	//API_Guid viewGuid;
	{
		API_NavigatorItem parentNavigatorItem;
		parentNavigatorItem.itemType = API_PerspectiveNavItem;
		parentNavigatorItem.mapId = API_ProjectMap;

		GS::Array<API_NavigatorItem> navigatorItemsArray;
		err = ACAPI_Navigator_SearchNavigatorItem (&parentNavigatorItem, &navigatorItemsArray);
		if (DBERROR (err != NoError))
			return;

		bool viewGuidFound = false;
		navigatorItemsArray.Enumerate ([&](const API_NavigatorItem& navigatorItem) {
			currentViewGuid = navigatorItem.guid;
			viewGuidFound = true;
		});

		if (DBERROR (!viewGuidFound)) {
			ErrorBeep ("View not found failed", err);
			return;
		}
	}
	API_ElemDifference elemDifference = {};
	API_ElemDifferenceGeneratorState diffGeneratorState1 = {};
	diffGeneratorState1.stateType = APIDiffState_InFile;
	diffGeneratorState1.fileLocation = GetStoredContextLoc ();
	diffGeneratorState1.viewGuid = currentViewGuid;
	API_ElemDifferenceGeneratorState diffGeneratorState2 = {};
	diffGeneratorState2.stateType = APIDiffState_CurrentProject;
	diffGeneratorState2.viewGuid = currentViewGuid;
	err = ACAPI_DifferenceGenerator_GenerateDifference (APIDiff_ContextBased, &diffGeneratorState1, &diffGeneratorState2, elemDifference);
	if (err != NoError) {
		ErrorBeep ("APIDiff_ContextBased ACAPI_DifferenceGenerator_GenerateDifference failed", err);
	} else {
		DumpComparisonResult ("-------------- APIDiff_ContextBased Difference: ------\n", elemDifference.newElements, elemDifference.modifiedElements, elemDifference.deletedElements);
	}
}


// -----------------------------------------------------------------------------
// Parse the menu events
// -----------------------------------------------------------------------------

static GSErrCode MenuCommandProc (const API_MenuParams* menuParams)
{
	try {
		switch (menuParams->menuItemRef.itemIndex) {
			case IDM_PlanDump:					Do_SaveAs ();					break;
			case IDM_StoreProject:				Do_StoreProjectForCompare ();	break;
			case IDM_CompareToStoredProject:	Do_CompareToStoredProject ();	break;
			case IDM_StoreModel:				Do_StoreModelForCompare ();		break;
			case IDM_CompareToStoredModel:		Do_CompareToStoredModel ();		break;
			case IDM_StoreContext:				Do_StoreContextForCompare ();	break;
			case IDM_CompareToStoredContext:	Do_CompareToStoredContext ();	break;
			default:															break;
		}
	}
	catch (...) {
		return APIERR_CANCEL;
	}

	return NoError;
}		// MenuCommandProc


// =============================================================================
//
// Required functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Called when the Add-On is going to be registered
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envirParams)
{
	RSGetIndString (&envirParams->addOnInfo.name, IDR_AddOnDescStrings, IDS_AddOnName, ACAPI_GetOwnResModule ());
	RSGetIndString (&envirParams->addOnInfo.description, IDR_AddOnDescStrings, IDS_AddOnDesc, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode	err = NoError;

	// Register menus
	err = ACAPI_MenuItem_RegisterMenu (IDR_PlanDumpMenu, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore); //or MenuFlag_Default

	return err;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	ACAPI_MenuItem_InstallMenuHandler (IDR_PlanDumpMenu, MenuCommandProc);

	writer = new DumpWriter ();

	return NoError;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	delete writer;

	return NoError;
}		// FreeData
