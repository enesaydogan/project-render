// *****************************************************************************
// Source code for the Attribute Test Add-On
// *****************************************************************************

// =============================================================================
//
// API Includes
//
// =============================================================================

#include	"VectorImageIterator.hpp"
#include	"ProfileVectorImageOperations.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"		// also includes APIdefs.h
#include	"APICommon.h"	// also includes GSRoot.hpp
#include	"OverriddenAttributes.hpp"
#include	"ProfileAdditionalInfo.hpp"
#include	"API_AttributeIndexUtils.hpp"

#ifdef JAMMAKE
static bool enableImportFormatRegistration = false;
#else
static bool enableImportFormatRegistration = true;
#endif

static void		DisposeAttribute (API_Attribute* attrib)
{
	switch (attrib->header.typeID) {
		case API_MaterialID:			delete attrib->material.texture.fileLoc;	break;
		default:						/* nothing has to be disposed */			break;
	}
}


static GS::Array<GS::Guid>	GetSomeAttributes (API_AttrTypeID typeID, USize maxAmount)
{
	GS::Array<GS::Guid> guids;
	ACAPI_Attribute_EnumerateAttributesByType (typeID, [&] (API_Attribute& attribute) {
		if (guids.GetSize () < maxAmount)
			guids.Push (APIGuid2GSGuid (attribute.header.guid));

		DisposeAttribute (&attribute);
	});

	return guids;
}


// -----------------------------------------------------------------------------
//  Add User Data to attribute
// -----------------------------------------------------------------------------

constexpr short USERDATA_VERSION = 1234;

template<typename UDType>
GSErrCode AddUserDataToAttribute (API_Attr_Head* attrHead, UDType value)
{
	API_AttributeUserData	userData = {};

	userData.dataVersion = USERDATA_VERSION;
	userData.platformSign = GS::Act_Platform_Sign;
	userData.dataHdl = BMhAll (sizeof (UDType));
	* (UDType *) (*userData.dataHdl) = value;

	GSErrCode err = ACAPI_UserData_SetUserData (attrHead, &userData);
	if (err != NoError) {
		WriteReport_Err ("Error while setting the attribute's user data", err);
	} else {
		WriteReport ("The new attribute's user data has been set");
	}

	return err;
}

// -----------------------------------------------------------------------------
//  Check originally added user data
// -----------------------------------------------------------------------------

template<typename UDType>
GSErrCode CheckAttributeUserData (API_Attr_Head* attrHead, UDType /*refValue*/)
{
	API_AttributeUserData	userData = {};

	GSErrCode err = ACAPI_UserData_GetUserData (attrHead, &userData);
	if (err != NoError || userData.dataVersion != USERDATA_VERSION || BMhGetSize (userData.dataHdl) != sizeof (UDType)) {
		if (err == NoError)
			err = APIERR_BADVALUE;
		WriteReport_Err ("Bad user data in attribute", err);
	} else {
		UDType value = * (UDType *) (*userData.dataHdl);
		WriteReport ("The attribute's user data at creation was set to %d!", (int) value);
	}

	return err;
}


// =============================================================================
//
// Attribute functions
//
// =============================================================================

// -----------------------------------------------------------------------------
//  Count the attribute instances
// -----------------------------------------------------------------------------

static void		Do_CountAttributes (void)
{
	API_AttrTypeID	typeID;
	UInt32			count;
	GSErrCode		err = NoError;

	WriteReport ("# Count attributes:");
	WriteReport ("#   - report the maximum index for each attribute type");

	for (typeID = API_FirstAttributeID; typeID <= API_LastAttributeID; ((Int32 &) typeID)++) {
		err = ACAPI_Attribute_GetNum (typeID, count);
		if (err != NoError) {
			WriteReport_Err ("Error in ACAPI_Attribute_GetNum", err);
			continue;
		}

		WriteReport ("%-10s: %d", AttrID_To_Name (typeID), count);
	}

	WriteReport_End (err);

	return;
}		// Do_CountAttributes


// -----------------------------------------------------------------------------
//  List all the attributes
// -----------------------------------------------------------------------------

static void		Do_ListAttributes (void)
{
	API_AttrTypeID	typeID;
	GSErrCode		err = NoError;

	WriteReport ("# List attributes:");
	WriteReport ("#   - scan all attribute types and show the number of each type");
	WriteReport ("#   - deleted instances will be called \"DEL\"");

	for (typeID = API_FirstAttributeID; typeID <= API_LastAttributeID; ((Int32 &) typeID)++) {
		GS::Array<API_Attribute> attributes;
		err = ACAPI_Attribute_GetAttributesByType (typeID, attributes);
		if (err != NoError) {
			WriteReport_Err ("Error in ACAPI_Attribute_GetAttributesByType", err);
			continue;
		}

		WriteReport ("%s: %d", AttrID_To_Name (typeID), attributes.GetSize ());

		for (API_Attribute& attrib : attributes) {
			char guidStr[64];
			APIGuid2GSGuid (attrib.header.guid).ConvertToString (guidStr);
			WriteReport ("  [%3s]   {%s}  \"%s\"", attrib.header.index.ToUniString ().ToCStr ().Get (), guidStr, attrib.header.name);

			DisposeAttribute (&attrib);
		}
	}

	WriteReport_End (err);

	return;
}		// Do_ListAttributes


// -----------------------------------------------------------------------------
// Rename all attributes	(append index)
// -----------------------------------------------------------------------------

static GSErrCode	Do_RenameAttributes (bool giveInfo)
{
	API_AttrTypeID	typeID;
	GSErrCode		err = NoError;

	if (giveInfo) {
		WriteReport ("# Rename all attributes");
		WriteReport ("# (new name: append the attribute index");
		WriteReport ("#   - pens, fonts and Archicad Layer cannot be edited");
		WriteReport ("# Archicad menus must be updated");
		WriteReport ("# Elements must keep the references to the renamed attributes");
	}

	for (typeID = API_FirstAttributeID; typeID <= API_LastAttributeID; ((Int32 &) typeID)++) {
		if (giveInfo) {
			WriteReport ("Renaming %ss...", AttrID_To_Name (typeID));
		}
		err = ACAPI_Attribute_EnumerateAttributesByType (typeID, [&] (API_Attribute& attrib) {
			if (attrib.header.typeID == API_LayerID && attrib.header.index == APIApplicationLayerAttributeIndex)
				return;

			API_AttributeDefExt	defs = {};
			ACAPI_Attribute_GetDefExt (typeID, attrib.header.index, &defs);
			GS::UniString postfix = " " + attrib.header.index.ToUniString ();
			if (attrib.header.uniStringNamePtr != nullptr) {
				*attrib.header.uniStringNamePtr += postfix;
			} else {
				attrib.header.name[API_AttrNameLen - postfix.GetLength () - 1] = '\0';
				strcat (attrib.header.name, postfix.ToCStr ().Get ());
			}
			ACAPI_Attribute_ModifyExt (&attrib, &defs);
			ACAPI_DisposeAttrDefsHdlsExt (&defs);
			DisposeAttribute (&attrib);
		});
	}

	if (giveInfo)
		WriteReport_End (err);

	return err;
}		// Do_RenameAttributes


// -----------------------------------------------------------------------------
//  Copy all attributes
// -----------------------------------------------------------------------------

static void		Do_CopyAttributes (void)
{
	API_AttrTypeID	typeID;
	GSErrCode		err = NoError;

	WriteReport ("# Copy all attributes");
	WriteReport ("#   - font:   not editable");
	WriteReport ("#   - fill:   Solid, Empty, ind=1 not editable");
	WriteReport ("# Important:");
	WriteReport ("#   - copied instances may be created in place of deleted indices");
	WriteReport ("#   - so avoid recursion");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Try this at different drawing scales");

	for (typeID = API_FirstAttributeID; typeID <= API_LastAttributeID; ((Int32 &) typeID)++) {
		GS::Array<API_Attribute> attributes;
		err = ACAPI_Attribute_EnumerateAttributesByType (typeID, [&] (API_Attribute& attrib) {
			API_AttributeDefExt	defs;
			err = ACAPI_Attribute_GetDefExt (typeID, attrib.header.index, &defs);
			if (err == APIERR_BADID) {
				defs = {};
				err = NoError;
			}
			if (err == NoError) {
				if (attrib.header.typeID == API_LayerID && attrib.header.index == APIApplicationLayerAttributeIndex)
					strcpy (attrib.header.name, "Archicad Layer");
				attrib.header.name[API_AttrNameLen - 6] = '\0';
				strcat (attrib.header.name, " COPY");
				err = ACAPI_Attribute_CreateExt (&attrib, &defs);
			}
			ACAPI_DisposeAttrDefsHdlsExt (&defs);
			DisposeAttribute (&attrib);
		});
	}

	WriteReport_End (err);

	return;
}		// Do_CopyAttributes


// -----------------------------------------------------------------------------
//  Delete all attributes
// -----------------------------------------------------------------------------

static GSErrCode	Do_DeleteAttributes (bool giveInfo)
{
	API_AttrTypeID	typeID;
	GSErrCode		err = NoError;

	if (giveInfo) {
		WriteReport ("# Delete all attributes");
		WriteReport ("#   - ltype, layer:		ind=1 not deletable");
		WriteReport ("#   - pen, font:			not deletable");
		WriteReport ("#   - fill:				Solid, Empty not deletable");
		WriteReport ("#   - comp, mat, zcat, mepsys:	at least one attrib must remain");
		WriteReport ("#   - lcomb:		all deletable");
		WriteReport ("# Archicad menus must be updated");
	}

	for (typeID = API_FirstAttributeID; typeID <= API_LastAttributeID; ((Int32 &) typeID)++) {
		err = ACAPI_Attribute_EnumerateAttributesByType (typeID, [&] (API_Attribute& attrib) {
			err = ACAPI_Attribute_Delete (attrib.header);
			DisposeAttribute (&attrib);
		});
	}

	if (giveInfo)
		WriteReport_End (err);

	return err;
}		// Do_DeleteAttributes


// -----------------------------------------------------------------------------
//  Create a layer
// -----------------------------------------------------------------------------

static void		Do_CreateLayer_Training (void)
{
	API_Attribute	attrib;
	GS::UniString	layerName ("Training_0123456789012345678901234567890123456789.ext");
	GSErrCode		err;

	WriteReport ("# Create a layer \"Training\"");
	WriteReport ("#   - lock status specified");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;
	attrib.header.flags  = APILay_Locked;
	attrib.header.uniStringNamePtr = &layerName;

	err = ACAPI_Attribute_Create (&attrib, nullptr);
	if (err == APIERR_ATTREXIST) {
		WriteReport_Alert ("The layer exists. Its index is: #%d", attrib.header.index);
		return;
	}
	if (err != NoError) {
		WriteReport_Err ("Unable to create layer \"Training\"", err);
		return;
	}

	WriteReport_Alert ("The layer index is: #%hd", attrib.header.index);

	WriteReport_End (err);

	return;
}		// Do_CreateLayer_Training


// -----------------------------------------------------------------------------
// Create linetype attributes
//		- Dashed
//		- Symbol
// -----------------------------------------------------------------------------

static void		Do_CreateLinetypes_Training (void)
{
	API_DashItems		*dash;
	API_LineItems		*line;
	API_Attribute		line_atr = {};
	API_AttributeDef	line_atrdef = {};
	double				sq2;
	short				nItems;
	GSErrCode			err = NoError;

	WriteReport ("# Create linetype attributes:");
	WriteReport ("#   - dashed: \"Training Dashed\"");
	WriteReport ("#   - symbol: \"Training Symbol\"");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Line Types...\" dialog after execution");

	// Dashed linetype
	nItems = 3;
	line_atrdef.ltype_dashItems = (API_DashItems **) BMAllocateHandle (nItems * sizeof (API_DashItems), ALLOCATE_CLEAR, 0);
	if (line_atrdef.ltype_dashItems == nullptr) {
		WriteReport_Err ("Memory full", 0);
		return;
	}
	dash = *line_atrdef.ltype_dashItems;
	dash[0].dash = 3.0;
	dash[0].gap  = 1.5;
	dash[1].dash = 0.0;
	dash[1].gap  = 1.5;
	dash[2].dash = 0.0;
	dash[2].gap  = 1.5;

	line_atr.linetype.head.typeID = API_LinetypeID;
	line_atr.linetype.head.flags  = APILine_ScaleWithPlan;
	line_atr.linetype.defineScale = 10.0;
	strcpy (line_atr.linetype.head.name, "Training Dashed");
	line_atr.linetype.period = 7.5;
	line_atr.linetype.height = 0.0;
	line_atr.linetype.type   = APILine_DashedLine;
	line_atr.linetype.nItems = nItems;

	err = ACAPI_Attribute_Create (&line_atr, &line_atrdef);
	if (err == APIERR_ATTREXIST) {
		err = ACAPI_Attribute_Modify (&line_atr, &line_atrdef);
	}
	ACAPI_DisposeAttrDefsHdls (&line_atrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the dashed linetype", err);
	} else {
		WriteReport ("Dashed linetype \"Training Dashed\" was created successfully.");
	}


	// Symbol linetype
	BNZeroMemory (&line_atr, sizeof (API_Attribute));
	BNZeroMemory (&line_atrdef, sizeof (API_AttributeDef));

	nItems = 3;
	line_atrdef.ltype_lineItems = (API_LineItems **) BMAllocateHandle (nItems * sizeof(API_LineItems), ALLOCATE_CLEAR, 0);
	if (line_atrdef.ltype_lineItems == nullptr) {
		WriteReport_Err ("Memory full", 0);
		return;
	}
	line = *line_atrdef.ltype_lineItems;

	sq2 = sqrt (2.0);

	line[0].itemType = APILine_LineItemType;
	line[0].itemBegPos.x = 0.0;
	line[0].itemBegPos.y = 0.0;
	line[0].itemEndPos.x = 1.0;
	line[0].itemEndPos.y = 1.0;
	line[1].itemType = APILine_ArcItemType;
	line[1].itemBegPos.x = 0.0;
	line[1].itemBegPos.y = 2.0;
	line[1].itemRadius   = sq2;
	line[1].itemBegAngle = -45 * PI/180;
	line[1].itemEndAngle = 45 * PI/180;
	line[2].itemType = APILine_CircItemType;
	line[2].itemBegPos.x = 1.0 - 0.5/sq2;
	line[2].itemBegPos.y = 3.0 + 0.5/sq2;
	line[2].itemRadius   = 0.5;

	line_atr.linetype.head.typeID = API_LinetypeID;
	line_atr.linetype.head.flags  = APILine_FixScale;
	line_atr.linetype.defineScale = 0.0;
	strcpy (line_atr.linetype.head.name, "Training Symbol");
	line_atr.linetype.period = 4.5;
	line_atr.linetype.height = 2.0 * (3.5 + 0.5/sq2);
	line_atr.linetype.type   = APILine_SymbolLine;
	line_atr.linetype.nItems = nItems;

	err = ACAPI_Attribute_Create (&line_atr, &line_atrdef);
	if (err == APIERR_ATTREXIST) {
		err = ACAPI_Attribute_Modify (&line_atr, &line_atrdef);
	}
	ACAPI_DisposeAttrDefsHdls (&line_atrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the symbol linetype", err);
	} else {
		WriteReport ("Symbol linetype \"Training Symbol\" was created successfully.");
	}

	WriteReport_End (err);

	return;
}		// Do_CreateLinetypes_Training


// -----------------------------------------------------------------------------
// Create fill attributes
//		- Solid
//		- Empty
//		- Vector
//		- Symbol
// -----------------------------------------------------------------------------

static void		Do_CreateFills_Training (void)
{
	API_Attribute		fill_atr;
	API_AttributeDef	fill_atrdef;
	API_FillLine		*fillLine;
	API_Polygon			*poly;
	API_Coord			*coords;
	Int32				*subpoly;
	double				*lineLength;
	GSErrCode			err;

	WriteReport ("# Create the following fill attributes:");
	WriteReport ("#   - solid:  \"Training Solid\"");
	WriteReport ("#   - empty:  \"Training Empty\"");
	WriteReport ("#   - vector: \"Training Vector\"");
	WriteReport ("#   - symbol: \"Training Symbol\"");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Fill Types...\" dialog after execution");

	// APIFill_Solid
	fill_atr = {};
	fill_atrdef = {};

	fill_atr.header.typeID = API_FilltypeID;
	fill_atr.header.flags  = APIFill_ScaleWithPlan;
	strcpy (fill_atr.header.name, "Training Solid");
	fill_atr.filltype.subType = APIFill_Solid;

	BNZeroMemory (fill_atr.filltype.bitPat, sizeof (API_Pattern));

	err = ACAPI_Attribute_Create (&fill_atr, &fill_atrdef);
	if (err == APIERR_ATTREXIST) {	// This can not be modified
		err = NoError;
	}
	ACAPI_DisposeAttrDefsHdls (&fill_atrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the solid fill", err);
	} else {
		WriteReport ("The solid fill \"Training Solid\" was created successfully!");
	}

	// APIFill_Empty
	fill_atr = {};
	fill_atr.header.typeID = API_FilltypeID;
	fill_atr.header.flags  = APIFill_ScaleWithPlan;
	strcpy (fill_atr.header.name, "Training Empty");
	fill_atr.filltype.subType = APIFill_Empty;

	err = ACAPI_Attribute_Create (&fill_atr, &fill_atrdef);
	if (err == APIERR_ATTREXIST) {	// This can not be modified
		err = NoError;
	}
	ACAPI_DisposeAttrDefsHdls (&fill_atrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the empty fill", err);
	} else {
		WriteReport ("The empty fill \"Training Empty\" was created successfully!");
	}

	// APIFill_Vector
	fill_atr = {};
	fill_atrdef = {};
	fill_atr.header.typeID = API_FilltypeID;
	fill_atr.header.flags  = APIFill_ScaleWithPlan | APIFill_ForPoly;
	strcpy (fill_atr.header.name, "Training Vector");
	fill_atr.filltype.subType = APIFill_Vector;

	fill_atr.filltype.hXSpac  = 0.1;
	fill_atr.filltype.hYSpac  = 0.1;
	fill_atr.filltype.hAngle  = 0.0;
	fill_atr.filltype.linNumb = 4;
	fill_atr.filltype.arcNumb = 0;

												/*	32103210  */
	fill_atr.filltype.bitPat[0] = 0x80;			/*	*         */
	fill_atr.filltype.bitPat[1] = 0x80;			/*	*         */
	fill_atr.filltype.bitPat[2] = 0x80;			/*	*         */
	fill_atr.filltype.bitPat[3] = 0x8F;			/*	*   ****  */
	fill_atr.filltype.bitPat[4] = 0x88;			/*	*   *     */
	fill_atr.filltype.bitPat[5] = 0x88;			/*	*   *     */
	fill_atr.filltype.bitPat[6] = 0x88;			/*	*   *     */
	fill_atr.filltype.bitPat[7] = 0xF8;			/*	*****     */

	fill_atrdef.fill_lineItems = (API_FillLine **) BMAllocateHandle (fill_atr.filltype.linNumb*sizeof (API_FillLine), ALLOCATE_CLEAR, 0);
	if (fill_atrdef.fill_lineItems == nullptr) {
		WriteReport_Err ("Memory full", 0);
	}
	fill_atrdef.fill_lineLength = (double **) BMAllocateHandle (2*fill_atr.filltype.linNumb*sizeof (double), ALLOCATE_CLEAR, 0);
	if (fill_atrdef.fill_lineLength == nullptr) {
		WriteReport_Err ("Memory full", 0);
	}
	fillLine = *fill_atrdef.fill_lineItems;
	lineLength = *fill_atrdef.fill_lineLength;
	BNZeroMemory (fillLine, fill_atr.filltype.linNumb * sizeof (API_FillLine));
	BNZeroMemory (lineLength, 2 * fill_atr.filltype.linNumb * sizeof (double));

	fillLine[0].lFreq = 2.0;
	fillLine[0].lDir  = 90.0 * DEGRAD;
	fillLine[0].lOffsetLine = 0.0;
	fillLine[0].lOffset.x = 0.0;
	fillLine[0].lOffset.y = 0.0;
	fillLine[0].lPartNumb = 2;
	fillLine[0].lPartOffs = 0;
	lineLength[0] = 2.0;
	lineLength[1] = 0.0;

	fillLine[1].lFreq = 2.0;
	fillLine[1].lDir  = 0.0;
	fillLine[1].lOffsetLine = 0.0;
	fillLine[1].lOffset.x = 0.0;
	fillLine[1].lOffset.y = 0.0;
	fillLine[1].lPartNumb = 2;
	fillLine[1].lPartOffs = 2;
	lineLength[2] = 1.0;
	lineLength[3] = 1.0;

	fillLine[2].lFreq = 2.0;
	fillLine[2].lDir  = 90.0 * DEGRAD;
	fillLine[2].lOffsetLine = 0.0;
	fillLine[2].lOffset.x = 1.0;
	fillLine[2].lOffset.y = 0.0;
	fillLine[2].lPartNumb = 2;
	fillLine[2].lPartOffs = 4;
	lineLength[4] = 1.0;
	lineLength[5] = 1.0;

	fillLine[3].lFreq = 2.0;
	fillLine[3].lDir  = 0.0;
	fillLine[3].lOffsetLine = 0.0;
	fillLine[3].lOffset.x = 1.0;
	fillLine[3].lOffset.y = 1.0;
	fillLine[3].lPartNumb = 2;
	fillLine[3].lPartOffs = 6;
	lineLength[6] = 1.0;
	lineLength[7] = 1.0;


	err = ACAPI_Attribute_Create (&fill_atr, &fill_atrdef);
	if (err == APIERR_ATTREXIST) {
		err = ACAPI_Attribute_Modify (&fill_atr, &fill_atrdef);
		if (err == NoError)
			err = CheckAttributeUserData (&fill_atr.header, fill_atr.filltype.linNumb);
	} else {
		err = AddUserDataToAttribute (&fill_atr.header, fill_atr.filltype.linNumb);
	}
	ACAPI_DisposeAttrDefsHdls (&fill_atrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the vectorial fill", err);
	} else {
		WriteReport ("The vectorial fill \"Training Vector\" was created successfully!");
	}

	// APIFill_Symbol
	fill_atr = {};
	fill_atrdef = {};
	fill_atr.header.typeID = API_FilltypeID;
	fill_atr.header.flags  = APIFill_ScaleWithPlan | APIFill_ForPoly;
	strcpy (fill_atr.header.name, "Training Symbol");
	fill_atr.filltype.subType = APIFill_Symbol;

	fill_atr.filltype.hXSpac  = 1.0;
	fill_atr.filltype.hYSpac  = 1.0;
	fill_atr.filltype.hAngle  = 0.0;
	fill_atr.filltype.linNumb = 0;
	fill_atr.filltype.arcNumb = 0;
	fill_atr.filltype.filNumb = 1;
	fill_atr.filltype.c1.x = 6.0;
	fill_atr.filltype.c1.y = 1.0;
	fill_atr.filltype.c2.x = 0.0;
	fill_atr.filltype.c2.y = 8.0;

												/*	32103210  */
	fill_atr.filltype.bitPat[0] = 0x80;			/*	*         */
	fill_atr.filltype.bitPat[1] = 0x80;			/*	*         */
	fill_atr.filltype.bitPat[2] = 0x80;			/*	*         */
	fill_atr.filltype.bitPat[3] = 0x8F;			/*	*   ****  */
	fill_atr.filltype.bitPat[4] = 0x88;			/*	*   *     */
	fill_atr.filltype.bitPat[5] = 0x88;			/*	*   *     */
	fill_atr.filltype.bitPat[6] = 0x88;			/*	*   *     */
	fill_atr.filltype.bitPat[7] = 0xF8;			/*	*****     */

	fill_atrdef.sfill_Items.sfill_SolidFills = (API_Polygon **) BMAllocateHandle (sizeof (API_Polygon), ALLOCATE_CLEAR, 0);
	if (fill_atrdef.sfill_Items.sfill_SolidFills) {
		poly = *fill_atrdef.sfill_Items.sfill_SolidFills;
		BNZeroMemory (poly, sizeof (API_Polygon));
		poly->nCoords   = 9;
		poly->nSubPolys = 1;
		fill_atrdef.sfill_Items.sfill_FillCoords = (API_Coord **) BMAllocateHandle ((poly->nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
		if (fill_atrdef.sfill_Items.sfill_FillCoords) {
			coords = *fill_atrdef.sfill_Items.sfill_FillCoords;
			BNZeroMemory (coords, poly->nCoords * sizeof (API_Coord));
			coords[0].x = coords[0].y = 0.0;
			coords[1].x = 0.0;	coords[1].y = 0.0;
			coords[2].x = 2.0;	coords[2].y = 0.0;
			coords[3].x = 2.0;	coords[3].y = 2.0;
			coords[4].x = 4.0;	coords[4].y = 2.0;
			coords[5].x = 4.0;	coords[5].y = 6.0;
			coords[6].x = 2.0;	coords[6].y = 6.0;
			coords[7].x = 2.0;	coords[7].y = 4.0;
			coords[8].x = 0.0;	coords[8].y = 4.0;
			coords[9] = coords[1];

			fill_atrdef.sfill_Items.sfill_SubPolys = (Int32 **) BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0);
			if (fill_atrdef.sfill_Items.sfill_SubPolys) {
				subpoly = *fill_atrdef.sfill_Items.sfill_SubPolys;
				BNZeroMemory (subpoly, 2 * sizeof (Int32));
				subpoly[0] = 0;
				subpoly[1] = 9;			/* poly->nCoords - 1 */
			}
		}
	}

	err = ACAPI_Attribute_Create (&fill_atr, &fill_atrdef);
	if (err == APIERR_ATTREXIST) {
		err = ACAPI_Attribute_Modify (&fill_atr, &fill_atrdef);
	}
	ACAPI_DisposeAttrDefsHdls (&fill_atrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the symbol fill", err);
	} else {
		WriteReport ("The symbol fill \"Training Symbol\" was created successfully!");
	}

	WriteReport_End (err);

	return;
}		// Do_CreateFills_Training


// -----------------------------------------------------------------------------
//  List one profile
// -----------------------------------------------------------------------------

static const char* AttributeName (API_AttrTypeID inType, const API_AttributeIndex& inIndex)
{
	API_Attribute attr		  = {};
	static char	  buffer[256] = {0};
	*buffer					  = {};
	attr.header.typeID		  = inType;
	attr.header.index		  = inIndex;
	if (ACAPI_Attribute_Get (&attr) == NoError) {
		CHTruncate (attr.header.name, buffer, sizeof (buffer));
	} else
		buffer[0] = ' ';

	return buffer;
}

static void	ListProfileDescription (const ProfileVectorImage& profileDescription, const GS::HashTable<PVI::ProfileParameterId, GS::UniString>& parameterNameTable)
{
	ConstProfileVectorImageIterator profileDescriptionIt (profileDescription);
	const GS::HashTable<GS::Guid, PVI::HatchVertexId>& associatedHotspotsTable = profileDescription.GetAssociatedHotspotsTable ();

	while (!profileDescriptionIt.IsEOI ()) {
		switch (profileDescriptionIt->item_Typ) {
			case SyHots: {
					const Sy_HotType* pSyHot = ((const Sy_HotType*) profileDescriptionIt);
					WriteReport ("- Hotspot (id: %s)", pSyHot->GetUniqueId ().ToUniString ().ToCStr ().Get ());
					WriteReport ("-- Coordinate: %.3f, %.3f", pSyHot->c.x, pSyHot->c.y);
					if (associatedHotspotsTable.ContainsKey (pSyHot->GetUniqueId ())) {
						const PVI::HatchVertexId associatedVertexId = associatedHotspotsTable[pSyHot->GetUniqueId ()];
						WriteReport ("-- Hotspot is associated to hatch %s's %d. vertex.", associatedVertexId.GetHatchId ().ToUniString ().ToCStr ().Get (), associatedVertexId.GetVertexIndex ());
					}
				}
				break;

			case SyLine: {
					const Sy_LinType* pSyLine = static_cast <const Sy_LinType*> (profileDescriptionIt);
					WriteReport ("- Line");
					WriteReport ("-- Layer: %d; SpecForProfile: %d", pSyLine->sy_fragmentIdx, pSyLine->specForProfile);
					WriteReport ("-- From: %.3f, %.3f", pSyLine->begC.x, pSyLine->begC.y);
					WriteReport ("-- To  : %.3f, %.3f", pSyLine->endC.x, pSyLine->endC.y);
				}
				break;

			case SyHatch: {
					const HatchObject& syHatch = profileDescriptionIt;
					const ProfileItem& profileItemInfo = syHatch.GetProfileItem();

					WriteReport ("- Hatch (id: %s, componentIndex: %s)",
								 syHatch.GetUniqueId ().ToUniString ().ToCStr ().Get (),
								 profileDescription.GetStructureComponentId (syHatch.GetUniqueId ()).ValueStringForDump ().ToCStr ().Get ());
					Int32 uiPriority = 0;
					{
						API_BuildingMaterialType	buildMat;
						BNZeroMemory (&buildMat, sizeof (API_BuildingMaterialType));
						buildMat.head.typeID = API_BuildingMaterialID;
						buildMat.head.index = AC2API_AttrIndex (syHatch.GetBuildMatIdx ());
						ACAPI_Attribute_Get ((API_Attribute*) &buildMat);
						ACAPI_Element_Elem2UIPriority (&buildMat.connPriority, &uiPriority);
					}
					WriteReport ("-- Priority: %d%s", uiPriority, profileItemInfo.IsCore () ? ", Core component" : "");
					WriteReport ("-- Coordinates: %ld", syHatch.GetCoords ().GetSize ());
					UInt32 index = 0;
					for (Coord coord : syHatch.GetCoords ()) {
						const bool isContourEnd = index != 0 && syHatch.GetSubPolyEnds ().Contains (index);
						WriteReport ("--- Coord #%-2d (%5.2f, %5.2f)%s", index++, coord.x, coord.y, isContourEnd ? " - Contour end" : "");
					}

					const std::vector<ProfileEdgeData>& profileEdgeData = syHatch.GetProfileEdgeData ();
					DBASSERT (syHatch.GetCoords ().GetSize () == profileEdgeData.size ());
					// Don't skip first elem
					for (auto edgeIt = profileEdgeData.begin(); edgeIt != profileEdgeData.end(); ++edgeIt) {
						WriteReport ("--- Edge data #%-2d surface = %32s (%3s) pen = (%3ld)", std::distance(profileEdgeData.begin (), edgeIt), AttributeName (API_MaterialID, AC2API_AttrIndex (edgeIt->GetMaterial ())),
									 edgeIt->GetMaterial ().ToUniString ().ToCStr ().Get (), edgeIt->GetPen ());
					}
				}
				break;

			case SySpline: {
					const Sy_SplineType* pSySpline = static_cast <const Sy_SplineType*> (profileDescriptionIt);
					const API_Coord* pSyCoords = (const API_Coord*) ((const char*) pSySpline + pSySpline->coorOff);
					const Geometry::DirType* pSyDirs = (const Geometry::DirType*) ((const char*) pSySpline + pSySpline->dirsOff);
					for (GSIndex ii = 0; ii < pSySpline->nCoords; ++ii) {
						WriteReport ("--- Coord #%-2d (%5.2f, %5.2f)", ii, pSyCoords[ii].x, pSyCoords[ii].y);
						WriteReport ("--- Dirs  #%-2d (%5.2f, %5.2f, %5.2f)", ii, pSyDirs[ii].lenPrev, pSyDirs[ii].lenNext, pSyDirs[ii].dirAng);
					}
				}
				break;

			case SyArc:	{
					const Sy_ArcType* pSyArc = static_cast <const Sy_ArcType*> (profileDescriptionIt);
					WriteReport ("- Arc");
					WriteReport ("-- Layer		: %d", pSyArc->sy_fragmentIdx);
					WriteReport ("-- Origo		: %.3f, %.3f", pSyArc->origC.x, pSyArc->origC.y);
					WriteReport ("-- BegCoord	: %.3f, %.3f", pSyArc->begC.x, pSyArc->begC.y);
					WriteReport ("-- EndCoord	: %.3f, %.3f", pSyArc->endC.x, pSyArc->endC.y);
					WriteReport ("-- Radius		: %.3f", pSyArc->r);
				}
				break;

			case SyPolyLine:
			case SyText:
			case SyPicture:
			case SyPixMap:
			case SyPointCloud:
				break;
		}
		++profileDescriptionIt;
	}

	for (auto itPar = profileDescription.GetParameterTable ().EnumeratePairs (); itPar != nullptr; ++itPar) {
		const PVI::ProfileParameterId& parameterId = itPar->key;
		const GS::HashSet<PVI::ProfileParameterSetupId>& parDefIDs = itPar->value;
		const GS::UniString parameterName = parameterNameTable[parameterId];

		for (auto it = parDefIDs.Enumerate (); it != nullptr; ++it) {
			const PVI::ProfileParameterSetupId& parameterDefId = *it;
			const PVI::ProfileVectorImageParameter& profileParameter = profileDescription.GetParameterDef (parameterDefId);

			WriteReport ("- ProfileParameter (id: %s, name: %s)", parameterId.ToUniString ().ToCStr ().Get (), parameterName.ToCStr ().Get ());
			if (profileParameter.GetType () == PVI::ProfileVectorImageParameter::ParameterType::EdgeOffset) {
				const PVI::EdgeOffsetParameter& edgeOffsetParameter = profileParameter.GetEdgeOffsetParameter ();

				const PVI::ProfileDimControlToolId& dimToolID = edgeOffsetParameter.GetDimControlToolID ();
				const GS::Optional<PVI::DimensionControlTool> dimTool = profileDescription.GetDimensionControlTool (dimToolID);
				DBASSERT (dimTool.HasValue ());
				const PVI::ProfileAnchorId& begAnchorID = dimTool->GetBegAnchorID ();
				const PVI::ProfileAnchorId& endAnchorID = dimTool->GetEndAnchorID ();
				const GS::Optional<PVI::Anchor>	begAnchor = profileDescription.GetAnchor (begAnchorID);
				const GS::Optional<PVI::Anchor>	endAnchor = profileDescription.GetAnchor (endAnchorID);
				DBASSERT (begAnchor.HasValue ());
				DBASSERT (endAnchor.HasValue ());

				//TODO B530M2 AKARKINEK: M1 agon a begAnchor a szokasos vertex vagy fix pontos, mig az endAnchor a main edge... M2 agon ez megvaltozik!
				PVI::HatchEdgeId mainEdge = endAnchor->GetAssociatedEdgeId ();
				WriteReport ("-- MainEdge: (HatchId: %s, EdgeIndex: %d, Direction: %s)", mainEdge.GetHatchId ().ToUniString ().ToCStr ().Get (),
							 mainEdge.GetEdgeIndex (),
							 ProfileVectorImageOperations::ResolveEdgeDirFlag (profileDescription, mainEdge) == PVI::AssociatedEdge::DirectionFlag::Left ? "Left" : "Right");
				for (const PVI::AssociatedEdge& associatedEdge : edgeOffsetParameter.GetAssociatedEdges ()) {
					WriteReport ("-- Further edges to offset: (HatchId: %s, EdgeIndex: %d, Direction: %s)", associatedEdge.GetHatchId ().ToUniString ().ToCStr ().Get (),
								 associatedEdge.GetEdgeIndex (),
								 associatedEdge.GetDirectionFlag () == PVI::AssociatedEdge::DirectionFlag::Left //TODO B530M2!!!
								 ? "Left" : "Right");
				}
				const PVI::Anchor& anchor = begAnchor.Get (); //TODO B530M2!!!

				if (anchor.GetAnchorType () == PVI::Anchor::AnchorType::VertexAssociative) {
					WriteReport ("-- Anchor is on vertex. (HatchId: %s, VertexIndex: %d", anchor.GetAssociatedVertexId ().GetHatchId ().ToUniString ().ToCStr ().Get (),
								 anchor.GetAssociatedVertexId ().GetVertexIndex ());
				} else if (anchor.GetAnchorType () == PVI::Anchor::AnchorType::FixedToStretchCanvas) {
					WriteReport ("-- Anchor is fixed. (%.3f, %.3f)", anchor.GetFixAnchorPosition ().GetX (), anchor.GetFixAnchorPosition ().GetY ());
				}
			} else {
				// other types are not supported.
			}
		}
	}

	{
		const PVI::StretchData& stretchData = profileDescription.GetStretchData ();
		if (stretchData.HasHorizontalZone ()) {
			WriteReport ("- Horizontal Stretch Zone: (%.3f, %.3f)", stretchData.GetHorizontalZoneMin (), stretchData.GetHorizontalZoneMax ());
		}
		if (stretchData.HasVerticalZone ()) {
			WriteReport ("- Vertical Stretch Zone: (%.3f, %.3f)", stretchData.GetVerticalZoneMin (), stretchData.GetVerticalZoneMax ());
		}
	}

	{
		const PVI::EdgeOverrideData& edgeOverrideData = profileDescription.GetEdgeOverrideData ();
		if (edgeOverrideData.GetOverrideSectionLines ()) {
			WriteReport ("- Inner section lines overridden: LineType to %s, linePen to %d", AttributeName (API_LinetypeID, AC2API_AttrIndex (edgeOverrideData.GetInnerLineType ())), edgeOverrideData.GetInnerPen ());
			WriteReport ("- Outer section lines overridden: LineType to %s, linePen to %d", AttributeName (API_LinetypeID, AC2API_AttrIndex (edgeOverrideData.GetOuterLineType ())), edgeOverrideData.GetOuterPen ());
		}
	}

}		// ListProfileDescription


// -----------------------------------------------------------------------------
//  List all details of all profiles
// -----------------------------------------------------------------------------

static void		Do_ListAllProfileDetails (void)
{
	WriteReport ("Detailed listing of Profiles");

	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_ProfileID, [&] (API_Attribute& attrib) {
		API_AttributeDefExt	defs;
		err = ACAPI_Attribute_GetDefExt (API_ProfileID, attrib.header.index, &defs);
		if (err == APIERR_BADID) {
			defs = {};
			err = NoError;
		}
		if (err == NoError) {
			WriteReport ("\n\n****Profile name: %s", attrib.header.name);
			ListProfileDescription (*defs.profile_vectorImageItems, *defs.profile_vectorImageParameterNames);
		}
		ACAPI_DisposeAttrDefsHdlsExt (&defs);
	});

	WriteReport_End (err);

	return;
}		// Do_ListAllProfileDetails


// -----------------------------------------------------------------------------
//  List all details of all Operation Profiles
// -----------------------------------------------------------------------------

static void		Do_ListAllOpProfileDetails (void)
{
	WriteReport ("Detailed listing of OperationProfiles");

	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_OperationProfileID, [&] (API_Attribute& attrib) {
		WriteReport ("\n\n****Profile name: %s", attrib.header.name);
		WriteReport ("\tOccupancyType: %d", attrib.operationProfile.occupancyType);
		WriteReport ("\tHot water: %.3f", attrib.operationProfile.hotWaterLoad);
		WriteReport ("\tHuman heat gain: %.3f", attrib.operationProfile.humanHeatGain);
		WriteReport ("\tHumidity: %.3f", attrib.operationProfile.humidity);
		API_AttributeDefExt	defs;
		err = ACAPI_Attribute_GetDefExt (API_OperationProfileID, attrib.header.index, &defs);
		if (err == APIERR_BADID) {
			defs = {};
			err = NoError;
		}
		if (err == NoError) {
		}
		Int32 maxHours = BMGetHandleSize ((GSHandle) defs.op_hourlyProfiles) / sizeof (API_HourlyProfile);

		for (Int32 p = 0; p < maxHours; p += 6) {
			WriteReport ("\n\n\t****DailyProfile name: %s for hour: %d", (*defs.op_hourlyProfiles)[p].name, p);
			WriteReport ("\t\tminTemp: %.3f ", (*defs.op_hourlyProfiles)[p].minTemp);
			WriteReport ("\t\tmaxTemp: %.3f ", (*defs.op_hourlyProfiles)[p].maxTemp);
			WriteReport ("\t\toccupancyCount: %.3f ", (*defs.op_hourlyProfiles)[p].occupancyCount);
			WriteReport ("\t\tlightGain: %.3f ", (*defs.op_hourlyProfiles)[p].lightGain);
			WriteReport ("\t\tequipmentGain: %.3f", (*defs.op_hourlyProfiles)[p].equipmentGain);
		}
		ACAPI_DisposeAttrDefsHdlsExt (&defs);
	});

	WriteReport_End (err);
}		// Do_ListAllOpProfileDetails


// -----------------------------------------------------------------------------
//  List all details of all Building Materials
// -----------------------------------------------------------------------------

static void		Do_ListAllBuildingMaterials (void)
{
	WriteReport ("Detailed listing of Building Materials");

	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_BuildingMaterialID, [&] (API_Attribute& attrib) {
		GS::UniString cutFillOrientationString;
		if (attrib.buildingMaterial.cutFillOrientation == APIFillOrientation_ProjectOrigin)
			cutFillOrientationString = "ProjectOrigin";
		else if (attrib.buildingMaterial.cutFillOrientation == APIFillOrientation_ElementOrigin)
			cutFillOrientationString = "ElementOrigin";
		else if (attrib.buildingMaterial.cutFillOrientation == APIFillOrientation_FitToSkin)
			cutFillOrientationString = "FitToSkin";

		WriteReport ("\n\n****Building Material #%d name: \"%s\"", attrib.header.index, attrib.header.name);
		WriteReport ("\tConnection priority: %d", attrib.buildingMaterial.connPriority);
		WriteReport ("\tCut fill index: %d", attrib.buildingMaterial.cutFill);
		WriteReport ("\tCut fill pen index: %d", attrib.buildingMaterial.cutFillPen);
		WriteReport ("\tCut fill background pen index: %d", attrib.buildingMaterial.cutFillBackgroundPen);
		WriteReport ("\tSurface index: %d", attrib.buildingMaterial.cutMaterial);
		WriteReport ("\tCut fill orientation: %s", cutFillOrientationString.ToCStr ().Get ());
		WriteReport ("\tThermal conductivity: %.3f", attrib.buildingMaterial.thermalConductivity);
		WriteReport ("\tDensity: %.3f", attrib.buildingMaterial.density);
		WriteReport ("\tHeat capacity: %.3f", attrib.buildingMaterial.heatCapacity);
	});

	WriteReport_End (err);
}		// Do_ListAllBuildingMaterials


// -----------------------------------------------------------------------------
//  Creates the internal structure of a profile attribute
// -----------------------------------------------------------------------------

static void	BuildProfileDescription (ProfileVectorImage* image)
{
	const short		nCoords = 5;
	Coord			coords[nCoords + 1] = {Coord(0.0, 0.0), Coord(0.0, 0.0), Coord(1.0, 0.0), Coord(1.0, 1.0), Coord(0.0, 1.0), Coord(0.0, 0.0)};
	Int32			size = sizeof (ProfileItem) + (nCoords + 1) * sizeof (ProfileEdgeData);
	GSHandle		addInfo = BMAllocateHandle (size, ALLOCATE_CLEAR, 0);
	UInt32			boends[2] = { 0, nCoords };

	if (!DBERROR (addInfo == nullptr)) {
		BNZeroMemory (*addInfo, size);

		ProfileItem*	profileItem = reinterpret_cast<ProfileItem*>(*addInfo);
		profileItem->obsoletePriorityValue = 0;		// not used
		profileItem->profileItemVersion = ProfileItemVersion;
		profileItem->SetCutEndLinePen (5);
		profileItem->SetCutEndLineType (ADB::CreateAttributeIndex (5));
		profileItem->SetVisibleCutEndLines (true);
		profileItem->SetCore (true);

		ProfileEdgeData*	profileEdgeData = reinterpret_cast<ProfileEdgeData*>(reinterpret_cast<char*>(*addInfo) + sizeof (ProfileItem));

		profileEdgeData[0].SetPen (0);
		profileEdgeData[0].SetLineType (ADB::InvalidAttributeIndex);
		profileEdgeData[0].SetMaterial (ADB::InvalidAttributeIndex);
		profileEdgeData[0].SetFlags (0);

		for (short i = 1; i <= nCoords; i++) {
			profileEdgeData[i].SetPen (1);
			profileEdgeData[i].SetLineType (ADB::SolidLineAttributeIndex);
			profileEdgeData[i].SetMaterial (ADB::CreateAttributeIndex (i));		// set different material attribute for each edges
			profileEdgeData[i].SetFlags (ProfileEdgeData::IsVisibleLineFlag);
		}
	}

	GX::Pattern::HatchTran	hatchTrafo;
	hatchTrafo.SetGlobal ();

	try {
		VBAttr::ExtendedPen extendedPen (4);
		image->AddHatchObsolete (true,								// hatch contour visible
							  VBAttr::ExtendedPen (5),			// drawing pen index of the hatch contour
							  ADB::CreateAttributeIndex (5),								// line type attribute index of the hatch contour
							  ADB::CreateAttributeIndex (7),								// building material
							  ADB::CreateAttributeIndex (7),								// fill attribute index of the hatch
							  VBAttr::OverriddenExtendedPen (VBAttr::UseThisAttribute, extendedPen),	// override pen index of the fill (false means no override, pen coming from building material)
							  VBAttr::OverriddenPen (VBAttr::UseThisAttribute, 12),	// override pen index of the fill background (zero means transparent; false means no override, pen coming from building material)
							  hatchTrafo,
							  1,								// number of the polygon contours
							  boends,							// ending vertex indices of contours
							  nCoords,							// number of vertices of the polygon
							  coords,							// vertex coordinates (array of pairs of double values)
							  nullptr,								// view angles of the edges, applicable if the polygon contains curved edges
							  0,								// fill category: 0 - DraftingFills, 1 - CutFills, 2 - CoverFills
							  addInfo, nullptr);							// profile related additional parameters
	} catch (const GS::Exception&) {}

	if (addInfo != nullptr)
		BMKillHandle (&addInfo);

}		// BuildProfileDescription


// -----------------------------------------------------------------------------
// Create a profile attribute
//		- Symbol
// -----------------------------------------------------------------------------

static void		Do_CreateProfile (void)
{
	API_Attribute		profile_attr;
	API_AttributeDefExt	profile_attrdef;
	GSErrCode			err = NoError;

	WriteReport ("# Create a profile attribute:");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Profiles...\" dialog after execution");

	BNZeroMemory ((GSPtr) &profile_attr, sizeof (API_Attribute));
	BNZeroMemory ((GSPtr) &profile_attrdef, sizeof (API_AttributeDefExt));

	profile_attr.header.typeID = API_ProfileID;
	profile_attr.header.flags  = 0;
	strcpy (profile_attr.header.name, "Profile from API");
	profile_attr.profile.wallType = true;
	profile_attr.profile.beamType = false;
	profile_attr.profile.coluType = true;

	profile_attrdef.profile_vectorImageItems = new ProfileVectorImage ();
	BuildProfileDescription (profile_attrdef.profile_vectorImageItems);

	err = ACAPI_Attribute_CreateExt (&profile_attr, &profile_attrdef);
	if (err == APIERR_ATTREXIST) {
		err = ACAPI_Attribute_ModifyExt (&profile_attr, &profile_attrdef);
	}
	ACAPI_DisposeAttrDefsHdlsExt (&profile_attrdef);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the profile", err);
	} else {
		WriteReport ("Profile \"Profile from API\" was created successfully.");
	}

	WriteReport_End (err);

	return;
}		// Do_CreateProfile


// -----------------------------------------------------------------------------
// Create a pen table attribute
// -----------------------------------------------------------------------------

static void		Do_CreatePenTable (void)
{
	API_Attribute		attrib;
	API_AttributeDefExt	attrdef;
	GSErrCode			err = NoError;

	WriteReport ("# Create a pen table attribute:");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Pens and Colors...\" dialog after execution");

	BNZeroMemory ((GSPtr) &attrib, sizeof (API_Attribute));
	BNZeroMemory ((GSPtr) &attrdef, sizeof (API_AttributeDefExt));

	attrib.header.typeID = API_PenTableID;
	attrib.header.flags  = 0;
	strcpy (attrib.header.name, "Gray pen table from API");

	short numOfPens = 255;

	attrdef.penTable_Items = new GS::Array<API_Pen> (numOfPens);
	if (attrdef.penTable_Items == nullptr) {
		WriteReport ("!!! Not enough memory for pen table pens");
		return;
	}

	for (short ii = 1; ii <= numOfPens; ii++) {
		API_Pen pen = {};
		pen.index		= ii;
		pen.rgb.f_red   = ii / 255.0;
		pen.rgb.f_green = ii / 255.0;
		pen.rgb.f_blue  = ii / 255.0;
		pen.width       = 0.0;
		attrdef.penTable_Items->Push (pen);
	}

	err = ACAPI_Attribute_CreateExt (&attrib, &attrdef);
	if (err == APIERR_ATTREXIST) {
		err = ACAPI_Attribute_ModifyExt (&attrib, &attrdef);
	}
	ACAPI_DisposeAttrDefsHdlsExt (&attrdef);

	if (err != NoError) {
		WriteReport_Err ("Error while creating the pen table", err);
	} else {
		WriteReport ("Pen table \"Gray pen table from API\" was created successfully.");
	}

	WriteReport_End (err);

	return;
}		// Do_CreatePenTable


#ifndef NO_MEP_FEATURES
// -----------------------------------------------------------------------------
// Create a MEP System attribute
// -----------------------------------------------------------------------------

static void		Do_CreateMEPSystem (void)
{
	API_Attribute		mepSystem_atr;
	GSErrCode			err;

	WriteReport ("# Create a new MEPSystem attribute:");
	WriteReport ("# Check the \"MEP Systems...\" dialog after execution");

	BNZeroMemory (&mepSystem_atr, sizeof (API_Attribute));

	mepSystem_atr.header.typeID = API_MEPSystemID;
	mepSystem_atr.header.flags  = 0;
	strcpy (mepSystem_atr.header.name, "Test System");

	mepSystem_atr.mepSystem.isForDuctwork = true;
	mepSystem_atr.mepSystem.isForPipework = false;
	mepSystem_atr.mepSystem.isForCabling = true;

	mepSystem_atr.mepSystem.contourPen = 5;
	mepSystem_atr.mepSystem.fillPen = 4;
	mepSystem_atr.mepSystem.fillBgPen = -1;
	mepSystem_atr.mepSystem.fillInd = ACAPI_CreateAttributeIndex (1);
	mepSystem_atr.mepSystem.centerLTypeInd = ACAPI_CreateAttributeIndex (4);
	mepSystem_atr.mepSystem.centerLinePen = 6;
	mepSystem_atr.mepSystem.materialInd = ACAPI_CreateAttributeIndex (55);
	mepSystem_atr.mepSystem.insulationMatInd = ACAPI_CreateAttributeIndex (45);

	err = ACAPI_Attribute_Create (&mepSystem_atr, nullptr);
	if (err == APIERR_ATTREXIST) {	// This can not be modified
		err = NoError;
	}

	if (err != NoError) {
		WriteReport_Err ("Error while creating the new MEPSystem", err);
	} else {
		WriteReport ("The MEPSystem \"Test System\" was created successfully!");
	}

	WriteReport_End (err);

	return;
}		// Do_CreateMEPSystem
#endif


// -----------------------------------------------------------------------------
// Create a Building Material attribute
// -----------------------------------------------------------------------------
static void		Do_CreateBuildingMaterial (void)
{
	API_Attribute	attrib = {};
	GS::UniString	attribName ("Test Building Material");

	attrib.header.typeID = API_BuildingMaterialID;
	attrib.header.uniStringNamePtr = &attribName;

	Int32 uiPriority = 8;
	Int32 elemPriority = 0;
	ACAPI_Element_UI2ElemPriority (&uiPriority, &elemPriority);
	attrib.buildingMaterial.connPriority = elemPriority;
	attrib.buildingMaterial.cutFill = ACAPI_CreateAttributeIndex (1);
	attrib.buildingMaterial.cutFillPen = 4;
	attrib.buildingMaterial.cutFillBackgroundPen = 0;
	attrib.buildingMaterial.cutMaterial = ACAPI_CreateAttributeIndex (75);
	attrib.buildingMaterial.cutFillOrientation = APIFillOrientation_ProjectOrigin;
	attrib.buildingMaterial.thermalConductivity = 0.4;
	attrib.buildingMaterial.density = 1500.0;
	attrib.buildingMaterial.heatCapacity = 700.0;
	attrib.buildingMaterial.showUncutLines = true;

	GSErrCode err = ACAPI_Attribute_Create (&attrib, nullptr);
	if (err != NoError) {
		WriteReport_Err ("Error while creating the new Building Material", err);
	} else {
		WriteReport ("\"%s\" attribute was created successfully!", attribName.ToCStr ().Get ());

		err = AddUserDataToAttribute (&attrib.header, attrib.buildingMaterial.heatCapacity);
	}

	WriteReport_End (err);

	return;
}		// Do_CreateBuildingMaterial


// -----------------------------------------------------------------------------
// Modify a Building Material attribute's heat capacity
// -----------------------------------------------------------------------------

static void		Do_ChangeBuildingMaterial (void)
{
	API_Attribute	attrib = {};
	GS::UniString	attribName ("Test Building Material");

	attrib.header.typeID = API_BuildingMaterialID;
	attrib.header.uniStringNamePtr = &attribName;

	if (ACAPI_Attribute_Get (&attrib) != NoError) {
		WriteReport ("Could not find the \"%s\" attribute!", attribName.ToCStr ().Get ());
		return;
	}

	attrib.buildingMaterial.cutFillOrientation = APIFillOrientation_ElementOrigin;
	attrib.buildingMaterial.heatCapacity = 1000.0;

	GSErrCode err = ACAPI_Attribute_Modify (&attrib, nullptr);
	if (err != NoError) {
		WriteReport_Err ("Error while modifying the Building Material", err);
	} else {
		WriteReport ("The \"%s\" attribute was modified successfully!", attribName.ToCStr ().Get ());
	}

	double valueToCheck = 700.0;
	err = CheckAttributeUserData (&attrib.header, valueToCheck);

	WriteReport_End (err);

	return;
}		// Do_ChangeBuildingMaterial


// -----------------------------------------------------------------------------
// Copy a Zone Category attribute
// -----------------------------------------------------------------------------
static void		Do_CopyZoneCategory (void)
{
	API_Attribute		attrib = {};
	API_AttributeDef	attrDef = {};

	GS::UniString		origAttribName;
	GS::UniString		attribName ("Test Zone Category");
	GS::UniString		catCode ("99");

	const GS::Array<GS::Guid> zoneCatGuids = GetSomeAttributes (API_ZoneCatID, 1);
	if (zoneCatGuids.GetSize () == 0) {
		WriteReport ("No zone categories!");
		return;
	}

	attrib.header.typeID = API_ZoneCatID;
	attrib.header.index  = APIInvalidAttributeIndex;		// tells the API to use the GUID
	attrib.header.guid   = GSGuid2APIGuid (zoneCatGuids[0]);
	attrib.header.uniStringNamePtr = &origAttribName;

	GSErrCode err = ACAPI_Attribute_Get (&attrib);
	if (err != NoError) {
		WriteReport ("Could not get the chosen zone category!");
		return;
	}

	err = ACAPI_Attribute_GetDef (attrib.header.typeID, attrib.header.index, &attrDef);
	if (err != NoError) {
		WriteReport ("Can't get the zone category's attribute definition!");
		return;
	}

	// set a new name for the copy
	attrib.header.name[0] = 0;
	attrib.header.uniStringNamePtr = &attribName;
	GS::ucsncpy (attrib.zoneCat.catCode, catCode.ToUStr (), sizeof (attrib.zoneCat.catCode) / sizeof (attrib.zoneCat.catCode[0]));

	err = ACAPI_Attribute_Create (&attrib, &attrDef);
	if (err != NoError) {
		WriteReport_Err ("Error while copying the Zone Category", err);
	} else {
		WriteReport ("The \"%s\" zone category was copied into \"%s\" successfully (R: %3.0lf)!",
		             origAttribName.ToCStr ().Get (),
		             attribName.ToCStr ().Get (),
		             255.0*attrib.zoneCat.rgb.f_red);

		double valueToSet = 255.0*attrib.zoneCat.rgb.f_red;
		err = AddUserDataToAttribute (&attrib.header, valueToSet);
	}

	WriteReport_End (err);

	ACAPI_DisposeAttrDefsHdls (&attrDef);
	return;
}		// Do_CopyZoneCategory


// -----------------------------------------------------------------------------
// Modify the Zone Category attribute's rgb color
// -----------------------------------------------------------------------------
static void		Do_ChangeZoneCategory (void)
{
	API_Attribute		attrib = {};
	API_AttributeDef	attrDef = {};

	GS::UniString		attribName ("Test Zone Category");

	attrib.header.typeID = API_ZoneCatID;
	attrib.header.uniStringNamePtr = &attribName;

	GSErrCode err = ACAPI_Attribute_Search (&attrib.header);
	if (err != NoError) {
		WriteReport ("Can't find the \"%s\" attribute!", attribName.ToCStr ().Get ());
		return;
	}

	err = ACAPI_Attribute_Get (&attrib);
	if (err != NoError) {
		WriteReport ("Can't get the \"%s\" attribute!", attribName.ToCStr ().Get ());
		return;
	}

	err = ACAPI_Attribute_GetDef (attrib.header.typeID, attrib.header.index, &attrDef);
	if (err != NoError) {
		WriteReport ("Can't get the zone category's attribute definition!");
		return;
	}

	// now modify the color
	attrib.zoneCat.rgb.f_red   = 1.0 - attrib.zoneCat.rgb.f_red;
	attrib.zoneCat.rgb.f_green = 1.0 - attrib.zoneCat.rgb.f_green;
	attrib.zoneCat.rgb.f_blue  = 1.0 - attrib.zoneCat.rgb.f_blue;

	err = ACAPI_Attribute_Modify (&attrib, &attrDef);
	if (err != NoError) {
		WriteReport_Err ("Error while modifying the new Zone Category", err);
	} else {
		WriteReport ("The \"%s\" attribute was modified successfully (R: %3.0lf)!",
		             attribName.ToCStr ().Get (),
		             255.0*attrib.zoneCat.rgb.f_red);

		double valueToCheck = 153.0;
		err = CheckAttributeUserData (&attrib.header, valueToCheck);
	}

	WriteReport_End (err);

	ACAPI_DisposeAttrDefsHdls (&attrDef);
	return;
}		// Do_ChangeZoneCategory


// -----------------------------------------------------------------------------
// Hide the layer of lines : "2D Drafting - General"
// -----------------------------------------------------------------------------

static void		Do_HideLayerOfLines (void)
{
	API_Attribute	attrib;
	GSErrCode		err;

	GS::UniString	attribName ("2D Drafting - General");

	WriteReport ("# Hide the layer: \"%s\"", attribName.ToCStr ().Get ());
	WriteReport ("#   - should work in each Teamwork state");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;
	attrib.header.uniStringNamePtr = &attribName;

	err = ACAPI_Attribute_Search (&attrib.header);		/* search by name: index returned in header */
	if (err != NoError) {
		WriteReport ("Layer \"%s\" was not found: %s", attribName.ToCStr ().Get (), ErrID_To_Name (err));
		return;
	}
	err = ACAPI_Attribute_Get (&attrib);
	if (err != NoError) {
		WriteReport_Err ("Unable to get the layer definition", err);
		return;
	}

	attrib.layer.head.flags |= APILay_Hidden;
	err = ACAPI_Attribute_Modify (&attrib, nullptr);
	if (err != NoError) {
		WriteReport ("Unable to modify the layer \"%s\": %s", attribName.ToCStr ().Get (), ErrID_To_Name (err));
		return;
	}

	WriteReport_End (err);

	return;
}		// Do_HideLayerOfLines


// -----------------------------------------------------------------------------
// Modify the linetype: Wave
// -----------------------------------------------------------------------------

static void		Do_ChangeLinetype_Wave (void)
{
	API_Attribute		attrib;
	API_AttributeDef	def;
	GSErrCode			err;

	WriteReport ("# Change the linetype: \"Wave\"");
	WriteReport ("#   - add a centerline to the linetype");
	WriteReport ("# Check the \"Line Types...\" dialog after execution");

	attrib = {};
	BNZeroMemory (&def, sizeof (API_AttributeDef));

	attrib.header.guid = GSGuid2APIGuid (GS::Guid ("35DE8210-A51E-43AE-9837-8CF3A01E8F63"));	// GUID of "Wave" linetype attribute
	err = ACAPI_Attribute_Search (&attrib.header);			/* search by guid: type and index returned */
	if (err != NoError) {
		GS::UniString uniStringName ("Wave");
		attrib.header.uniStringNamePtr = &uniStringName;
		attrib.header.typeID = API_LinetypeID;
		attrib.header.guid = APINULLGuid;
		err = ACAPI_Attribute_Search (&attrib.header);		/* search by name and type: index returned */
		attrib.header.uniStringNamePtr = nullptr;
	}
	if (err != NoError) {
		WriteReport_Err ("Linetype \"Wave\" was not found", err);
		return;
	}
	err = ACAPI_Attribute_Get (&attrib);
	if (err == NoError)
		err = ACAPI_Attribute_GetDef (attrib.header.typeID, attrib.header.index, &def);
	if (err != NoError) {
		WriteReport_Err ("Unable to get the linetype definition", err);
		return;
	}

	def.ltype_lineItems = (API_LineItems **) BMReallocHandle ((GSHandle) def.ltype_lineItems,
																 (attrib.linetype.nItems + 1) * sizeof (API_LineItems), REALLOC_CLEAR, 0);
	if (def.ltype_lineItems == nullptr)
		return;

	/* add a centerline to the linetype */
	BNZeroMemory (&(*def.ltype_lineItems) [attrib.linetype.nItems], sizeof (API_LineItems));
	(*def.ltype_lineItems) [attrib.linetype.nItems].itemType   = APILine_CenterLineItemType;
	(*def.ltype_lineItems) [attrib.linetype.nItems].itemLength = attrib.linetype.period;
	attrib.linetype.nItems ++;

	err = ACAPI_Attribute_Modify (&attrib, &def);
	ACAPI_DisposeAttrDefsHdls (&def);
	if (err != NoError) {
		WriteReport_Err ("Unable to modify the linetype \"Wave\"", err);
		return;
	}

	WriteReport_End (err);

	return;
}		// Do_ChangeLinetype_Wave


#ifdef __APPLE__
#pragma mark -
#endif

// =============================================================================
//
// TeamWork environment
//
// =============================================================================

// -----------------------------------------------------------------------------
// Create an attribute in TeamWork project
// -----------------------------------------------------------------------------

static void		Do_CreateAttributes_TW (void)
{
	API_AttrTypeID	typeID;
	GSErrCode		err = NoError;

	WriteReport ("# Create an attribute in TeamWork project: \"My TW Attribute\"");
	WriteReport ("# Copy the first attribute instance of each type");
	WriteReport ("#   - FullAccess:  should work on each");
	WriteReport ("#   - ViewOnly:    refused");
	WriteReport ("#   - Client:      the ClientBit should be set");
	WriteReport ("#   				 for layers: the reservation must be extended");
	WriteReport ("# Check all attribute setting dialogs after execution");
	WriteReport ("# Try it in all Teamwork modes");

	for (typeID = API_FirstAttributeID; typeID <= API_LastAttributeID; ((Int32 &) typeID)++) {
		err = ACAPI_Attribute_EnumerateAttributesByType (typeID, [&] (API_Attribute& attrib) {
			API_AttributeDefExt	defs;
			err = ACAPI_Attribute_GetDefExt (typeID, attrib.header.index, &defs);
			if (err == APIERR_BADID) {
				err = NoError;
			}
			if (err == NoError) {
				strcpy (attrib.header.name, "\"My TW Attribute\"");
				err = ACAPI_Attribute_CreateExt (&attrib, &defs);
				ACAPI_DisposeAttrDefsHdlsExt (&defs);
				DisposeAttribute (&attrib);
				return;
			}
			ACAPI_DisposeAttrDefsHdlsExt (&defs);
			DisposeAttribute (&attrib);
		});
	}

	WriteReport_End (err);

	return;
}		// Do_CreateAttributes_TW


// -----------------------------------------------------------------------------
// Rename all attributes in a TeamWork project
// -----------------------------------------------------------------------------

static void		Do_RenameMyAttributes_TW (void)
{
	GSErrCode	err;

	WriteReport ("# Rename all attributes in a TeamWork project");
	WriteReport ("#   - ViewOnly:    disabled");
	WriteReport ("#   - Client:      should work only on: \"My TW Attribute\"");
	WriteReport ("#   - FullAccess:  should work on each");
	WriteReport ("# Check all attribute setting dialogs after execution");
	WriteReport ("# Execute it in each TeamWork mode");

	err = Do_RenameAttributes (false);

	WriteReport_End (err);

	return;
}		// Do_RenameMyAttributes_TW


// -----------------------------------------------------------------------------
// Delete all attributes in TeamWork mode
// -----------------------------------------------------------------------------

static void		Do_DeleteMyAttributes_TW (void)
{
	GSErrCode	err;

	WriteReport ("# Delete all attributes in TeamWork mode (each type)");
	WriteReport ("#   - ViewOnly:    disabled");
	WriteReport ("#   - Client:      should work only on: \"My TW Attribute\"");
	WriteReport ("#   - FullAccess:  should work on each");
	WriteReport ("# Check all attribute setting dialogs after execution");
	WriteReport ("# Execute it in each TeamWork mode");

	err = Do_DeleteAttributes (false);

	WriteReport_End (err);

	return;
}		// Do_DeleteMyAttributes_TW


// -----------------------------------------------------------------------------
// Show/Hide and Lock/Unlock the layer of lines : "2D Drafting - General"
// -----------------------------------------------------------------------------

static void		Do_InvertLayerOfLines (void)
{
	API_Attribute	attrib;
	GSErrCode			err;

	WriteReport ("# Show/Hide and Lock/Unlock the layer named \"2D Drafting - General\"");
	WriteReport ("#   - ViewOnly:    disabled");
	WriteReport ("#   - Client:");
	WriteReport ("#       not in workspace:  only Show/Hide status is allowed to be changed");
	WriteReport ("#       in workspace:      must work, all enabled");
	WriteReport ("#   - FullAccess:  must work, all enabled");
	WriteReport ("# Execute it in each TeamWork mode");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;
	strcpy (attrib.header.name, "2D Drafting - General");

	err = ACAPI_Attribute_Search (&attrib.header);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Search", err);
		return;
	}

	WriteReport ("layer index of \"2D Drafting - General\": #%d", attrib.header.index);

	err = ACAPI_Attribute_Get (&attrib);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Get", err);
		return;
	}

	attrib.header.flags ^= APILay_Hidden;
	attrib.header.flags ^= APILay_Locked;
	err = ACAPI_Attribute_Modify (&attrib, nullptr);
	if (err != NoError)
		WriteReport_Err ("ACAPI_Attribute_Modify", err);

	WriteReport_End (err);

	return;
}		// Do_InvertLayerOfLines


// -----------------------------------------------------------------------------
// Create a Layer Combination named "My TW Layercomb"
// -----------------------------------------------------------------------------

static void		Do_CreateLayerComb (void)
{
	WriteReport ("# Create a LayerCombination named \"My TW Layercomb\"");
	WriteReport ("#   - ViewOnly:    disabled");
	WriteReport ("#   - Client:      notMine layers must be locked");
	WriteReport ("#   - FullAccess:  must work");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layers menu\" and the \"Layer Settings...\" dialog after execution");

	const short startIndex = 10;
	const short endIndex = 40;

	API_AttributeDef defs = {};
	defs.layer_statItems = new GS::HashTable<API_AttributeIndex, API_LayerStat> ();

	short count = 0;
	for (short i = startIndex; i <= endIndex; i++) {	// fill an incomplete list of layer status
		API_LayerStat layerStat = {};
		layerStat.lFlags = APILay_Locked | APILay_ForceToWire;
		layerStat.conClassId = count;
		defs.layer_statItems->Add (ACAPI_CreateAttributeIndex (i), layerStat);	// unreferenced layers (except the ARCHICAD Layer) will be hidden in this layer combination
		count++;
	}

	API_Attribute attrib;
	attrib = {};
	attrib.header.typeID = API_LayerCombID;

	strcpy (attrib.layerComb.head.name, "My TW Layercomb");
	attrib.layerComb.lNumb = endIndex - startIndex + 1;

	GSErrCode err = ACAPI_Attribute_Create (&attrib, &defs);

	ACAPI_DisposeAttrDefsHdls (&defs);

	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Create", err);
		return;
	}

	WriteReport ("layerComb index of \"My TW Layercomb\": #%d", attrib.header.index);

	WriteReport_End (err);

	return;
}		// Do_CreateLayerComb


#ifdef __APPLE__
#pragma mark -
#endif

// =============================================================================
//
// Layers / Layer Combinations
//
// =============================================================================

// -----------------------------------------------------------------------------
//  List all layers
// -----------------------------------------------------------------------------

static void		Do_ListLayers (void)
{
	WriteReport ("# List of layers:");

	GSErrCode err = ACAPI_Attribute_EnumerateAttributesByType (API_LayerID, [&] (API_Attribute& attrib) {
		WriteReport ("  [%2s]   %s  %s  %s", attrib.header.index.ToUniString ().ToCStr ().Get (),
					 attrib.header.flags & APILay_Hidden ? "HID " : "    ",
					 attrib.header.flags & APILay_Locked ? "LOC " : "    ",
					 (attrib.header.index == APIApplicationLayerAttributeIndex) ? "Archicad Layer" : attrib.header.name);
	});

	WriteReport_End (err);

	return;
}		// Do_ListLayers


// -----------------------------------------------------------------------------
//  List all layer combinations
// -----------------------------------------------------------------------------

static void		Do_ListLayerCombinations (void)
{
	GS::Array<API_Attribute>	attributes;
	GSErrCode					err;

	WriteReport ("# List of layer combinations:");

	err = ACAPI_Attribute_GetAttributesByType (API_LayerCombID, attributes);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_GetAttributesByType", err);
		return;
	}

	WriteReport ("# Num: %d", attributes.GetSize ());
	for (const API_Attribute& layerCombAttrib : attributes) {
		API_AttributeDef defs;
		err = ACAPI_Attribute_GetDef (API_LayerCombID, layerCombAttrib.header.index, &defs);

		Int32 layerCount = layerCombAttrib.layerComb.lNumb;
		WriteReport ("-------------------------");
		WriteReport ("[%2s]   %s  nLay: %d", layerCombAttrib.header.index.ToUniString ().ToCStr ().Get (), layerCombAttrib.header.name, layerCount);
		for (auto it = defs.layer_statItems->BeginPairs (); it != nullptr; ++it) {
			API_Attribute layerAttrib = {};
			layerAttrib.header.typeID = API_LayerID;
			layerAttrib.header.index = it->key;
			err = ACAPI_Attribute_Get (&layerAttrib);
			if (err != NoError)
				WriteReport ("  [%2s]   err:%d", layerAttrib.header.index.ToUniString ().ToCStr ().Get (), err);
			else
				WriteReport ("  [%2s]   %s  %s   %3d   %s", layerAttrib.header.index.ToUniString ().ToCStr ().Get (),
							 it->value.lFlags & APILay_Hidden ? "HID " : "    ",
							 it->value.lFlags & APILay_Locked ? "LOC " : "    ",
							 it->value.conClassId,
							 (layerAttrib.header.index == APIApplicationLayerAttributeIndex) ? "Archicad Layer" : layerAttrib.header.name);
		}

		ACAPI_DisposeAttrDefsHdls (&defs);
	}

	WriteReport_End (err);

	return;
}		// Do_ListLayerCombinations


// -----------------------------------------------------------------------------
// Create a layer named "AAAA"
// -----------------------------------------------------------------------------

static void		Do_CreateLayer_AAAA (void)
{
	API_Attribute	attrib;
	GSErrCode			err;

	WriteReport ("# Create a layer named \"AAAA\"");
	WriteReport ("#   - not hidden, not locked");
	WriteReport ("#   - must be put into each layerComb as Hidden");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;

	strcpy (attrib.layer.head.name, "AAAA");
	err = ACAPI_Attribute_Create (&attrib, nullptr);

	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Create", err);
		return;
	}

	WriteReport ("layer index of \"AAAA\": #%d", attrib.header.index);

	WriteReport_End (err);

	return;
}		// Do_CreateLayer_AAAA


// -----------------------------------------------------------------------------
// Delete the layer named "AAAA"
// -----------------------------------------------------------------------------

static void		Do_DeleteLayer_AAAA (void)
{
	API_Attribute	attrib;
	GSErrCode			err;

	WriteReport ("# Delete the layer named \"AAAA\"");
	WriteReport ("#   - must be removed from all layer combinations");
	WriteReport ("#     (call the \"List layer combinations\" command)");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;
	strcpy (attrib.header.name, "AAAA");

	err = ACAPI_Attribute_Search (&attrib.header);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Search", err);
		return;
	}

	WriteReport ("layer index of \"AAAA\": #%d", attrib.header.index);

	err = ACAPI_Attribute_Delete (attrib.header);
	if (err != NoError)
		WriteReport_Err ("ACAPI_Attribute_Delete", err);

	WriteReport_End (err);

	return;
}		// Do_DeleteLayer_AAAA


// -----------------------------------------------------------------------------
// Lock/Unlock the layer named "AAAA"
// -----------------------------------------------------------------------------

static void		Do_InvLockLayer_AAAA (void)
{
	API_Attribute	attrib;
	GSErrCode			err;

	WriteReport ("# Lock/Unlock the layer named \"AAAA\"");
	WriteReport ("#   - no effect on layer combinations");
	WriteReport ("#     (call the \"List layer combinations\" command)");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;
	strcpy (attrib.header.name, "AAAA");

	err = ACAPI_Attribute_Search (&attrib.header);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Search", err);
		return;
	}

	WriteReport ("layer index of \"AAAA\": #%d", attrib.header.index);

	err = ACAPI_Attribute_Get (&attrib);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Get", err);
		return;
	}

	attrib.header.flags ^= APILay_Locked;
	err = ACAPI_Attribute_Modify (&attrib, nullptr);
	if (err != NoError)
		WriteReport_Err ("ACAPI_Attribute_Modify", err);

	WriteReport_End (err);

	return;
}		// Do_InvLockLayer_AAAA


// -----------------------------------------------------------------------------
// Lock/Hide the layer named "ARCHICAD"
// -----------------------------------------------------------------------------

static void		Do_LockHideLayer_1 (void)
{
	API_Attribute	attrib;
	GSErrCode			err;

	WriteReport ("# Lock/Hide the layer named \"Archicad\"");
	WriteReport ("#   - not allowed operation");
	WriteReport ("#     (call the \"List layers\" command)");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerID;
	attrib.header.index  = APIApplicationLayerAttributeIndex;

	err = ACAPI_Attribute_Get (&attrib);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Get", err);
		return;
	}

	attrib.header.flags |= APILay_Locked | APILay_Hidden;
	err = ACAPI_Attribute_Modify (&attrib, nullptr);		// should return an error
	if (err != NoError)
		WriteReport_Err ("ACAPI_Attribute_Modify", err);

	WriteReport_End (err);

	return;
}		// Do_LockHideLayer_1


// -----------------------------------------------------------------------------
// Create a layer combination named "BBBB"
// -----------------------------------------------------------------------------

static void		Do_CreateLayerComb_BBBB (void)
{
	WriteReport ("# Create a layer combination named \"BBBB\"");
	WriteReport ("#   - active set should remain the same");
	WriteReport ("#   - every second layer must be locked");
	WriteReport ("#   - \"Archicad\" layer cannot be LOCKED or HIDDEN");
	WriteReport ("# (call the \"List layer combinations\" command)");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layers menu\" and the \"Layer Settings...\" dialog after execution");

	GS::Array<API_Attribute> attributes;
	GSErrCode err = ACAPI_Attribute_GetAttributesByType (API_LayerID, attributes);

	API_AttributeDef defs = {};
	defs.layer_statItems = new GS::HashTable<API_AttributeIndex, API_LayerStat> ();
	for (UInt32 i = 0; i < attributes.GetSize (); ++i) {
		API_LayerStat layerStat = {};
		if (i % 2 == 0)
			layerStat.lFlags = APILay_Locked;
		layerStat.conClassId = 1;
		defs.layer_statItems->Add (attributes[i].header.index, layerStat);
	}

	API_Attribute attrib = {};
	attrib.header.typeID = API_LayerCombID;

	strcpy (attrib.layerComb.head.name, "BBBB");
	attrib.layerComb.lNumb = attributes.GetSize ();

	err = ACAPI_Attribute_Create (&attrib, &defs);

	ACAPI_DisposeAttrDefsHdls (&defs);

	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Create", err);
		return;
	}

	WriteReport ("layerComb index of \"BBBB\": #%d", attrib.header.index);

	WriteReport_End (err);

	return;
}		// Do_CreateLayerComb_BBBB


// -----------------------------------------------------------------------------
// Delete the layer combination named "BBBB"
// -----------------------------------------------------------------------------

static void		Do_DeleteLayerComb_BBBB (void)
{
	API_Attribute	attrib;
	GSErrCode			err;

	WriteReport ("# Delete the layer combination named \"BBBB\"");
	WriteReport ("#   - must disappear from Archicad menus");
	WriteReport ("#   - active set should be removed if it is");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layers...\" menu after execution");


	attrib = {};
	attrib.header.typeID = API_LayerCombID;
	strcpy (attrib.header.name, "BBBB");

	err = ACAPI_Attribute_Search (&attrib.header);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Search", err);
		return;
	}

	WriteReport ("layerComb index of \"BBBB\": #%d", attrib.header.index);

	err = ACAPI_Attribute_Delete (attrib.header);
	if (err != NoError)
		WriteReport_Err ("ACAPI_Attribute_Delete", err);

	WriteReport_End (err);

	return;
}		// Do_DeleteLayerComb_BBBB


// -----------------------------------------------------------------------------
// Show/Hide the layers in the layer combination named "BBBB"
// - active set should remain the same
// -----------------------------------------------------------------------------

static void		Do_InvShowLayerComb_BBBB (void)
{
	API_Attribute		attrib;
	API_AttributeDef	defs;
	GSErrCode			err;

	WriteReport ("# Show/Hide the layers in the layer combination named \"BBBB\"");
	WriteReport ("#   - active set should remain the same", false);
	WriteReport ("#   - \"Archicad\" layer cannot be LOCKED or HIDDEN");
	WriteReport ("# Archicad menus must be updated");
	WriteReport ("# Check the \"Layer Settings...\" dialog after execution");

	attrib = {};
	attrib.header.typeID = API_LayerCombID;
	strcpy (attrib.header.name, "BBBB");

	err = ACAPI_Attribute_Search (&attrib.header);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Search", err);
		return;
	}

	err = ACAPI_Attribute_Get (&attrib);
	defs = {};
	if (err == NoError)
		err = ACAPI_Attribute_GetDef (API_LayerCombID, attrib.header.index, &defs);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Get", err);
		return;
	}

	WriteReport ("layerComb index of \"BBBB\": #%d", attrib.header.index);

	for (auto it = defs.layer_statItems->BeginValues (); it != nullptr; ++it)
		it->lFlags ^= APILay_Hidden;

	err = ACAPI_Attribute_Modify (&attrib, &defs);
	if (err != NoError)
		WriteReport_Err ("ACAPI_Attribute_Modify", err);

	ACAPI_DisposeAttrDefsHdls (&defs);

	WriteReport_End (err);

	return;
}		// Do_InvShowLayerComb_BBBB


// -----------------------------------------------------------------------------
// Create a Composite Wall named "Extended CompWall (API)"
// -----------------------------------------------------------------------------

static void		Do_CreateCompWall (void)
{
	API_Attribute		attrib;
	API_AttributeDefExt	defs;
	short				nComps = 4, i;
	double				totalThick = 0.6;
	GSErrCode			err;

	WriteReport ("# Create a Composite Wall named \"Extended CompWall (API)\"");

	attrib = {};
	defs = {};
	defs.cwall_compItems = (API_CWallComponent **) BMAllocateHandle (nComps * sizeof (API_CWallComponent), ALLOCATE_CLEAR, 0);
	if (defs.cwall_compItems == nullptr) {
		WriteReport_Err ("BMAllocateHandle", ErrMemoryFull);
		return;
	}
	for (i = 0; i < nComps; i++) {
		(*defs.cwall_compItems)[i].buildingMaterial =  ACAPI_CreateAttributeIndex (i + 1);
		(*defs.cwall_compItems)[i].framePen = (short) (i + 1);
		(*defs.cwall_compItems)[i].flagBits = 1;
		(*defs.cwall_compItems)[i].fillThick = totalThick / nComps;
	}

	defs.cwall_compLItems = (API_CWallLineComponent **) BMAllocateHandle ((nComps + 1) * sizeof (API_CWallLineComponent), ALLOCATE_CLEAR, 0);
	if (defs.cwall_compLItems == nullptr) {
		WriteReport_Err ("BMAllocateHandle", ErrMemoryFull);
		return;
	}

	for (i = 0; i < nComps + 1; i++) {
		(*defs.cwall_compLItems)[i].ltypeInd = ACAPI_CreateAttributeIndex (i + 1);
		(*defs.cwall_compLItems)[i].linePen = (short) (i + 1);
	}

	attrib.header.typeID = API_CompWallID;

	strcpy (attrib.layerComb.head.name, "Extended CompWall (API)");
	attrib.compWall.totalThick = totalThick;
	attrib.compWall.nComps = nComps;

	err = ACAPI_Attribute_CreateExt (&attrib, &defs);

	ACAPI_DisposeAttrDefsHdlsExt (&defs);

	if (err != NoError) {
		WriteReport_Err ("ACAPI_Attribute_Create", err);
		return;
	}

	WriteReport ("CompWall index of \"Extended CompWall (API)\": #%d", attrib.header.index);

	WriteReport_End (err);

	return;
}		// Do_CreateCompWall


// -----------------------------------------------------------------------------
// Modify the CompWall: Extended CompWall
// -----------------------------------------------------------------------------

static void		Do_ChangeCompWall (void)
{
	API_Attribute		attrib;
	API_AttributeDefExt	defs;
	GSErrCode			err;
	double				extra = 0.2;

	WriteReport ("# Change the CompWall: \"Extended CompWall (API)\"");
	WriteReport ("# Check the \"Composite...\" dialog after execution");

	attrib = {};
	defs = {};

	attrib.header.typeID = API_CompWallID;
	strcpy (attrib.header.name, "Extended CompWall (API)");

	err = ACAPI_Attribute_Search (&attrib.header);		// search by name: index returned in header
	if (err != NoError) {
		WriteReport_Err ("CompWall \"Extended CompWall (API)\" was not found", err);
		return;
	}
	err = ACAPI_Attribute_Get (&attrib);
	if (err == NoError)
		err = ACAPI_Attribute_GetDefExt (attrib.header.typeID, attrib.header.index, &defs);
	if (err != NoError) {
		WriteReport_Err ("Unable to get the compWall definition", err);
		return;
	}

	attrib.compWall.totalThick += extra;
	attrib.compWall.nComps++;

	defs.cwall_compItems = (API_CWallComponent **) BMReallocHandle ((GSHandle) defs.cwall_compItems,
																	(attrib.compWall.nComps) * sizeof (API_CWallComponent), REALLOC_CLEAR, 0);
	if (defs.cwall_compItems == nullptr)
		return;

	defs.cwall_compLItems = (API_CWallLineComponent **) BMReallocHandle ((GSHandle) defs.cwall_compLItems,
																		 (attrib.compWall.nComps + 1) * sizeof (API_CWallLineComponent), REALLOC_CLEAR, 0);
	if (defs.cwall_compLItems == nullptr)
		return;

	(*defs.cwall_compItems)[attrib.compWall.nComps - 1].buildingMaterial = ACAPI_CreateAttributeIndex (1);
	(*defs.cwall_compItems)[attrib.compWall.nComps - 1].framePen = 4;
	(*defs.cwall_compItems)[attrib.compWall.nComps - 1].flagBits = (*defs.cwall_compItems)[0].flagBits;
	(*defs.cwall_compItems)[attrib.compWall.nComps - 1].fillThick = extra;

	(*defs.cwall_compLItems)[attrib.compWall.nComps].ltypeInd = ACAPI_CreateAttributeIndex (6);
	(*defs.cwall_compLItems)[attrib.compWall.nComps].linePen = 6;

	err = ACAPI_Attribute_ModifyExt (&attrib, &defs);

	ACAPI_DisposeAttrDefsHdlsExt (&defs);
	if (err != NoError) {
		WriteReport_Err ("Unable to modify the compWall \"Extended CompWall\"", err);
		return;
	}

	WriteReport_End (err);

	return;
}		// Do_ChangeCompWall


// -----------------------------------------------------------------------------
// Print all folders under Lines
// -----------------------------------------------------------------------------

static void		PrintAllFolders (const API_AttributeFolder& folder, USize indent)
{
	GS::UniString indentStr;
	indentStr.SetLength (indent * 4, ' ');

	GS::UniString line = indentStr + (indent == 0 ? "<Root folder>" : folder.path.GetLast ());
	WriteReport ("%s", line.ToCStr ().Get ());

	API_AttributeFolderContent folderContent;
	if (DBVERIFY (ACAPI_Attribute_GetFolderContent (folder, folderContent) == NoError)) {
		for (const API_AttributeFolder& subFolder : folderContent.subFolders)
			PrintAllFolders (subFolder, indent + 1);
	}
}


API_AttributeFolder CreateLineFolder (const GS::Array<GS::UniString>& path)
{
	API_AttributeFolder folder;
	folder.typeID = API_LinetypeID;
	folder.path = path;
	DBVERIFY (ACAPI_Attribute_CreateFolder (folder) == NoError);

	return folder;
}


void DeleteFolder (const API_AttributeFolder& folder)
{
	DBVERIFY (ACAPI_Attribute_DeleteFolder (folder) == NoError);
}


void DeleteLineFolder (const GS::Array<GS::UniString>& path)
{
	API_AttributeFolder folder;
	folder.typeID = API_LinetypeID;
	folder.path = path;
	DeleteFolder (folder);
}


void RenameFolder (const API_AttributeFolder& folder, const GS::UniString& newName)
{
	DBVERIFY (ACAPI_Attribute_RenameFolder (folder, newName) == NoError);
}


static void		Do_CreateFoldersUnderLines ()
{
	CreateLineFolder ({ "A", "B", "C1" });
	CreateLineFolder ({ "A", "B", "C2" });
	CreateLineFolder ({ "A", "B", "X" });
}


static void		Do_DeleteFoldersUnderLines ()
{
	auto c1 = CreateLineFolder ({ "A", "B", "C1" });
	auto c2 = CreateLineFolder ({ "A", "B", "C2" });
	DeleteFolder (c1);
	DeleteFolder (c2);
	DeleteLineFolder ({ "A", "B" });
	DeleteLineFolder ({ "A" });
}


static void		Do_RenameFoldersUnderLines ()
{
	auto c1 = CreateLineFolder ({ "A", "B", "C1" });
	auto c2 = CreateLineFolder ({ "A", "B", "C2" });
	RenameFolder (c2, "C3");
}


static void		Do_PrintAllFoldersUnderLines ()
{
	API_AttributeFolder rootFolder;
	rootFolder.typeID = API_LinetypeID;

	PrintAllFolders (rootFolder, 0);
}


// -----------------------------------------------------------------------------
// Register importer function into Attribute Manager
// -----------------------------------------------------------------------------

static void Do_ImportFormatRegistration (void)
{
	API_AttributeManagerFormat formatData;
	formatData.fileCreator = 'GSAC';
	formatData.fileType = 'TEXT';
	formatData.fileExtension = "txt";
	formatData.popupText = "Test addon format";

	GSErrCode err = ACAPI_AddOnIntegration_RegisterAttributeImportFormat (formatData);

	if (err != NoError)
		DBPrintf ("Attribute Test:: RegisterInterface() ACAPI_AddOnIntegration_RegisterAttributeImportFormat failed\n");
}

GSErrCode ImportAttributeProcess (const IO::Location& sourceFile, GS::Array<GS::Pair<API_Attribute, API_AttributeDefExt*>>& result)
{
	IO::Name fileName;
	sourceFile.GetLastLocalName (&fileName);

	WriteReport ("Importing attributes from: %s", sourceFile.ToDisplayText ().ToPrintf ());

	{
		API_Attribute attrib = {};

		attrib.header.typeID = API_BuildingMaterialID;
		strcpy (attrib.header.name, "Test Building Material");

		Int32 uiPriority = 8;
		Int32 elemPriority = 0;
		ACAPI_Element_UI2ElemPriority (&uiPriority, &elemPriority);
		attrib.buildingMaterial.connPriority = elemPriority;
		attrib.buildingMaterial.cutFill = ACAPI_CreateAttributeIndex (1);
		attrib.buildingMaterial.cutFillPen = 4;
		attrib.buildingMaterial.cutFillBackgroundPen = 0;
		attrib.buildingMaterial.cutMaterial = ACAPI_CreateAttributeIndex (75);
		attrib.buildingMaterial.cutFillOrientation = APIFillOrientation_ProjectOrigin;
		attrib.buildingMaterial.thermalConductivity = 0.4;
		attrib.buildingMaterial.density = 1500.0;
		attrib.buildingMaterial.heatCapacity = 700.0;
		attrib.buildingMaterial.showUncutLines = true;

		result.Push ({ attrib, nullptr });
	}

	{
		API_Attribute		profile_attr = {};
		API_AttributeDefExt* profile_attrdef = new API_AttributeDefExt ();

		*profile_attrdef = {};

		profile_attr.header.typeID = API_ProfileID;
		profile_attr.header.flags = 0;
		strcpy (profile_attr.header.name, "Profile from API");
		profile_attr.profile.wallType = true;
		profile_attr.profile.beamType = false;
		profile_attr.profile.coluType = true;

		profile_attrdef->profile_vectorImageItems = new ProfileVectorImage ();
		BuildProfileDescription (profile_attrdef->profile_vectorImageItems);

		result.Push ({ profile_attr, profile_attrdef });
	}

	return NoError;
}


static void		Do_MoveAttributesAndFoldersUnderLines ()
{
		// Preparations

	auto a1 = CreateLineFolder ({ "A", "A1" });
	auto a2 = CreateLineFolder ({ "A", "A2" });
	auto b = CreateLineFolder ({ "b" });

	const GS::Array<GS::Guid> lineGuids = GetSomeAttributes (API_LinetypeID, 2);

		// Move

	const GSErrCode err = ACAPI_Attribute_Move ({ a1, a2 },
												lineGuids,
												b);
	DBVERIFY (err == NoError);
}


#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	enum AttributeTestMenuItems {
		CountAttributes = 1,
		ListAttributes,
		RenameAttributes,
		CopyAttributes,
		DeleteAttributes,
		Separator1,
		CreateLayerTraining,
		CreateLineTypesTraining,
		CreateFillsTraining,
		CreateCompWall,
		CreateProfile,
		CreatePenTable,
#ifndef NO_MEP_FEATURES
		CreateMEPSystem,
#endif
		CreateBuildingMaterial,
		CopyZoneCategory
	};

	enum AttributeModifyMenuItems {
		HideLayer = 1,
		ChangeLineType,
		ChangeCompositeWall,
		ChangeBuildingMaterial,
		ChangeZoneCategory
	};

	switch (menuParams->menuItemRef.menuResID) {
		case 32500:		/* Attributes */
				switch (menuParams->menuItemRef.itemIndex) {
					case CountAttributes:			Do_CountAttributes ();				break;
					case ListAttributes:			Do_ListAttributes ();				break;
					case RenameAttributes:			Do_RenameAttributes (true);			break;
					case CopyAttributes:			Do_CopyAttributes ();				break;
					case DeleteAttributes:			Do_DeleteAttributes (true);			break;
					/* ---- */
					case CreateLayerTraining:		Do_CreateLayer_Training ();			break;
					case CreateLineTypesTraining:	Do_CreateLinetypes_Training ();		break;
					case CreateFillsTraining:		Do_CreateFills_Training ();			break;
					case CreateCompWall:			Do_CreateCompWall ();				break;
					case CreateProfile:				Do_CreateProfile ();				break;
					case CreatePenTable:			Do_CreatePenTable ();				break;
#ifndef NO_MEP_FEATURES
					case CreateMEPSystem:			Do_CreateMEPSystem ();				break;
#endif
					case CreateBuildingMaterial:	Do_CreateBuildingMaterial ();		break;
					case CopyZoneCategory:			Do_CopyZoneCategory ();				break;
				}
				break;

		case 32501:		/* Attributes: Modify */
				switch (menuParams->menuItemRef.itemIndex) {
					case HideLayer:					Do_HideLayerOfLines ();				break;
					case ChangeLineType:			Do_ChangeLinetype_Wave ();			break;
					case ChangeCompositeWall:		Do_ChangeCompWall ();				break;
					case ChangeBuildingMaterial:	Do_ChangeBuildingMaterial ();		break;
					case ChangeZoneCategory:		Do_ChangeZoneCategory ();			break;
				}
				break;

		case 32502:		/* Attributes: Teamwork mode */
				switch (menuParams->menuItemRef.itemIndex) {
					case 1:		Do_CreateAttributes_TW ();			break;
					case 2:		Do_RenameMyAttributes_TW ();		break;
					case 3:		Do_DeleteMyAttributes_TW ();		break;
					/* ---- */
					case 5:		Do_InvertLayerOfLines ();			break;
					case 6:		Do_CreateLayerComb ();				break;
				}
				break;

		case 32503:		/* Attributes: Snippets */
				switch (menuParams->menuItemRef.itemIndex) {
					case 1:		Do_ListLayers ();					break;
					case 2:		Do_ListLayerCombinations ();		break;
					/* ---- */
					case 4:		Do_CreateLayer_AAAA ();				break;
					case 5:		Do_DeleteLayer_AAAA ();				break;
					case 6:		Do_InvLockLayer_AAAA ();			break;
					case 7:		Do_LockHideLayer_1 ();				break;
					/* ---- */
					case 9:		Do_CreateLayerComb_BBBB ();			break;
					case 10:	Do_DeleteLayerComb_BBBB ();			break;
					case 11:	Do_InvShowLayerComb_BBBB ();		break;
					/* ---- */
					case 13:	Do_ListAllProfileDetails ();		break;
					case 14:	Do_ListAllOpProfileDetails ();		break;
					case 15:	Do_ListAllBuildingMaterials ();		break;
				}
				break;

		case 32504:		/* Attributes: Folders */
				switch (menuParams->menuItemRef.itemIndex) {
					case 1:		Do_CreateFoldersUnderLines ();				break;
					case 2:		Do_DeleteFoldersUnderLines ();				break;
					case 3:		Do_PrintAllFoldersUnderLines ();			break;
					case 4:		Do_MoveAttributesAndFoldersUnderLines ();	break;
					case 5:		Do_RenameFoldersUnderLines ();				break;
				}
				break;
	}


	return NoError;
}		// MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = NoError;
	err = ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore);
	if (err == NoError)
		err = ACAPI_MenuItem_RegisterMenu (32501, 0, MenuCode_UserDef, MenuFlag_Default);
	if (err == NoError)
		err = ACAPI_MenuItem_RegisterMenu (32502, 0, MenuCode_UserDef, MenuFlag_Default);
	if (err == NoError)
		err = ACAPI_MenuItem_RegisterMenu (32503, 0, MenuCode_UserDef, MenuFlag_Default);
	if (err == NoError)
		err = ACAPI_MenuItem_RegisterMenu (32504, 0, MenuCode_UserDef, MenuFlag_SeparatorAfter);
	if (err == NoError && enableImportFormatRegistration)
		Do_ImportFormatRegistration ();
	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Attribute Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32501, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Attribute Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32502, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Attribute Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32503, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Attribute Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32504, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Attribute Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	if (enableImportFormatRegistration) {
		err = ACAPI_AddOnIntegration_InstallAttrManagerImportMethod ("txt", ImportAttributeProcess);
		if (err != NoError)
			DBPrintf ("Attribute Test:: Initialize() ACAPI_AddOnIntegration_InstallAttrManagerImportMethod failed\n");
	}

	return NoError;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
