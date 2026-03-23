// *****************************************************************************
// Source code for the Interface Functions Add-On
// *****************************************************************************

#define	_INTERFACE_FUNCTIONS_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"
#include	"ACAPI/CuttingPlane.hpp"

#include	"FileTypeManager.hpp"
#include	"DGModule.hpp"
#include	"IV.hpp"

#include	"ACAPI/InputHandler.hpp"

// ---------------------------------- Types ------------------------------------

#define	 Menu_OpenPalette	1
#define	 Menu_ShowPalette	2
#define	 Menu_GetAPoint		4
#define	 Menu_GetALine		5
#define	 Menu_GetAnArc		6
#define	 Menu_GetARect		7
#define	 Menu_GetARotRect	8
#define	 Menu_GetAPoly		9
#define  Menu_Constraint	10
#define	 Menu_TypeItem		12
#define	 Menu_RubberLine	13
#define	 Menu_Sensitive		14
#define  Menu_FeedBack		15
#define  Menu_3DCutaway		17
#define  Menu_CuttingPlanes	18
#define	 Menu_PetPalette	20
#define	 Menu_OpenLibPart	22
#define	 Menu_OpenPicture	23
#define	 Menu_OpenZoneStamp	24
#define	 Menu_Forward		26
#define	 Menu_ShowSelection	28
#define	 Menu_ShowAll		29
#define  Menu_CheckEditable	30
#define  Menu_SetInputPos	32

#define	 Dial_GetAPoint		1
#define	 Dial_GetALine		2
#define	 Dial_GetAnArc		3
#define	 Dial_GetARect		4
#define	 Dial_GetARotRect	5
#define	 Dial_GetAPoly		6
#define	 Dial_TypeCylind	7
#define	 Dial_TypeRect		8
#define	 Dial_RubberLine	9
#define	 Dial_Sensitive		10
#define	 Dial_FeedBack		11
#define	 Dial_SpaceValue	12



typedef struct {
	short			dialID;					/* dialog ID of the palette */
	short			filler_1;
	bool			rubberIsOn;				/* rubber line mode */
	bool			disableDefaultFeedback;	/* double line mode	*/
	bool			filterIsOn;				/* filtered input */
	bool			circleBased;			/* column type */
	double			defSpacing;				/* default gap between columns */
	bool			inMyInput;				/* the input was initiated from this add-on */
	bool			hiddenByUser;			/* palette was hidden from menu by the user */
	bool			hiddenByAC;				/* palette was hidden through the API callback */
	bool			filler_2;
	Int32			filler_3;
} CntlDlgData;


// ---------------------------------- Variables --------------------------------

static bool					firstCall = true;

static CntlDlgData			cntlDlgData;
static Int32				nBase;
static API_Coord3D**		baseCoord;
static API_Coord3D*			origoCoord;
static double				actSpacing = 0.0;
static double				defColumnThick = 0.0;
static API_PolyGetMethod	gPolyMethod = APIPolyGetMethod_General;
static bool					actualNegArc = true;
static bool					applyConstraint = false;	/* enforce perpendicular segments for polygonal input */
static bool					isLayout = false;
static bool					isModel = false;


// ---------------------------------- Prototypes -------------------------------

static void	EnablePaletteControls (bool enable);
static void	Do_PaletteShowHide (bool byUserFromMenu, bool beginHide);




// =============================================================================
//
// Utility functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Load a localisable string from resource
// -----------------------------------------------------------------------------

static void		GetStringFromResource (char *buffer, short resID, short stringID)
{
	if (!ACAPI_Resource_GetLocStr (buffer, resID, stringID))
		*buffer = '\0';

	return;
}		// GetStringFromResource


// -----------------------------------------------------------------------------
// Load a localisable Unicode string from resource
// -----------------------------------------------------------------------------

static void		GetStringFromResource (GS::UniString* buffer, short resID, short stringID)
{
	if (buffer != nullptr && !RSGetIndString (buffer, resID, stringID, ACAPI_GetOwnResModule ()))
		buffer->Clear ();

	return;
}		// GetStringFromResource


// -----------------------------------------------------------------------------
// Return the length of a 2D vector
// -----------------------------------------------------------------------------

static double	LenVector (const API_Vector *v)
{
	return (sqrt (v->x * v->x + v->y * v->y));
}		// LenVector


// -----------------------------------------------------------------------------
// Calculate the coordinates for the fence
// -----------------------------------------------------------------------------

static GSErrCode	CalculateFenceCoords (API_Coord3D		begPos,
										  API_Coord3D		endPos,
										  API_Coord3D		*origoPos,
										  bool				negArc,
										  double			space,
										  API_Coord3D		***theCoords,
										  double			***rotAngles,
										  Int32				*nItems)
{
	API_Vector		vect;
	double			length, radius, angle, begAngle, endAngle, sinusHalfAngle;
	double			deltaX, deltaY, deltaZ, deltaAngle;
	Int32			numItems, ind;
	GSErrCode		err;

	if (theCoords == nullptr || nItems == 0 || space < EPS)
		return APIERR_GENERAL;

	*theCoords = nullptr;
	if (rotAngles != nullptr)
		*rotAngles = nullptr;

	err = NoError;
	*theCoords = nullptr;
	*nItems = 0;

	deltaX = deltaY = deltaZ = 0.0;

	vect.x = endPos.x - begPos.x;
	vect.y = endPos.y - begPos.y;
	length = LenVector (&vect);

	if (origoPos != nullptr) {			// curved case

		vect.x = begPos.x - origoPos->x;
		vect.y = begPos.y - origoPos->y;
		radius = LenVector (&vect);

		if (radius > space / 2.0 + EPS) {

			deltaX = begPos.x - origoPos->x;
			deltaY = begPos.y - origoPos->y;
			if (fabs (deltaX) > EPS)
				begAngle = atan2 (deltaY, deltaX);
			else
				begAngle = deltaY < 0.0 ? -PI / 2.0 : PI / 2.0;

			deltaX = endPos.x - origoPos->x;
			deltaY = endPos.y - origoPos->y;
			if (fabs (deltaX) > EPS)
				endAngle = atan2 (deltaY, deltaX);
			else
				endAngle = deltaY < 0.0 ? -PI / 2.0 : PI / 2.0;

			sinusHalfAngle = space / 2.0 / radius;
			deltaAngle = fabs (2 * asin (sinusHalfAngle));

			if (negArc) {
				deltaAngle = -deltaAngle;
				while (begAngle < endAngle)
					begAngle += 2.0 * PI;
			} else {
				while (endAngle < begAngle)
					endAngle += 2.0 * PI;
			}

			numItems = 0;
			if (fabs (deltaAngle) > EPS)
				numItems = (Int32) (fabs (endAngle - begAngle) / fabs (deltaAngle)) + 1;
			if (numItems > 0) {
				*theCoords = (API_Coord3D **) BMAllocateHandle (numItems * sizeof (API_Coord3D), ALLOCATE_CLEAR, 0);
				err = BMError ();
				if (err == NoError && rotAngles != nullptr) {
					*rotAngles = (double **) BMAllocateHandle (numItems * sizeof (double), ALLOCATE_CLEAR, 0);
					err = BMError ();
				}
				if (err == NoError) {
					for (ind = 0; ind < numItems; ind++) {
						angle = begAngle + (double) ind * deltaAngle;
						(**theCoords) [ind].x = origoPos->x + radius * cos (angle);
						(**theCoords) [ind].y = origoPos->y + radius * sin (angle);
						(**theCoords) [ind].z = origoPos->z;
						if (rotAngles != nullptr && *rotAngles != nullptr)
							(**rotAngles) [ind] = angle;
					}
					*nItems = numItems;
				}
			}

		}

	} else {						// linear case

		deltaX = endPos.x - begPos.x;
		deltaY = endPos.y - begPos.y;
		if (fabs (deltaX) > EPS)
			angle = atan2 (deltaY, deltaX);
		else
			angle = deltaY < 0.0 ? -PI / 2.0 : PI / 2.0;

		numItems = ((Int32) (length / space)) + 1;
		*theCoords = (API_Coord3D **) BMAllocateHandle (numItems * sizeof (API_Coord3D), ALLOCATE_CLEAR, 0);
		err = BMError ();
		if (err == NoError && rotAngles != nullptr) {
			*rotAngles = (double **) BMAllocateHandle (numItems * sizeof (double), ALLOCATE_CLEAR, 0);
			err = BMError ();
		}
		if (err == NoError) {
			if (numItems > 1) {
				deltaX = (endPos.x - begPos.x) * space / length;
				deltaY = (endPos.y - begPos.y) * space / length;
				deltaZ = (endPos.z - begPos.z) * space / length;
			}

			for (ind = 0; ind < numItems; ind++) {
				(**theCoords) [ind].x = begPos.x + (double) ind * deltaX;
				(**theCoords) [ind].y = begPos.y + (double) ind * deltaY;
				(**theCoords) [ind].z = begPos.z + (double) ind * deltaZ;
				if (rotAngles != nullptr && *rotAngles != nullptr)
					(**rotAngles) [ind] = angle;
			}
			*nItems = numItems;
			err = NoError;
		}
	}

	return err;
}		// CalculateFenceCoords


// -----------------------------------------------------------------------------
// Place the fence
// -----------------------------------------------------------------------------

static GSErrCode	CreateTheFence (API_Coord3D	**theCoords,
									double		**rotAngles,
									Int32		nItems,
									bool		ignoreFirstCoord)
{
	API_DatabaseInfo	currentDB, planDB;
	API_Element			element;
	Int32				ind;
	double				floorLevel = 0.0;
	GSErrCode			err;

	if (theCoords == nullptr || nItems <= 0)
		return APIERR_GENERAL;

	ACAPI_Database_GetCurrentDatabase (&currentDB);
	if (isModel) {
		BNZeroMemory (&planDB, sizeof (API_DatabaseInfo));
		planDB.typeID = APIWind_FloorPlanID;
		ACAPI_Database_ChangeCurrentDatabase (&planDB);

		API_StoryInfo	storyInfo;
		BNZeroMemory (&storyInfo, sizeof (API_StoryInfo));
		err = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
		if (err == NoError)
			floorLevel = (*storyInfo.data)[storyInfo.actStory - storyInfo.firstStory].level;
		BMKillHandle ((GSHandle *) &storyInfo.data);

	}
	element = {};

	element.header.type = isModel ? API_ColumnID : API_ArcID;
	API_ElementMemo memo;
	memo = {};
	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err == NoError) {
		if (!isModel) {
			element.arc.r = isLayout ? 0.005 : defColumnThick / 2.0;
			element.arc.ratio = 1.0;
			element.arc.whole = true;
		} else {
			if (memo.columnSegments != nullptr) {
				memo.columnSegments[0].assemblySegmentData.circleBased = cntlDlgData.circleBased;
				memo.columnSegments[0].assemblySegmentData.modelElemStructureType = API_BasicStructure;
			}
		}
		if (ignoreFirstCoord)
			ind = 1;
		else
			ind = 0;
		for (; ind < nItems; ind++) {
			if (!isModel) {
				element.arc.origC.x = (*theCoords) [ind].x;
				element.arc.origC.y = (*theCoords) [ind].y;
			} else {
				element.column.origoPos.x			= (*theCoords) [ind].x;
				element.column.origoPos.y			= (*theCoords) [ind].y;
				element.column.bottomOffset			= (*theCoords) [ind].z - floorLevel;
				element.column.axisRotationAngle	= (rotAngles != nullptr) ? (*rotAngles) [ind] : 0.0;
			}
			err = ACAPI_Element_Create (&element, &memo);
			if (err != NoError)
				break;
		}
	}
	ACAPI_DisposeElemMemoHdls (&memo);

	if (isModel)
		ACAPI_Database_ChangeCurrentDatabase (&currentDB);

	return err;
}		// CreateTheFence


// -----------------------------------------------------------------------------
// Get the default thickness of columns
// -----------------------------------------------------------------------------

static double	GetDefColumnThick (void)
{
	GSErrCode		err;
	double			width;
	API_Element		element;

	width = 0.0;
	element = {};

	element.header.type = API_ColumnID;

	API_ElementMemo memo;
	memo = {};
	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err == NoError) {
		if (memo.columnSegments != nullptr) {
			width = memo.columnSegments[0].assemblySegmentData.nominalHeight + 2.0 * memo.columnSegments[0].venThick;
		}

		//width = element.column.coreDepth + 2.0 * element.column.venThick;
	}
	ACAPI_DisposeElemMemoHdls (&memo);

	return width;
}		// GetDefColumnThick


// =============================================================================
//
// Menu Item manipulations
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Enable/disable a menu item of the addon
// -----------------------------------------------------------------------------

static void		EnableACMenuItem (short itemInd, bool enable)
{
	API_MenuItemRef	itemRef;
	GSFlags			itemFlags;

	BNZeroMemory (&itemRef, sizeof (API_MenuItemRef));
	itemRef.menuResID = 32500;
	itemRef.itemIndex = itemInd;

	itemFlags = 0;
	ACAPI_MenuItem_GetMenuItemFlags (&itemRef, &itemFlags);
	if (enable)
		itemFlags &= ~API_MenuItemDisabled;
	else
		itemFlags |= API_MenuItemDisabled;
	ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &itemFlags);

	return;
}		// EnableACMenuItem


// -----------------------------------------------------------------------------
// Checkmark a menu item of the addon
// -----------------------------------------------------------------------------

static void		CheckACMenuItem (short itemInd, bool checked)
{
	API_MenuItemRef	itemRef;
	GSFlags			itemFlags;

	BNZeroMemory (&itemRef, sizeof (API_MenuItemRef));
	itemRef.menuResID = 32500;
	itemRef.itemIndex = itemInd;

	itemFlags = 0;
	ACAPI_MenuItem_GetMenuItemFlags (&itemRef, &itemFlags);
	if (checked)
		itemFlags |= API_MenuItemChecked;
	else
		itemFlags &= ~API_MenuItemChecked;
	ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &itemFlags);

	ACAPI_SetPreferences (1, sizeof (CntlDlgData), &cntlDlgData);			// save the dialog settings

	return;
}		// CheckACMenuItem


// =============================================================================
//
// Getting user input
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Switch Palette related texts in menu items
// -----------------------------------------------------------------------------

static void		SetPaletteMenuTexts (bool paletteIsOn, bool paletteIsVisible)
{
	API_MenuItemRef	itemRef;
	GS::UniString	itemStr;

	if (paletteIsOn)
		GetStringFromResource (&itemStr, 32600, 2);
	else
		GetStringFromResource (&itemStr, 32600, 1);

	BNZeroMemory (&itemRef, sizeof (API_MenuItemRef));
	itemRef.menuResID = 32500;
	itemRef.itemIndex = Menu_OpenPalette;
	ACAPI_MenuItem_SetMenuItemText (&itemRef, nullptr, &itemStr);

	if (paletteIsOn && paletteIsVisible)
		GetStringFromResource (&itemStr, 32600, 4);
	else
		GetStringFromResource (&itemStr, 32600, 3);

	BNZeroMemory (&itemRef, sizeof (API_MenuItemRef));
	itemRef.menuResID = 32500;
	itemRef.itemIndex = Menu_ShowPalette;
	ACAPI_MenuItem_SetMenuItemText (&itemRef, nullptr, &itemStr);

	EnableACMenuItem (Menu_ShowPalette, paletteIsOn);
	return;
}		// SetPaletteMenuTexts


// -----------------------------------------------------------------------------
// Fill API_NeigFilter with Line Neig types
// -----------------------------------------------------------------------------

static void		SetFilterForInput (API_NeigFilter *filter)
{
	ACAPI_UserInput_InitNeigFilter (filter,APIInitNeigFilter_Empty);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_Line);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_LineOn);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_Arc);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_ArcOn);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_Circ);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_CircOn);
	ACAPI_UserInput_SetNeigFilter (filter,APINeig_Hot);
	return;
}		// SetFilterForInput


// -----------------------------------------------------------------------------
// Fill API_CursorSet for changing input cursor forms
// -----------------------------------------------------------------------------

static void		SetCursorSetForInput (API_CursorSet	*cursorSet, short mySetType)
{
	switch (mySetType) {
		case 1:
				cursorSet->nothingOnCursor		= APICursor_Hammer;
				cursorSet->pointOnCursor		= APICursor_Hammer;
				cursorSet->lineOnCursor			= APICursor_Hammer;
				cursorSet->refPointOnCursor		= APICursor_Hammer;
				cursorSet->refLineOnCursor		= APICursor_Hammer;
				cursorSet->crossOnCursor		= APICursor_Hammer;
				cursorSet->normalOnCursor		= APICursor_Hammer;
				cursorSet->tangentOnCursor		= APICursor_Hammer;
				break;
		case 2:
				cursorSet->nothingOnCursor		= APICursor_Hand;
				cursorSet->pointOnCursor		= APICursor_MiniHand;
				cursorSet->lineOnCursor			= APICursor_MiniHand;
				cursorSet->refPointOnCursor		= APICursor_MiniHand;
				cursorSet->refLineOnCursor		= APICursor_MiniHand;
				cursorSet->crossOnCursor		= APICursor_MiniHand;
				cursorSet->normalOnCursor		= APICursor_MiniHand;
				cursorSet->tangentOnCursor		= APICursor_MiniHand;
				break;
		case 3:
				cursorSet->nothingOnCursor		= APICursor_XPoint;
				cursorSet->pointOnCursor		= APICursor_ArrowXPoint;
				cursorSet->lineOnCursor			= APICursor_PencilXPoint;
				cursorSet->refPointOnCursor		= APICursor_ArrowXPoint;
				cursorSet->refLineOnCursor		= APICursor_PencilXPoint;
				cursorSet->crossOnCursor		= APICursor_ArrowXPoint;
				cursorSet->normalOnCursor		= APICursor_PencilXPoint;
				cursorSet->tangentOnCursor		= APICursor_ArrowXPoint;
				break;
		case 4:
				cursorSet->nothingOnCursor		= APICursor_Eye;
				cursorSet->pointOnCursor		= APICursor_DoubleEye;
				cursorSet->lineOnCursor			= APICursor_DoubleEye;
				cursorSet->refPointOnCursor		= APICursor_DoubleEye;
				cursorSet->refLineOnCursor		= APICursor_DoubleEye;
				cursorSet->crossOnCursor		= APICursor_DoubleEye;
				cursorSet->normalOnCursor		= APICursor_DoubleEye;
				cursorSet->tangentOnCursor		= APICursor_DoubleEye;
				break;
		default:
				cursorSet->nothingOnCursor		= APICursor_Default;
				cursorSet->pointOnCursor		= APICursor_Default;
				cursorSet->lineOnCursor			= APICursor_Default;
				cursorSet->refPointOnCursor		= APICursor_Default;
				cursorSet->refLineOnCursor		= APICursor_Default;
				cursorSet->crossOnCursor		= APICursor_Default;
				cursorSet->normalOnCursor		= APICursor_Default;
				cursorSet->tangentOnCursor		= APICursor_Default;
				break;
	}

	return;
}		// SetCursorSetForInput


// -----------------------------------------------------------------------------
// Callback function to store a new poly node
// -----------------------------------------------------------------------------

static void AddPolyNodeCallBack (const API_Coord3D *c3)
{
	GSErrCode	err;
	API_Coord3D	c3Copy (*c3);

	if (baseCoord == nullptr) {
		baseCoord = (API_Coord3D **) BMAllocateHandle (sizeof (API_Coord3D), ALLOCATE_CLEAR, 0);
		nBase = 1;
	} else {
		nBase ++;
		BMReallocHandle ((GSHandle) baseCoord, nBase * sizeof (API_Coord3D), REALLOC_CLEAR, 0);
	}
	err = BMError ();

	if (err == NoError)
		(*baseCoord) [nBase-1] = c3Copy;

	if (err == NoError && nBase > 1 && applyConstraint) {
		API_Coord3D cPrev = (*baseCoord) [nBase-2];
		double angleRad = atan2 (c3->y - cPrev.y, c3->x - cPrev.x);

		API_ConstraintPars  constPars;
		BNZeroMemory (&constPars, sizeof (API_ConstraintPars));
		constPars.constType = APIConst_A;
		constPars.constValue = angleRad + 1.5707963267948966192313216916398;
		constPars.useLocOrigo = true;
		err = ACAPI_UserInput_SetConstraint (&constPars);
	}

	return;
}		// AddPolyNodeCallBack


// -----------------------------------------------------------------------------
// Callback function to delete a new poly node
// -----------------------------------------------------------------------------

static void DelPolyNodeCallBack (void)
{
	if (baseCoord == nullptr || nBase == 0)
		return;

	nBase--;

	return;
}		// DelPolyNodeCallBack


// -----------------------------------------------------------------------------
// Callback function to draw the rubber lines
// -----------------------------------------------------------------------------

static void RubberLineCallBack (const API_RubberLineInfo *info)
{
	API_RubberLineType	rubberLine;
	API_Coord3D			**theCoords, actCoord, begC, endC;
	Int32				nItems, ind, i;
	double				crossWidth, sinAngle, cosAngle;
	GSErrCode			err;
	bool				negArc = true;
	double				a, b, c;
	short				sign;
	Int32				index;

	BNZeroMemory (&rubberLine, sizeof (API_RubberLineType));

	rubberLine.lineThick = APIRubberLine_Simple;

	theCoords = nullptr;
	nItems = 0;
	crossWidth = isLayout ? 0.005 : defColumnThick / 2.0;
	baseCoord == nullptr ? (index = nBase + 1) : (index = nBase);

	if (gPolyMethod == APIPolyGetMethod_Rect && baseCoord != nullptr) {
		endC = info->actCoord;
		endC.y = (*baseCoord)[0].y;
		AddPolyNodeCallBack (&endC);

		endC = info->actCoord;
		AddPolyNodeCallBack (&endC);

		endC = info->actCoord;
		endC.x = (*baseCoord)[0].x;
		AddPolyNodeCallBack (&endC);

		AddPolyNodeCallBack (&(*baseCoord)[0]);
		index = nBase - 1;
	}

	if (gPolyMethod == APIPolyGetMethod_RotRect && nBase == 2) {
		endC = info->actCoord;
		AddPolyNodeCallBack (&endC);

		endC = info->actCoord;
		endC.x += (*baseCoord)[0].x - (*baseCoord)[1].x;
		endC.y += (*baseCoord)[0].y - (*baseCoord)[1].y;
		AddPolyNodeCallBack (&endC);

		AddPolyNodeCallBack (&(*baseCoord)[0]);
		index = nBase - 1;
	}

	for (i = 0; i < index; i++) {

		if (baseCoord == nullptr) {
			begC = info->actCoord;
			endC = info->actCoord;
		} else {
			begC = (*baseCoord)[i];
			if (i < nBase - 1)
				endC = (*baseCoord)[i+1];
			else
				endC = info->actCoord;
		}

		if (origoCoord != nullptr) {				//circle case
			a = begC.y - origoCoord->y;
			if (fabs (a) <= EPS)
				a = 0.0;
			b = origoCoord->x - begC.x;
			if (fabs (b) <= EPS)
				b = 0.0;
			c = -1 * (a * begC.x + b * begC.y); // ax + by = ax0 + by0

			if (b > 0) {
				a *= -1;
				b *= -1;
				c *= -1;
				sign = 1;
			} else if ((b != 0) && (a > 0)) {
				a *= -1;
				b *= -1;
				c *= -1;
				sign = 1;
			} else
				sign = 0;

			negArc = true;
			double temp = a * endC.x + b * endC.y + c;
			if (sign) {
				if (temp >= 0)
					negArc = false;
			} else	{
				if (temp <= 0)
					negArc = false;
			}

			API_Vector		vect;
			double			length;

			vect.x = endC.x - begC.x;
			vect.y = endC.y - begC.y;
			length = LenVector (&vect);
			if (length < actSpacing / 2.0 + EPS && negArc != actualNegArc)
				actualNegArc = negArc;
			else
				negArc = actualNegArc;
		}


		err = CalculateFenceCoords (begC, endC, origoCoord, negArc, actSpacing, &theCoords, nullptr, &nItems);
		if (err == NoError) {
			for (ind = 0; ind < nItems; ind++) {
				actCoord = (*theCoords) [ind];
				sinAngle = crossWidth * sin (actCoord.z + PI / 4.0);
				cosAngle = crossWidth * cos (actCoord.z + PI / 4.0);

				rubberLine.c1.x = actCoord.x - cosAngle;
				rubberLine.c1.y = actCoord.y - sinAngle;
				rubberLine.c1.z = actCoord.z;
				rubberLine.c2.x = actCoord.x + cosAngle;
				rubberLine.c2.y = actCoord.y + sinAngle;
				rubberLine.c2.z = actCoord.z;
				ACAPI_UserInput_DrawRubberLine (&rubberLine);

				rubberLine.c1.x = actCoord.x + sinAngle;
				rubberLine.c1.y = actCoord.y - cosAngle;
				rubberLine.c1.z = actCoord.z;
				rubberLine.c2.x = actCoord.x - sinAngle;
				rubberLine.c2.y = actCoord.y + cosAngle;
				rubberLine.c2.z = actCoord.z;
				ACAPI_UserInput_DrawRubberLine (&rubberLine);
			}
		}

		if (theCoords != nullptr)
			BMKillHandle ((GSHandle *) &theCoords);
	}

	if (gPolyMethod == APIPolyGetMethod_Rect && baseCoord != nullptr)
		for (int i = 0; i < 4; i++)
			DelPolyNodeCallBack ();

	if (gPolyMethod == APIPolyGetMethod_RotRect && nBase > 2)
		for (int i = 0; i < 3; i++)
			DelPolyNodeCallBack ();

	return;
}		// RubberLineCallBack

// -----------------------------------------------------------------------------
// Callback function to get 3D data
// -----------------------------------------------------------------------------

static void Get3DComponentCallBack (const API_Get3DComponentType *info)
{
	if (info != nullptr && info->guid != APINULLGuid) {
		char msg[1024];
		sprintf (msg, "guid: %s\ndbElemIdx: %ld\nbodyIdx: %ld\nipgon: %ld\nc.x: %f\nc.y: %f\nc.z: %f\nnormal.x: %f\nnormal.y: %f\nnormal.z: %f\n",
			(((APIGuid2GSGuid (info->guid)).ToUniString ()).ToCStr ()).Get (),
			(GS::LongForStdio) info->dbElemIdx, (GS::LongForStdio) info->bodyIdx, (GS::LongForStdio) info->ipgon,
			info->c3.x, info->c3.y, info->c3.z,
			info->normal.x, info->normal.y, info->normal.z);
		ACAPI_WriteReport (msg, false);
	}
}		// Get3DComponentCallBack

// -----------------------------------------------------------------------------
// Input a point
// -----------------------------------------------------------------------------

static GSErrCode	Do_InputAPoint (void)
{
	API_GetPointType		pointInfo;
	RubberLineInfoProc		*rubberProc;
	API_Coord3D				**theCoords;
	double					**rotAngles;
	Int32					nItems;
	char					prompt[256];
	GSErrCode				err;


	BNZeroMemory (&pointInfo, sizeof (API_GetPointType));

	GetStringFromResource (prompt, 32601, 1);						// "Click a point"
	CHTruncate (prompt, pointInfo.prompt, sizeof (pointInfo.prompt));
	if (cntlDlgData.filterIsOn) {
		pointInfo.changeFilter = true;
		SetFilterForInput (&pointInfo.filter);						// set sensitivity only for "line" types

		pointInfo.changeCursorSet = true;
		SetCursorSetForInput (&pointInfo.cursorSet, 1);				// my hammer cursor set
	}

	pointInfo.enableQuickSelection = true;

	rubberProc			= cntlDlgData.rubberIsOn ? RubberLineCallBack : nullptr;

	API_WindowInfo windowInfo;
	memset (&windowInfo, 0, sizeof (windowInfo));
	ACAPI_Window_GetCurrentWindow (&windowInfo);

	if (windowInfo.typeID == APIWind_3DModelID) {
		err = ACAPI_UserInput_GetPoint (&pointInfo,
							rubberProc,
							&Get3DComponentCallBack);
	} else {
		err = ACAPI_UserInput_GetPoint (&pointInfo,
							rubberProc);
	}

	if (err == NoError) {														// calculate and create columns
		nItems = 0;
		err = CalculateFenceCoords (pointInfo.pos, pointInfo.pos, nullptr, false, actSpacing, &theCoords, &rotAngles, &nItems);
		if (err == NoError) {
			err = ACAPI_CallUndoableCommand ("Put a Column",
				[&] () -> GSErrCode {
					return CreateTheFence (theCoords, rotAngles, nItems, false);
				});
		BMKillHandle ((GSHandle *) &theCoords);
		BMKillHandle ((GSHandle *) &rotAngles);
		}


		if (err != NoError)
			ACAPI_WriteReport ("Cannot create Columns.", false);
	}

	return err;
}		// Do_InputAPoint


// -----------------------------------------------------------------------------
// Input a line
// -----------------------------------------------------------------------------

static GSErrCode	Do_InputALine (void)
{
	API_GetPointType	pointInfo;
	API_GetLineType		lineInfo;
	RubberLineInfoProc	*rubberProc;
	API_Coord3D			**theCoords;
	double				**rotAngles;
	Int32				nItems;
	char				prompt[256];
	GSErrCode			err;

	BNZeroMemory (&pointInfo, sizeof (API_GetPointType));
	BNZeroMemory (&lineInfo, sizeof (API_GetLineType));

	GetStringFromResource (prompt, 32601, 2);						// "Click the first point"
	CHTruncate (prompt, pointInfo.prompt, sizeof (pointInfo.prompt));
	pointInfo.changeFilter = false;
	pointInfo.changeCursorSet = false;								// use default cursor shapes

	rubberProc = cntlDlgData.rubberIsOn ? RubberLineCallBack : nullptr; // draw X before the first click
	err = ACAPI_UserInput_GetPoint (&pointInfo,rubberProc);

	if (err == NoError) {
		GetStringFromResource (prompt, 32601, 3);					// "Click the second point"
		CHTruncate (prompt, lineInfo.prompt, sizeof (lineInfo.prompt));
		if (cntlDlgData.filterIsOn) {
			lineInfo.changeFilter = true;
			SetFilterForInput (&lineInfo.filter);					// set sensitivity only for "line" types

			lineInfo.changeCursorSet = true;
			SetCursorSetForInput (&lineInfo.cursorSet, 2);			// my handy cursor set
		}

		lineInfo.startCoord = pointInfo.pos;						// line starts with the clicked point
		lineInfo.disableDefaultFeedback = cntlDlgData.disableDefaultFeedback;	// do not draw the default thick rubber line
		AddPolyNodeCallBack (&lineInfo.startCoord);

		rubberProc = cntlDlgData.rubberIsOn ? RubberLineCallBack : nullptr;

		err = ACAPI_UserInput_GetLine (&lineInfo,rubberProc);

		BMKillHandle ((GSHandle *) &baseCoord);
	}

	if (err == NoError) {											// calculate and create columns
		nItems = 0;
		err = CalculateFenceCoords (lineInfo.startCoord, lineInfo.pos, nullptr, false, actSpacing, &theCoords, &rotAngles, &nItems);
		if (err == NoError) {
			err = ACAPI_CallUndoableCommand ("Put a Line of Columns",
				[&] () -> GSErrCode {
					return CreateTheFence (theCoords, rotAngles, nItems, false);
				});
		}
		BMKillHandle ((GSHandle *) &theCoords);
		BMKillHandle ((GSHandle *) &rotAngles);

		if (err != NoError)
			ACAPI_WriteReport ("Cannot create Columns.", false);
	}

	return err;
}		// Do_InputALine


// -----------------------------------------------------------------------------
// Input an arc
// -----------------------------------------------------------------------------

static GSErrCode	Do_InputAnArc (void)
{
	API_GetPointType	pointInfo;
	API_GetLineType		lineInfo;
	API_GetArcType		arcInfo;
	RubberLineInfoProc	*rubberProc;
	API_Coord3D			**theCoords;
	double				**rotAngles;
	Int32				nItems;
	char				prompt[256];
	GSErrCode			err;

	BNZeroMemory (&pointInfo, sizeof (API_GetPointType));
	BNZeroMemory (&lineInfo, sizeof (API_GetLineType));
	BNZeroMemory (&arcInfo, sizeof (API_GetArcType));

	GetStringFromResource (prompt, 32601, 4);						// "Click the arc center point"
	CHTruncate (prompt, pointInfo.prompt, sizeof (pointInfo.prompt));
	if (cntlDlgData.filterIsOn) {
		pointInfo.changeFilter = true;
		SetFilterForInput (&pointInfo.filter);						// set sensitivity only for "line" types

		pointInfo.changeCursorSet = true;
		SetCursorSetForInput (&pointInfo.cursorSet, 3);				// my centerpoint cursor set
	}


	err = ACAPI_UserInput_GetPoint (&pointInfo);

	if (err == NoError) {
		GetStringFromResource (prompt, 32601, 5);					// "Enter the arc start point"
		CHTruncate (prompt, lineInfo.prompt, sizeof (lineInfo.prompt));
		if (cntlDlgData.filterIsOn) {
			lineInfo.changeFilter = true;
			SetFilterForInput (&lineInfo.filter);					// set sensitivity only for "line" types

			lineInfo.changeCursorSet = true;
			SetCursorSetForInput (&lineInfo.cursorSet, 2);			// my handy cursor set
		}

		lineInfo.startCoord = pointInfo.pos;						// line starts with the clicked point
		lineInfo.disableDefaultFeedback = cntlDlgData.disableDefaultFeedback;	// do not draw the default thick rubber line

		err = ACAPI_UserInput_GetLine (&lineInfo);
	}

	if (err == NoError) {
		GetStringFromResource (prompt, 32601, 6);					// "Enter the arc end point"
		CHTruncate (prompt, arcInfo.prompt, sizeof (arcInfo.prompt));
		if (cntlDlgData.filterIsOn) {
			arcInfo.changeCursorSet = true;
			SetCursorSetForInput (&arcInfo.cursorSet, 4);			// my eye cursor set
		}

		arcInfo.origo = lineInfo.startCoord;						// set arc origo
		arcInfo.startCoord = lineInfo.pos;							// arc starts with the second clicked point
		arcInfo.startCoordGiven = true;
		arcInfo.disableDefaultFeedback = cntlDlgData.disableDefaultFeedback;	// do not draw the default thick rubber line
		origoCoord = &arcInfo.origo;
		AddPolyNodeCallBack (&arcInfo.startCoord);

		rubberProc = cntlDlgData.rubberIsOn ? RubberLineCallBack : nullptr;

		err = ACAPI_UserInput_GetArc (&arcInfo,rubberProc);

		BMKillHandle ((GSHandle *) &baseCoord);
	}

	if (err == NoError) {											// calculate and create columns
		nItems = 0;
		err = CalculateFenceCoords (arcInfo.startCoord, arcInfo.pos, &arcInfo.origo, false/*arcInfo.negArc*/, actSpacing, &theCoords, &rotAngles, &nItems);
		if (err == NoError) {
			err = ACAPI_CallUndoableCommand ("Put an Arc of Columns",
				[&] () -> GSErrCode {
					return CreateTheFence (theCoords, rotAngles, nItems, false);
				});
		}
		BMKillHandle ((GSHandle *) &theCoords);
		BMKillHandle ((GSHandle *) &rotAngles);

		if (err != NoError)
			ACAPI_WriteReport ("Cannot create Columns.", false);
	}

	return err;
}		// Do_InputAnArc


// -----------------------------------------------------------------------------
// Input a polygon
// -----------------------------------------------------------------------------

static GSErrCode	Do_InputAPolygon (API_PolyGetMethod polyMethod)
{
	API_GetPointType		pointInfo;
	API_GetPolyType			polyInfo;
	API_RubberPolyCallbacks	cbProcs;
	API_Coord3D				begCoord, endCoord;
	API_Coord3D				**theCoords;
	double					**rotAngles;
	RubberLineInfoProc		*rubberProc;
	Int32					nItems, ind, endInd;
	char					prompt[256];
	GSErrCode				err;

	gPolyMethod = polyMethod;

	BNZeroMemory (&pointInfo, sizeof (API_GetPointType));
	BNZeroMemory (&polyInfo, sizeof (API_GetPolyType));

	GetStringFromResource (prompt, 32601, 7);						// "Click the first node of the polygon"
	CHTruncate (prompt, pointInfo.prompt, sizeof (pointInfo.prompt));
	if (cntlDlgData.filterIsOn) {
		pointInfo.changeFilter = true;
		SetFilterForInput (&pointInfo.filter);						// set sensitivity only for "line" types

		pointInfo.changeCursorSet = true;
		SetCursorSetForInput (&pointInfo.cursorSet, 1);				// my hammer cursor set
	}

	rubberProc = cntlDlgData.rubberIsOn ? RubberLineCallBack : nullptr; // draw X before the first click
	err = ACAPI_UserInput_GetPoint (&pointInfo,rubberProc);

	if (err == NoError) {
		polyInfo.method = polyMethod;
		GetStringFromResource (prompt, 32601, 8);					// "Enter the polygon nodes"
		CHTruncate (prompt, polyInfo.prompt, sizeof (polyInfo.prompt));
		if (cntlDlgData.filterIsOn) {
			polyInfo.changeCursorSet = true;
			SetCursorSetForInput (&polyInfo.cursorSet, 0);			// use default cursors
		}

		polyInfo.getZCoords = true;									// retrieve Z coordinate values as well

		polyInfo.startCoord = pointInfo.pos;						// polygon starts with the clicked point
		polyInfo.disableDefaultFeedback = cntlDlgData.disableDefaultFeedback;
		AddPolyNodeCallBack (&pointInfo.pos);

		BNZeroMemory (&cbProcs, sizeof (cbProcs));
		if (cntlDlgData.rubberIsOn) {
			cbProcs.rubberLineInfoProc = (GSPtr *) (GS::IntPtr) RubberLineCallBack;
			cbProcs.addPolyNodeProc = (GSPtr *) (GS::IntPtr) AddPolyNodeCallBack;
			cbProcs.delPolyNodeProc = (GSPtr *) (GS::IntPtr) DelPolyNodeCallBack;
			err = ACAPI_UserInput_GetPoly (&polyInfo, &cbProcs);
		} else
			err = ACAPI_UserInput_GetPoly (&polyInfo);

		BMKillHandle ((GSHandle *) &baseCoord);
		baseCoord = nullptr;
		nBase = 0;
	}

	if (err == NoError) {											// calculate and create columns
		err = ACAPI_CallUndoableCommand ("Put a Polygon of Columns",
			[&] () -> GSErrCode {
				GSErrCode err1 = NoError;

				endInd = polyInfo.nCoords;
				for (ind = 1; ind < endInd && err1 == NoError; ind++) {
					begCoord.x = (*polyInfo.coords) [ind].x;
					begCoord.y = (*polyInfo.coords) [ind].y;
					begCoord.z = (polyInfo.zCoords != nullptr) ? (*polyInfo.zCoords) [ind] : 0.0;

					endCoord.x = (*polyInfo.coords) [ind + 1].x;
					endCoord.y = (*polyInfo.coords) [ind + 1].y;
					endCoord.z = (polyInfo.zCoords != nullptr) ? (*polyInfo.zCoords) [ind + 1] : 0.0;

					nItems = 0;
					err1 = CalculateFenceCoords (begCoord, endCoord, nullptr, false, actSpacing, &theCoords, &rotAngles, &nItems);
					if (err1 == NoError)
						err1 = CreateTheFence (theCoords, rotAngles, nItems, true);

					BMKillHandle ((GSHandle *) &theCoords);
					BMKillHandle ((GSHandle *) &rotAngles);
				}

				return err1;
			});

		if (err != NoError)
			ACAPI_WriteReport ("Cannot create Columns.", false);
	}

	BMKillHandle ((GSHandle *) &polyInfo.coords);
	BMKillHandle ((GSHandle *) &polyInfo.parcs);
	BMKillHandle ((GSHandle *) &polyInfo.zCoords);

	gPolyMethod = APIPolyGetMethod_General;

	return err;
}		// Do_InputAPolygon


// -----------------------------------------------------------------------------
// General input
// -----------------------------------------------------------------------------

static void		Do_TheInput (short inputMode)
{
	API_DatabaseInfo	currentDB;
	GSErrCode			err;
	char				buffer[256];

	WriteReport ("Get input from the user");

	err = NoError;

	nBase = 0;
	baseCoord = nullptr;
	origoCoord = nullptr;

	defColumnThick = GetDefColumnThick ();
	actSpacing = defColumnThick + cntlDlgData.defSpacing;

	ACAPI_Database_GetCurrentDatabase (&currentDB);
	isModel = (currentDB.typeID == APIWind_FloorPlanID || currentDB.typeID == APIWind_3DModelID);
	isLayout = (currentDB.typeID == APIWind_LayoutID || currentDB.typeID == APIWind_MasterLayoutID);
	if (isLayout)
		actSpacing /= 10.0;

	cntlDlgData.inMyInput = true;

	switch (inputMode) {
		case Dial_GetAPoint:		err = Do_InputAPoint ();
									break;
		case Dial_GetALine:			err = Do_InputALine ();
									break;
		case Dial_GetAnArc:			err = Do_InputAnArc ();
									break;
		case Dial_GetARect:			err = Do_InputAPolygon (APIPolyGetMethod_Rect);
									break;
		case Dial_GetARotRect:		err = Do_InputAPolygon (APIPolyGetMethod_RotRect);
									break;
		case Dial_GetAPoly:			err = Do_InputAPolygon (APIPolyGetMethod_Polyline);
									break;
		default:
									break;
	}

	cntlDlgData.inMyInput = false;

	if (err != NoError) {
		if (err == APIERR_CANCEL) {
			GetStringFromResource (buffer, 32602, 1);
			WriteReport (buffer);
		} else {
			GetStringFromResource (buffer, 32602, 2);
			ErrorBeep (buffer, err);
		}
	}

	return;
}		// Do_TheInput


// -----------------------------------------------------------------------------
// Set input type
// -----------------------------------------------------------------------------

static void		Do_SetInputType (bool circleBased)
{
	cntlDlgData.circleBased = circleBased;
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID))
		DGSetItemValLong (cntlDlgData.dialID, (short) (cntlDlgData.circleBased ? Dial_TypeCylind : Dial_TypeRect), 1);
	CheckACMenuItem (Menu_TypeItem, cntlDlgData.circleBased);

	return;
}		// Do_SetInputType


// -----------------------------------------------------------------------------
// Enable/disable rubber line drawing
// -----------------------------------------------------------------------------

static void		Do_SwitchRubberLineMode (bool isEnabled)
{
	cntlDlgData.rubberIsOn = isEnabled;
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID))
		DGSetItemValLong (cntlDlgData.dialID, Dial_RubberLine, (cntlDlgData.rubberIsOn ? 1 : 0));
	CheckACMenuItem (Menu_RubberLine, cntlDlgData.rubberIsOn);

	return;
}		// Do_SwitchRubberLineMode


// -----------------------------------------------------------------------------
// Enable/disable default feedback function
// -----------------------------------------------------------------------------

static void		Do_SwitchFeedBack (bool isEnabled)
{
	cntlDlgData.disableDefaultFeedback = isEnabled;
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID))
		DGSetItemValLong (cntlDlgData.dialID, Dial_FeedBack, (cntlDlgData.disableDefaultFeedback ? 0 : 1));
	CheckACMenuItem (Menu_FeedBack, !cntlDlgData.disableDefaultFeedback);

	return;
}		// Do_SwitchFeedBack


// -----------------------------------------------------------------------------
// Set cursor sensitivity to line status only
// -----------------------------------------------------------------------------

static void		Do_SetCursorSensitivity (bool isEnabled)
{
	cntlDlgData.filterIsOn = isEnabled;
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID))
		DGSetItemValLong (cntlDlgData.dialID, Dial_Sensitive, (cntlDlgData.filterIsOn ? 1 : 0));
	CheckACMenuItem (Menu_Sensitive, cntlDlgData.filterIsOn);

	return;
}		// Do_SetCursorSensitivity


// -----------------------------------------------------------------------------
// Enable/Disable 3DCutaway
// -----------------------------------------------------------------------------

static void		Do_Switch3DCutaway ()
{
	ACAPI::Result<ACAPI::Cutaway::CuttingPlane> cuttingPlane = ACAPI::Cutaway::GetCuttingPlane ();
	if (!cuttingPlane.IsOk ()) {
		ACAPI_WriteReport ("ACAPI::Cutaway::GetCuttingPlane () failed!", true);
		return;
	}

	ACAPI::Result<bool> status = cuttingPlane->Is3DCutawayEnabled ();
	if (!status.IsOk ()) {
		ACAPI_WriteReport ("APIIo_Is3DCutawayEnabledID failed!", true);
	} else {
		ACAPI::Result<void> result = cuttingPlane->Set3DCutawayStatus (!status.Unwrap ());
		if (!result.IsOk ())
			ACAPI_WriteReport ("APIIo_Set3DCutawayStatusID failed!", true);
	}
}		// Do_Switch3DCutaway


// -----------------------------------------------------------------------------
// Show/Hide Cutting Planes
// -----------------------------------------------------------------------------

static void		Do_SwitchCuttingPlanes ()
{

	ACAPI::Result<ACAPI::Cutaway::CuttingPlane> cuttingPlane = ACAPI::Cutaway::GetCuttingPlane ();
	if (!cuttingPlane.IsOk ()) {
		ACAPI_WriteReport ("ACAPI::GetCuttingPlane () failed!", true);
		return;
	}

	ACAPI::Result<bool> visible = cuttingPlane->AreCuttingPlanesVisible ();
	if (!visible.IsOk ()) {
		ACAPI_WriteReport ("ACAPI::Cutaway::CuttingPlane::AreCuttingPlanesVisible () failed!", true);
		return;
	}

	ACAPI::Result<void> result = cuttingPlane->SetCuttingPlanesVisibility (!visible.Unwrap ());
	if (!result.IsOk ()) {
		ACAPI_WriteReport ("ACAPI::Cutaway::CuttingPlane::SetCuttingPlanesVisibility () failed!", true);
	}
}		// Do_SwitchCuttingPlanes



// =============================================================================
//
// Performing user input with pet palette
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Pet Palette input callback
// -----------------------------------------------------------------------------

static GSErrCode PetPaletteCallBack (short actValue)
{
	actValue++;
	Do_TheInput (actValue);			// should be elaborated more...

	return NoError;
}		// PetPaletteCallBack


// -----------------------------------------------------------------------------
// Initialize pet palette input
// -----------------------------------------------------------------------------

static void		Do_InputWithPetPalette (void)
{
	API_PetPaletteType	petPaletteInfo;
	short**				petItemIdsHdl;
	short				petItemIds[5] = { 32201, 32202, 32203, 32204, 32205 };

    BNZeroMemory (&petPaletteInfo, sizeof (API_PetPaletteType));

	// Constructing the handle, which contains the pet item icons' resource IDs
	short nIcons = sizeof (petItemIds) / sizeof (short);
	petItemIdsHdl = (short**) BMhAll (nIcons * sizeof (short));
	for (short i = 0; i < nIcons; i++)
		(*petItemIdsHdl)[i] = petItemIds[i];

	// Filling petPaletteInfo
	petPaletteInfo.petPaletteID = 32200;
	petPaletteInfo.nCols = 5;
	petPaletteInfo.nRows = 1;
	petPaletteInfo.value = 0;
	petPaletteInfo.grayBits = 0;
	petPaletteInfo.petIconIDsHdl = petItemIdsHdl;
	petPaletteInfo.dhlpResourceID = 32200;
	petPaletteInfo.petIconResModule = ACAPI_GetOwnResModule ();

	ACAPI_Dialog_PetPalette (&petPaletteInfo,PetPaletteCallBack);

	BMhKill ((GSHandle *) &petItemIdsHdl);

	return;
}		// Do_InputWithPetPalette


// -----------------------------------------------------------------------------
// Show SSAForward Dialog
// -----------------------------------------------------------------------------

static void		Do_ShowForwardDialog (void)
{
	ACAPI_Licensing_InvokeForwardDialog ();
}		// Do_ShowForwardDialog


// =============================================================================
//
// Managing a modeless palette
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Enable/disable all controls on the palette
//
// It is recommended to disable the palette's user interface during input.
// While ARCHICAD is in input mode (e.g. editing or creating elements), the API is filtering the
// messages that DG would send to the add-on's palette. The add-on is notified about this with
// the DG_MSG_API_ENABLEPALETTE message to let it disable its changeable dialog items itself.
// During input the DG_MSG_CLICK, DG_MSG_DOUBLECLICK, DG_MSG_CHANGE messages will not be sent,
// though the dialog item can be changed by the user, if the item is enabled. To avoid inconsistent
// status of the user interface, the necessary items should be disabled for that time.
// -----------------------------------------------------------------------------

static void		EnablePaletteControls (bool enable)
{
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID)) {
		if (enable)
			DGEnableItem (cntlDlgData.dialID, DG_ALL_ITEMS);
		else
			DGDisableItem (cntlDlgData.dialID, DG_ALL_ITEMS);
	}

	return;
}		// EnablePaletteControls


// -----------------------------------------------------------------------------
// Adjust controls during input
//
// The same as above; except when the current input is owned by an addon which has an open palette.
// It may happen, that some control on a modeless palette is created to provide input
// modifiers. If all palettes are disabled during input, this functionality
// cannot be achieved.
// That addon which owns the actual input gets the DG_MSG_API_ININPUTPALETTE message instead of
// the DG_MSG_API_ENABLEPALETTE. When this happens the addon has to disable all controls
// on the palettes, except those which are used to modify the input behaviour.
// -----------------------------------------------------------------------------

static void		DisableInputControls (void)
{
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID)) {
		DGDisableItem (cntlDlgData.dialID, Dial_GetAPoint);
		DGDisableItem (cntlDlgData.dialID, Dial_GetALine);
		DGDisableItem (cntlDlgData.dialID, Dial_GetAnArc);
		DGDisableItem (cntlDlgData.dialID, Dial_GetARect);
		DGDisableItem (cntlDlgData.dialID, Dial_GetARotRect);
		DGDisableItem (cntlDlgData.dialID, Dial_GetAPoly);
		DGDisableItem (cntlDlgData.dialID, Dial_RubberLine);
		DGDisableItem (cntlDlgData.dialID, Dial_FeedBack);
		DGDisableItem (cntlDlgData.dialID, Dial_Sensitive);
	}

	return;
}		// DisableInputControls


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
static GSErrCode PaletteAPIControlCallBack (Int32 referenceID, API_PaletteMessageID messageID, GS::IntPtr /*param*/)
{
	if (referenceID == cntlDlgData.dialID) {
		switch (messageID) {
			case APIPalMsg_ClosePalette:		DGModelessClose (cntlDlgData.dialID);
												break;

			case APIPalMsg_HidePalette_Begin:
			case APIPalMsg_HidePalette_End:		Do_PaletteShowHide (false, messageID == APIPalMsg_HidePalette_Begin);
												break;

			case APIPalMsg_DisableItems_Begin:	if (!cntlDlgData.inMyInput)
													EnablePaletteControls (false);
												else
													DisableInputControls ();
												break;
			case APIPalMsg_DisableItems_End:
												EnablePaletteControls (true);
												break;
			case APIPalMsg_OpenPalette:			break;
			default:							break;
		}
	}

	return NoError;
}


// -----------------------------------------------------------------------------
// Choose a Library Part in the object selector dialog
// -----------------------------------------------------------------------------

static void		Do_ChooseLibraryPart (void)
{
	IO::Location	chosenLibPartLocation;
	bool	wasAnyLibPartChosen = false;
	bool	isGSM = true;
	bool	wasExternalEditorUsed;

	ACAPI_Dialog_OpenLibPartFileDialog (&wasAnyLibPartChosen, &chosenLibPartLocation, &isGSM, &wasExternalEditorUsed);

	if (wasAnyLibPartChosen) {
		ACAPI_WriteReport (chosenLibPartLocation.ToDisplayText (), true);
	}

	return;
}		// Do_ChooseLibraryPart


// -----------------------------------------------------------------------------
// Choose a Picture in the texture selector dialog
//	also adds the chosen picture to the embedded library if it has not been
//	embedded previously
// -----------------------------------------------------------------------------

static void		Do_ChoosePicture (void)
{
	IO::Location	chosenPictureLocation;
	bool	wasAnyPictureChosen = false;

	ACAPI_Dialog_OpenPictureDialog (&wasAnyPictureChosen, &chosenPictureLocation);

	if (wasAnyPictureChosen) {
		ACAPI_WriteReport (chosenPictureLocation.ToDisplayText (), true);
	}

	return;
}		// Do_ChoosePicture


// -----------------------------------------------------------------------------
// Choose a Zone Stamp or Label via the object selector dialog
// -----------------------------------------------------------------------------

static void		Do_ChooseZoneStamp (void)
{
	IO::Location	chosenZoneStampLocation;
	bool	wasAnyZoneStampChosen = false;
	bool	isZoneStamp = true;		// set this to 'false' to choose a label

	ACAPI_Dialog_OpenOtherObjectDialog (&wasAnyZoneStampChosen, &chosenZoneStampLocation, &isZoneStamp);

	if (wasAnyZoneStampChosen) {
		ACAPI_WriteReport (chosenZoneStampLocation.ToDisplayText (), true);
	}

	return;
}		// Do_ChooseZoneStamp


// -----------------------------------------------------------------------------
// Callback function for the palette
// -----------------------------------------------------------------------------

static short DGCALLBACK CntlDlgCallBack (short message, short dialID, short item, DGUserData userData, DGMessageData /*msgData*/)
{
	CntlDlgData		*dlgData = (CntlDlgData *) userData;

	switch (message) {
		case DG_MSG_INIT:
			DGSetItemValLong (dialID, (short) (dlgData->circleBased ? Dial_TypeCylind : Dial_TypeRect), 1);
			DGSetItemValLong (dialID, Dial_RubberLine, (dlgData->rubberIsOn) ? 1 : 0);
			DGSetItemValLong (dialID, Dial_FeedBack, (dlgData->disableDefaultFeedback) ? 0 : 1);
			DGSetItemValLong (dialID, Dial_Sensitive, (dlgData->filterIsOn) ? 1 : 0);
			DGSetItemValDouble (dialID, Dial_SpaceValue, dlgData->defSpacing);
			DGSetFocus (dialID, DG_NO_ITEM);

			if (ACAPI_RegisterModelessWindow (dialID, PaletteAPIControlCallBack,
						API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
						API_PalEnabled_InteriorElevation + API_PalEnabled_Detail + API_PalEnabled_Worksheet + API_PalEnabled_3D + API_PalEnabled_Layout) != NoError)
				DBPrintf ("Test:: ACAPI_RegisterModelessWindow failed\n");
			break;

		case DG_MSG_ACTIVATE:
			break;

		case DG_MSG_UPDATE:
			break;

		case DG_MSG_CHANGE:
			switch (item) {

				case Dial_TypeCylind:
						dlgData->circleBased = (DGGetItemValLong (dialID, item) == 1);
						Do_SetInputType (dlgData->circleBased);
						break;

				case Dial_TypeRect:
						dlgData->circleBased = (DGGetItemValLong (dialID, item) != 1);
						Do_SetInputType (dlgData->circleBased);
						break;

				case Dial_RubberLine:
						dlgData->rubberIsOn = (DGGetItemValLong (dialID, item) == 1);
						Do_SwitchRubberLineMode (dlgData->rubberIsOn);
						break;

				case Dial_Sensitive:
						dlgData->filterIsOn = (DGGetItemValLong (dialID, item) == 1);
						Do_SetCursorSensitivity (dlgData->filterIsOn);
						break;

				case Dial_FeedBack:
						dlgData->disableDefaultFeedback = (DGGetItemValLong (dialID, item) != 1);
						Do_SwitchFeedBack (dlgData->disableDefaultFeedback);
						break;

				case Dial_SpaceValue:
						dlgData->defSpacing = DGGetItemValDouble (dialID, item);
						break;
			}
			break;

		case DG_MSG_CLICK:
			switch (item) {
				case Dial_GetAPoint:
				case Dial_GetALine:
				case Dial_GetAnArc:
				case Dial_GetARect:
				case Dial_GetARotRect:
				case Dial_GetAPoly:			Do_TheInput (item);
											break;

				case DG_CLOSEBOX:
						return item;			// this will result in a DG_MSG_CLOSE message
			}
			break;

		case DG_MSG_DOUBLECLICK:
			break;

		case DG_MSG_CLOSE:
			SetPaletteMenuTexts (false, false);
			ACAPI_UnregisterModelessWindow (dlgData->dialID);
			dlgData->dialID = 0;
			break;
	}

	ACAPI_SetPreferences (1, sizeof (CntlDlgData), &cntlDlgData);			// save the dialog settings
	return (0);
}		// CntlDlgCallBack


// -----------------------------------------------------------------------------
// Initialize/open our palette
//   - the resource file must be switched
//   - use ACAPI_ModelessInit instead of DGModelessInit
//   - note that the 'dialID' can be different from 'GDLG' resID;
// Modify the menu item to "Close..."
// -----------------------------------------------------------------------------

static GSErrCode	Do_PaletteInit (void)
{
	GSErrCode		err = NoError;

	if (cntlDlgData.dialID == 0 || !DGIsDialogOpen (cntlDlgData.dialID))
		cntlDlgData.dialID = DGModelessInit (ACAPI_GetOwnResModule (), 32400, ACAPI_GetOwnResModule (), CntlDlgCallBack, (DGUserData) &cntlDlgData, 1);

	if (cntlDlgData.dialID != 0)
		SetPaletteMenuTexts (true, true);
	else
		err = APIERR_GENERAL;

	return err;
}		// Do_PaletteInit


// -----------------------------------------------------------------------------
// Close our palette
// Modify the menu item to "Open..."
// -----------------------------------------------------------------------------

static void		Do_PaletteClose (void)
{
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID))
		DGModelessClose (cntlDlgData.dialID);

	cntlDlgData.dialID = 0;

	return;
}		// Do_PaletteClose


// -----------------------------------------------------------------------------
// Show/hide our palette
// -----------------------------------------------------------------------------

static void	Do_PaletteShowHide (bool byUserFromMenu, bool beginHide)
{
	if (cntlDlgData.dialID != 0 && DGIsDialogOpen (cntlDlgData.dialID)) {
		if (byUserFromMenu) {
			if (DGIsModelessDialogVisible (cntlDlgData.dialID)) {
				DGHideModelessDialog (cntlDlgData.dialID);
				SetPaletteMenuTexts (true, false);
				cntlDlgData.hiddenByUser = true;
			} else if (!cntlDlgData.hiddenByAC) {
				DGShowModelessDialog (cntlDlgData.dialID, DG_DF_FIRST);
				SetPaletteMenuTexts (true, true);
				cntlDlgData.hiddenByUser = false;
			}
		} else {
			if (beginHide) {
				cntlDlgData.hiddenByAC = true;
				if (DGIsModelessDialogVisible (cntlDlgData.dialID)) {
					DGHideModelessDialog (cntlDlgData.dialID);
					SetPaletteMenuTexts (true, false);
				}
			} else {
				cntlDlgData.hiddenByAC = false;
				if (!cntlDlgData.hiddenByUser) {
					DGShowModelessDialog (cntlDlgData.dialID, DG_DF_FIRST);
					SetPaletteMenuTexts (true, true);
				}
			}
		}
	}

	return;
}		// Do_PaletteShowHide

// -----------------------------------------------------------------------------
// Show Selection/All In 3D
// -----------------------------------------------------------------------------

static void Do_ShowIn3D (bool onlySelection)
{
	const GSErrCode err = onlySelection ? ACAPI_View_ShowSelectionIn3D () : ACAPI_View_ShowAllIn3D ();
	if (err != NoError) {
		ACAPI_WriteReport ("Error in %s: %s",
						   true,
						   (onlySelection ? "APIDo_ShowSelectionIn3DID" : "APIDo_ShowAllIn3DID"),
						   ErrID_To_Name (err));
	}
}	 // Do_ShowIn3D


static void Do_CheckSelectionEditable ()
{
	GSErrCode			err;
	API_SelectionInfo selectionInfo;
	GS::Array<API_Neig> selNeigs;
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false);

	if (err || selNeigs.GetSize () == 0) {
		return;
	}

	bool isEditable = true;
	for (UIndex i = 0; i < selNeigs.GetSize (); ++i) {
		isEditable &= ACAPI_Element_Filter (selNeigs[i].guid, APIFilt_IsEditable);
	}

	if (isEditable) {
		ACAPI_WriteReport ("All selected elements are editable", true);
	} else {
		ACAPI_WriteReport ("At least one selected element is not editable", true);
	}
}	 // CheckEditable


namespace {

class SetInputPositionDialog :	public DG::ModalDialog,
								public DG::CompoundItemObserver,
								public DG::ButtonItemObserver,
								public DG::RealEditObserver,
								public DG::CheckItemObserver
{
public:
	SetInputPositionDialog (const API_Coord3D& inputPos) :
		DG::ModalDialog (ACAPI_GetOwnResModule (), 32401, ACAPI_GetOwnResModule ()),
		okButton (GetReference (), OkButtonId),
		cancelButton (GetReference (), CancelButtonId),
		xText (GetReference (), XTextId),
		xRealEdit (GetReference (), XRealEditId),
		yText (GetReference (), YTextId),
		yRealEdit (GetReference (), YRealEditId),
		zText (GetReference (), ZTextId),
		zRealEdit (GetReference (), ZRealEditId),
		applyCheckBox (GetReference (), ApplyCheckBoxId),
		inputPos (inputPos),
		apply (false)
	{
		AttachToAllItems (*this);

		xRealEdit.SetValue (inputPos.x);
		yRealEdit.SetValue (inputPos.y);
		zRealEdit.SetValue (inputPos.z);
	}

	~SetInputPositionDialog ()
	{
		DetachFromAllItems (*this);
	}

	const API_Coord3D& GetNewInputPosition ()
	{
		return inputPos;
	}

	bool WasApplyInputChecked ()
	{
		return apply;
	}

private:
	enum {
		OkButtonId		= 1,
		CancelButtonId	= 2,
		XTextId			= 3,
		XRealEditId		= 4,
		YTextId			= 5,
		YRealEditId		= 6,
		ZTextId			= 7,
		ZRealEditId		= 8,
		ApplyCheckBoxId	= 9
	};

	DG::Button		okButton;
	DG::Button		cancelButton;
	DG::LeftText	xText;
	DG::RealEdit	xRealEdit;
	DG::LeftText	yText;
	DG::RealEdit	yRealEdit;
	DG::LeftText	zText;
	DG::RealEdit	zRealEdit;
	DG::CheckBox	applyCheckBox;

	API_Coord3D		inputPos;
	bool			apply;


	virtual	void	ButtonClicked (const DG::ButtonClickEvent& ev) override
	{
		if (ev.GetSource () == &okButton) {
			PostCloseRequest (DG::ModalDialog::Accept);
		} else if (ev.GetSource () == &cancelButton) {
			PostCloseRequest (DG::ModalDialog::Cancel);
		}
	}

	virtual void	RealEditChanged (const DG::RealEditChangeEvent& ev) override
	{
		if (ev.GetSource () == &xRealEdit) {
			inputPos.x = xRealEdit.GetValue ();
		} else if (ev.GetSource () == &yRealEdit) {
			inputPos.y = yRealEdit.GetValue ();
		} else if (ev.GetSource () == &zRealEdit) {
			inputPos.z = zRealEdit.GetValue ();
		}
	}
	
	virtual void	CheckItemChanged (const DG::CheckItemChangeEvent& ev) override
	{
		if (ev.GetSource () == &applyCheckBox) {
			apply = applyCheckBox.IsChecked ();
		}
	}
};

}


static void Do_SetInputPositionDialog ()
{
	auto inputHandler = ACAPI::CreateInputHandler ();
	if (inputHandler.IsErr ()) {
		ACAPI_WriteReport ("Cannot create input handler", false);
		return;
	}

	auto getResult = inputHandler->GetInputPosition ();
	if (getResult.IsErr ()) {
		ACAPI_WriteReport ("Cannot get input position", false);
		return;
	}

	SetInputPositionDialog dlg (*getResult);
	if (dlg.Invoke ()) {
		auto setResult = inputHandler->SetInputPosition (dlg.GetNewInputPosition (), true);
		if (setResult.IsErr ()) {
			ACAPI_WriteReport ("Cannot set input position", false);
			return;
		}

		if (dlg.WasApplyInputChecked ()) {
			auto applyResult = inputHandler->ApplyInputPosition ();
			if (applyResult.IsErr ()) {
				ACAPI_WriteReport ("Cannot apply input position", false);
				return;
			}
		}
		API_Coord3D temp = *inputHandler->GetInputPosition ();
		WriteReport ("%f, %f, %f\n", temp.x, temp.y, temp.z);
	}
}	 // Do_SetInputPositionDialog


// ============================================================================
// Entry points to handle ARCHICAD events
//
//
// ============================================================================

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	if (firstCall) {
		Int32			version = 0;
		GSSize			nBytes = 0;
		unsigned short	platformSign = GS::Act_Platform_Sign;

		BNZeroMemory (&cntlDlgData, sizeof (CntlDlgData));
		cntlDlgData.rubberIsOn  = true;
		cntlDlgData.circleBased = true;

		ACAPI_Preferences_Platform (&version, &nBytes, nullptr, nullptr);
		if (version == 1 && nBytes == sizeof (CntlDlgData)) {
			ACAPI_Preferences_Platform (&version, &nBytes, &cntlDlgData, &platformSign);
			if (platformSign != GS::Act_Platform_Sign) {
				GS::PlatformSign	inplatform = (GS::PlatformSign) platformSign;
				IVShort (inplatform, &cntlDlgData.dialID);
				IVShort (inplatform, &cntlDlgData.filler_1);
				IVBool  (inplatform, &cntlDlgData.rubberIsOn);
				IVBool  (inplatform, &cntlDlgData.disableDefaultFeedback);
				IVBool  (inplatform, &cntlDlgData.filterIsOn);
				IVBool  (inplatform, &cntlDlgData.circleBased);
				IVDouble (inplatform, &cntlDlgData.defSpacing);
				IVBool  (inplatform, &cntlDlgData.inMyInput);
				IVBool  (inplatform, &cntlDlgData.hiddenByUser);
				IVBool  (inplatform, &cntlDlgData.hiddenByAC);
				IVBool  (inplatform, &cntlDlgData.filler_2);
				IVLong (inplatform, &cntlDlgData.filler_3);
			}

			cntlDlgData.dialID = 0;										// don't use the old dialID - palette is still closed
			SetPaletteMenuTexts (false, false);							// reset the menu items
			CheckACMenuItem (Menu_RubberLine, cntlDlgData.rubberIsOn);
			CheckACMenuItem (Menu_TypeItem, cntlDlgData.circleBased);
			CheckACMenuItem (Menu_Sensitive, cntlDlgData.filterIsOn);
			CheckACMenuItem (Menu_FeedBack, !cntlDlgData.disableDefaultFeedback);
		}
		firstCall = false;
	}

	switch (menuParams->menuItemRef.menuResID) {
		case 32500:
				switch (menuParams->menuItemRef.itemIndex) {
					case Menu_OpenPalette:		if (cntlDlgData.dialID == 0)
													Do_PaletteInit ();
												else
													Do_PaletteClose ();
												break;
					case Menu_ShowPalette:		Do_PaletteShowHide (true, false);
												break;

					case Menu_GetAPoint:		Do_TheInput (Dial_GetAPoint);
												break;
					case Menu_GetALine:			Do_TheInput (Dial_GetALine);
												break;
					case Menu_GetAnArc:			Do_TheInput (Dial_GetAnArc);
												break;
					case Menu_GetARect:			Do_TheInput (Dial_GetARect);
												break;
					case Menu_GetARotRect:		Do_TheInput (Dial_GetARotRect);
												break;
					case Menu_GetAPoly:			Do_TheInput (Dial_GetAPoly);
												break;
					case Menu_Constraint:		applyConstraint = true;
												Do_TheInput (Dial_GetAPoly);
												applyConstraint = false;
												ACAPI_UserInput_ResetConstraint ();
												break;

					case Menu_TypeItem:			Do_SetInputType (!cntlDlgData.circleBased);
												break;
					case Menu_RubberLine:		Do_SwitchRubberLineMode (!cntlDlgData.rubberIsOn);
												break;
					case Menu_Sensitive:		Do_SetCursorSensitivity (!cntlDlgData.filterIsOn);
												break;
					case Menu_FeedBack:			Do_SwitchFeedBack (!cntlDlgData.disableDefaultFeedback);
												break;

					case Menu_3DCutaway:		Do_Switch3DCutaway ();
												break;
					case Menu_CuttingPlanes:	Do_SwitchCuttingPlanes ();
												break;

					case Menu_PetPalette:		Do_InputWithPetPalette ();
												break;

					case Menu_OpenLibPart:		Do_ChooseLibraryPart ();
												break;
					case Menu_OpenPicture:		Do_ChoosePicture ();
												break;
					case Menu_OpenZoneStamp:	Do_ChooseZoneStamp ();
												break;

					case Menu_Forward:			Do_ShowForwardDialog ();
												break;

					case Menu_ShowSelection:	Do_ShowIn3D (true);
												break;
					case Menu_ShowAll:			Do_ShowIn3D (false);
												break;
					case Menu_CheckEditable:	Do_CheckSelectionEditable ();
												break;
					case Menu_SetInputPos:		Do_SetInputPositionDialog ();
												break;
				}
				break;

		case 32501:			// nothing to do; illustrates modifier flags
		case 32502:
		case 32503:
				DBPrintf ("Interface_Functions::MenuCommandHandler called: menu ID %d, menu item %ld\n",
							menuParams->menuItemRef.menuResID, menuParams->menuItemRef.itemIndex);
				break;
	}

	ACAPI_SetPreferences (1, sizeof (CntlDlgData), &cntlDlgData);			// save the dialog settings

	return NoError;
}



// ============================================================================
// Required functions
//
//
// ============================================================================


//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		/* CheckEnvironment */


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	ACAPI_MenuItem_RegisterMenu (32500, 32510, MenuCode_UserDef, MenuFlag_SeparatorBefore);
	ACAPI_MenuItem_RegisterMenu (32501,     0, MenuCode_UserDef, MenuFlag_Default);
	ACAPI_MenuItem_RegisterMenu (32502,     0, MenuCode_UserDef, MenuFlag_InsertIntoSame);
	ACAPI_MenuItem_RegisterMenu (32503,     0, MenuCode_UserDef, MenuFlag_InsertIntoSame);
	return NoError;
}		/* RegisterInterface */


//------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
//------------------------------------------------------
GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Interface_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler (32500) failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32501, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Interface_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler (32501) failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32502, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Interface_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler (32502) failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32503, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Interface_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler (32503) failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32504, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Interface_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler (32504) failed\n");

	err = ACAPI_MenuItem_InstallMenuHandler (32505, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Interface_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler (32505) failed\n");

	return err;
}		/* Initialize */


//------------------------------------------------------
// Called when the Add-On is going to be unloaded
//------------------------------------------------------
GSErrCode FreeData (void)
{
	Do_PaletteClose ();

	return NoError;
}		/* FreeData */
