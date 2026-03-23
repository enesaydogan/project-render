// *****************************************************************************
// Source code for the Element Test Add-On
// *****************************************************************************

#define	_ELEMENT_BASICS_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"BezierDetails.hpp"
#include	"Spline2DData.h"

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"
#include	"ACAPI/ZoneLabelingSettings.hpp"

#include	"Element_Test.h"

#include	"File.hpp"

#include	"uchar_t.hpp"

#include	"Dumper.h"

#include	"GXImageBase.h"
#include	"DGDefs.h"
#include	"DG.h"

#include	"OnExit.hpp"
#include	"VectorImageIterator.hpp"

#include <optional>

// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------



// =============================================================================
//
// Utility functions
//
// =============================================================================

static void		usprintf (GS::uchar_t* dst, Int32 dstsize, const char* fmt, ...)
{
	va_list ap;
	static char	buffer[1024];

	va_start(ap, fmt);
	vsprintf (buffer, fmt, ap);
	va_end(ap);

	GS::UniString tmp (buffer);
	GS::ucsncpy (dst, tmp.ToUStr(), dstsize / sizeof (GS::UniChar) - 1);
}

void Do_RefreshElements() {
  API_Guid			guid;
	if (!ClickAnElem("Click on an object to run its parameter script", API_ZombieElemID, nullptr, nullptr, &guid)) {
		WriteReport_Alert("No object was clicked");
		return;
	}

	API_Element element = {};
	element.header.guid = guid;
	if (ACAPI_Element_Get(&element) == NoError) {
		bool force = true;
		GSErrCode err = ACAPI_Element_RefreshElement(&element.object.head, &force);
		ACAPI_WriteReport ("ACAPI_Element_RefreshElements: " + GS::ValueToUniString (err), true);
	}

	return;
	
}

// -----------------------------------------------------------------------------
// Dump owner information
// -----------------------------------------------------------------------------
static void		DumpOwner (const char 				*info,
						   const API_ProjectInfo	*projectInfo,
						   const API_SharingInfo	*sharingInfo,
						   short 					userId)
{
	char		ownerStr[256];
	bool		found;
	Int32		i;

	if (userId == 0) {
		strcpy (ownerStr, "none");
	} else if (userId == projectInfo->userId) {
		strcpy (ownerStr, "me");
	} else {
		found = false;
		for (i = 0; i < sharingInfo->nUsers; i++) {
			if ((*sharingInfo->users)[i].userId == userId) {
				found = true;
				break;
			}
		}
		if (found) {
			GS::UniString fullName ((*sharingInfo->users)[i].fullName);
			sprintf (ownerStr, "\"%s\" connected: %s", fullName.ToCStr ().Get (), (*sharingInfo->users)[i].connected ? "YES" : "NO");
		} else {
			sprintf (ownerStr, "not found");
		}
	}
	WriteReport ("%s %s", info, ownerStr);

	return;
}		// DumpOwner


// -----------------------------------------------------------------------------
// Compare two elements
// -----------------------------------------------------------------------------
static bool	CompareElems (const API_Element& 		elem1,
						  const API_ElementMemo&	memo1)
{
	GSErrCode	err;

	API_ElementMemo memo2 = {};

	if (elem1.header.guid != APINULLGuid) {
		API_Element elem2 = {};
		elem2.header.guid = elem1.header.guid;

		err = ACAPI_Element_Get (&elem2);
		if (err == NoError)
			err = ACAPI_Element_GetMemo (elem2.header.guid, &memo2);

		if (err != NoError)
			return false;
	}

	bool same = true;

	const Int32 nHandle = sizeof (API_ElementMemo) / sizeof (GSHandle);
	for (Int32 i = 0; i < nHandle; i++) {
		GSConstHandle h1 = ((GSHandle *) &memo1)[i];
		GSConstHandle h2 = ((GSHandle *) &memo2)[i];
		if (h1 == nullptr && h2 == nullptr)
			continue;
		if (h1 == nullptr || h2 == nullptr) {
			same = false;
			DBBREAK ();
			DBPrintf ("CompareElems: memo handles differ: [%d] 0x%08x, %08x", i, h1, h2);
		}
	}

	if (elem1.header.guid != APINULLGuid)
		ACAPI_DisposeElemMemoHdls (&memo2);

	return same;
}		// CompareElems


// -----------------------------------------------------------------------------
// Draw the drawing primitives on the floorplan
// -----------------------------------------------------------------------------
static GSErrCode Draw_ShapePrims (const API_PrimElement*	primElem,
												 const void*			par1,
												 const void*			par2,
												 const void*			par3)
{
	DumpPrimitive (primElem, par1, par2, par3);
	return NoError;
}		// Draw_ShapePrims


static void	ReplaceEmptyTextWithPredefined (API_ElementMemo& memo)
{
	static constexpr std::u16string_view predefinedContent = u"Default text was empty.";

	if (memo.textContent == nullptr || memo.textContent->GetLength () < 2U) {
		delete memo.textContent;
		memo.textContent = new GS::UniString { predefinedContent };
		(*memo.paragraphs)[0].run[0].range = static_cast<Int32> (predefinedContent.length ());
	}
}


GSErrCode	CreateRectangleFill (const API_Coord& 			bottomLeftC,
                                 double 					width,
                                 double 					height,
                                 const API_AttributeIndex&	fillType,
                                 short 						bgPen,
                                 short 						fgPen,
                                 const API_AttributeIndex&	contourLineType,
                                 short 						contourPen)
{
	API_Element		elem = {};
	API_ElementMemo	memo = {};

	elem.header.type = API_HatchID;

	elem.hatch.fillInd	 = fillType;
	elem.hatch.fillBGPen = bgPen;
	elem.hatch.fillPen.penIndex	 = fgPen;
	elem.hatch.ltypeInd	 = contourLineType;
	elem.hatch.contPen.penIndex = contourPen;

	elem.hatch.poly.nCoords	  = 5;
	elem.hatch.poly.nSubPolys = 1;
	elem.hatch.poly.nArcs	  = 0;

	memo.coords = (API_Coord**) BMhAllClear ((elem.hatch.poly.nCoords + 1) * sizeof (API_Coord));
	(*memo.coords)[1].x = bottomLeftC.x;
	(*memo.coords)[1].y = bottomLeftC.y;
	(*memo.coords)[2].x = bottomLeftC.x + width;
	(*memo.coords)[2].y = bottomLeftC.y;
	(*memo.coords)[3].x = bottomLeftC.x + width;
	(*memo.coords)[3].y = bottomLeftC.y + height;
	(*memo.coords)[4].x = bottomLeftC.x;
	(*memo.coords)[4].y = bottomLeftC.y + height;
	(*memo.coords)[5].x = (*memo.coords)[1].x;
	(*memo.coords)[5].y = (*memo.coords)[1].y;

	memo.pends = (Int32**) BMhAllClear ((elem.hatch.poly.nSubPolys + 1) * sizeof (Int32));
	(*memo.pends)[0] = 0;
	(*memo.pends)[1] = elem.hatch.poly.nCoords;

	const GSErrCode err = ACAPI_Element_Create (&elem, &memo);

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}


// =============================================================================
//
// General element functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Count all lines in the project
//	- use filtering support for fast access
//	- only the current database is scanned
// -----------------------------------------------------------------------------
void	Do_CountLines (void)
{
	API_ProjectInfo		projectInfo = {};
	Int32				n;
	GSErrCode			err;

	GS::Array<API_Guid> lineList;
	err = ACAPI_Element_GetElemList (API_LineID, &lineList);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetElemList ()", err);
		return;
	}
	WriteReport ("Number of total lines: %u", (GS::UIntForStdio) lineList.GetSize ());

	if (lineList.IsEmpty ())
		return;

	n = 0;
	for (GS::Array<API_Guid>::ConstIterator it = lineList.Enumerate (); it != nullptr; ++it) {
		if (ACAPI_Element_Filter (*it, APIFilt_IsEditable))
			n++;
	}
	WriteReport ("Number of editable lines: %d", n);

	n = 0;
	for (GS::Array<API_Guid>::ConstIterator it = lineList.Enumerate (); it != nullptr; ++it) {
		if (ACAPI_Element_Filter (*it, APIFilt_OnActFloor))
			n++;
	}
	WriteReport ("Number of lines on the actual floor: %d", n);

	n = 0;
	for (GS::Array<API_Guid>::ConstIterator it = lineList.Enumerate (); it != nullptr; ++it) {
		if (ACAPI_Element_Filter (*it, APIFilt_OnVisLayer | APIFilt_OnActFloor))
			n++;
	}
	WriteReport ("Number of visible lines: %d", n);


	err = ACAPI_ProjectOperation_Project (&projectInfo);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ProjectID", err);
		return;
	}

	if (projectInfo.teamwork) {
		n = 0;
		for (GS::Array<API_Guid>::ConstIterator it = lineList.Enumerate (); it != nullptr; ++it) {
			if (ACAPI_Element_Filter (*it, APIFilt_InMyWorkspace))
				n++;
		}
		WriteReport ("Number of lines in my workspace: %d", n);
	}
}		// Do_CountLines


// -----------------------------------------------------------------------------
// Create a line using the default settings and user input
// -----------------------------------------------------------------------------
void	Do_CreateLine (API_Guid& guid)
{
	API_Coord			c = {};
	API_GetLineType		clickInfo = {};
	API_Element			element = {};
	GSErrCode			err;

	// input the coordinates
	if (!ClickAPoint ("Click the line start point", &c))
		return;

	CHCopyC ("Click the line end point", clickInfo.prompt);

	clickInfo.startCoord.x = c.x;
	clickInfo.startCoord.y = c.y;
	err = ACAPI_UserInput_GetLine (&clickInfo);
	if (err != NoError) {
		ErrorBeep ("APIIo_GetLineID", err);
		return;
	}

	// real work
	element.header.type = API_LineID;
	ACAPI_Element_GetDefaults (&element, nullptr);
		if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Line)", err);
		return;
		}

	element.header.renovationStatus = API_DemolishedStatus;
	element.line.begC.x = clickInfo.startCoord.x;
	element.line.begC.y = clickInfo.startCoord.y;
	element.line.endC.x = clickInfo.pos.x;
	element.line.endC.y = clickInfo.pos.y;
	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (Line)", err);
		return;
	}

	ACAPI_WriteReport ("GUID of the Line: %s", true, APIGuidToString (element.header.guid).ToCStr ().Get ());
	guid = element.header.guid;		// store it for later use

	ACAPI_KeepInMemory (true);

	return;
}		// Do_CreateLine


// -----------------------------------------------------------------------------
// Search for a line by unique ID
// -----------------------------------------------------------------------------
void	Do_GetLineByGuid (const API_Guid& guid)
{
	if (guid == GS::NULLGuid) {
		WriteReport_Alert ("# Please call the \"Create Line\" command before");
		return;
	}

	API_Element element;
	element.header.guid = guid;

	const GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get (line)", err);
		return;
	}

	GS::UniString	renovationFilterName;
	ACAPI_Renovation_GetRenovationFilterName (&element.header.renovationFilterGuid, &renovationFilterName);

	WriteReport ("Line:");
	WriteReport ("  guid: %s", APIGuidToString (element.header.guid).ToCStr ().Get ());
	WriteReport ("  begC: (%lf, %lf)", element.line.begC.x, element.line.begC.y);
	WriteReport ("  endC: (%lf, %lf)", element.line.endC.x, element.line.endC.y);
	if (renovationFilterName.GetLength ())
		WriteReport ("  renovation filter: \"%s\"", (const char*) renovationFilterName.ToCStr ());
}		// Do_GetLineByGuid


// -----------------------------------------------------------------------------
// Create a detail element
// -----------------------------------------------------------------------------
void	Do_CreateDetail (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_SubElement		marker = {};
	API_AddParType		**markAddPars;
	API_Coord			c2;
	GSErrCode			err = NoError;

	element.header.type = API_DetailID;
	marker.subType = (API_SubElementType) (APISubElement_MainMarker | APISubElement_NoParams);

	err = ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker);
	if (err != NoError) {
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	API_LibPart libPart = {};

	err = ACAPI_LibraryPart_GetMarkerParent (element.header.type, libPart);
	if (err != NoError) {
		ErrorBeep ("APIAny_GetMarkerParentID", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	err = ACAPI_LibraryPart_Search (&libPart, false, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_LibraryPart_Search", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	delete libPart.location;

	double a, b;
	Int32 addParNum;
	err = ACAPI_LibraryPart_GetParams (libPart.index, &a, &b, &addParNum, &markAddPars);
	if (err != NoError) {
		ErrorBeep ("ACAPI_LibraryPart_GetParams", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	marker.memo.params = markAddPars;

	if (!ClickAPoint ("Place the Detail", &c2)) {
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	usprintf (element.detail.detailName, sizeof (element.detail.detailName), "Detail_API (%.3f,%.3f)", c2.x, c2.y);
	usprintf (element.detail.detailIdStr, sizeof (element.detail.detailIdStr), "IDstring_API");
	element.detail.linkData.sourceMarker = true;
	element.detail.pos = c2;
	element.detail.poly.nCoords = 5;
	element.detail.poly.nSubPolys = 1;
	element.detail.poly.nArcs = 0;
	memo.coords = (API_Coord**) BMAllocateHandle ((element.detail.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.pends  = (Int32**) BMAllocateHandle ((element.detail.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0);
	if (memo.coords != nullptr && memo.pends != nullptr) {
		(*memo.coords)[1].x = c2.x - 1.0;
		(*memo.coords)[1].y = c2.y;
		(*memo.coords)[2].x = c2.x;
		(*memo.coords)[2].y = c2.y - 1.0;
		(*memo.coords)[3].x = c2.x + 1.0;
		(*memo.coords)[3].y = c2.y;
		(*memo.coords)[4].x = c2.x;
		(*memo.coords)[4].y = c2.y + 1.0;
		(*memo.coords)[5].x = (*memo.coords)[1].x;
		(*memo.coords)[5].y = (*memo.coords)[1].y;

		(*memo.pends)[0] = 0;
		(*memo.pends)[1] = element.detail.poly.nCoords;
	}

	marker.subElem.object.pen = 3;
	marker.subElem.object.useObjPens = true;
	marker.subElem.object.pos.x = c2.x + 1.5;
	marker.subElem.object.pos.y = c2.y + 1.0;

	marker.subType = APISubElement_MainMarker;
	err = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_CreateExt (Detail)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);

	return;
}		// Do_CreateDetail


// -----------------------------------------------------------------------------
// Create a Change Marker element
// -----------------------------------------------------------------------------
void	Do_CreateChangeMarker (void)
{
	API_Element element = {};
	element.header.type = API_ChangeMarkerID;

	API_SubElement	marker = {};
	marker.subType = (API_SubElementType) (APISubElement_MainMarker);

	API_ElementMemo memo = {};

	if (ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker) != NoError) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	API_Coord c2;
	if (!ClickAPoint ("Place the ChangeMarker", &c2)) {
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	usprintf (element.changeMarker.changeId, sizeof (element.changeMarker.changeId), "Test CM ID");
	usprintf (element.changeMarker.changeName, sizeof (element.changeMarker.changeName), "Test CM Name (%.3f, %.3f)", c2.x, c2.y);
	element.changeMarker.linkType = APICMLT_CreateNewChange;

	element.changeMarker.poly.nCoords = 5;
	element.changeMarker.poly.nSubPolys = 1;
	element.changeMarker.poly.nArcs = 0;
	element.changeMarker.pos = c2;

	memo.coords = (API_Coord**) BMAllocateHandle ((element.changeMarker.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.pends  = (Int32**) BMAllocateHandle ((element.changeMarker.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0);

	if (memo.coords != nullptr && memo.pends != nullptr) {
		(*memo.coords)[1].x = c2.x - 1.0;
		(*memo.coords)[1].y = c2.y;
		(*memo.coords)[2].x = c2.x;
		(*memo.coords)[2].y = c2.y - 1.0;
		(*memo.coords)[3].x = c2.x + 1.0;
		(*memo.coords)[3].y = c2.y;
		(*memo.coords)[4].x = c2.x;
		(*memo.coords)[4].y = c2.y + 1.0;
		(*memo.coords)[5].x = (*memo.coords)[1].x;
		(*memo.coords)[5].y = (*memo.coords)[1].y;

		(*memo.pends)[0] = 0;
		(*memo.pends)[1] = element.changeMarker.poly.nCoords;
	}

	marker.subElem.object.pen = 3;
	marker.subElem.object.useObjPens = true;
	marker.subElem.object.pos.x = c2.x + 1.5;
	marker.subElem.object.pos.y = c2.y + 1.0;

	marker.subType = APISubElement_MainMarker;
	const GSErrCode err = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_CreateExt (Change Marker)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);
}


// -----------------------------------------------------------------------------
// Create a static dimension with leaderline
// -----------------------------------------------------------------------------
void	Do_CreateStaticDimension (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	GSErrCode			err = NoError;

	element.header.type = API_DimensionID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (API_DimensionID)", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	element.dimension.dimAppear			= APIApp_Normal;
	element.dimension.textPos			= APIPos_Above;
	element.dimension.textWay			= APIDir_Parallel;
	element.dimension.defStaticDim		= true;
	element.dimension.usedIn3D			= false;
	element.dimension.horizontalText	= false;

	element.dimension.nDimElem			= 4;
	element.dimension.refC.x			= 0.0;
	element.dimension.refC.y			= 10.0;
	element.dimension.direction.x		= 1.0;
	element.dimension.direction.y		= 0.0;

	memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
	if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	double prevX = element.dimension.refC.x;
	for (Int32 i = 0; i < element.dimension.nDimElem; ++i) {
		API_DimElem& dimElem = (*memo.dimElems)[i];

		dimElem.base.loc.x 				= (i == 0) ? (prevX) : (prevX + 5.0 + (i - 1));
		dimElem.base.loc.y				= element.dimension.refC.y - 5.0;
		dimElem.note					= element.dimension.defNote;
		dimElem.witnessVal				= element.dimension.defWitnessVal;
		dimElem.witnessForm				= element.dimension.defWitnessForm;
		dimElem.fixedPos				= true;
		dimElem.pos.x					= dimElem.base.loc.x;
		dimElem.pos.y					= element.dimension.refC.y;

		switch (i) {
			case 1:
				dimElem.note.fixPos 			= false;
				break;

			case 2:
				dimElem.note.fixPos 			= true;
				dimElem.note.pos.x				= dimElem.pos.x - 1.0;
				dimElem.note.pos.y				= dimElem.pos.y + 1.0;
				dimElem.note.noteAngle			= PI / 4.0;
				break;

			case 3:
				dimElem.note.fixPos 			= true;
				dimElem.note.useLeaderLine		= true;
				dimElem.note.anchorAtTextEnd 	= false;
				dimElem.note.begC.x				= dimElem.pos.x - 2.5;
				dimElem.note.begC.y				= dimElem.pos.y;
				dimElem.note.midC.x				= dimElem.note.begC.x + 1.0;
				dimElem.note.midC.y				= dimElem.note.begC.y + 2.0;
				dimElem.note.endC.x				= dimElem.note.midC.x + 5.0;
				dimElem.note.endC.y				= dimElem.note.midC.y;
				break;

			default:
				break;
		}

		prevX = dimElem.base.loc.x;
	}

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (Static Dimension)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
}


void	Do_CreateAssociativeDimensions ()
{
	static const std::unordered_set<API_ElemType> supportedElemTypes = {  API_WallID, API_RailingID };

	class ErrorHandler {};

	class ElemSelection
	{
	public:
		API_Guid elemGuid;
		API_Coord3D clickedPoint3D;
		API_Coord clickedPoint2D;
		API_Element elem;

		bool Create ()
		{
			if (!ClickAnElem ("Click an elem to place dimensions", API_ZombieElemID, nullptr, &elem.header.type, &elemGuid, &clickedPoint3D)) {
				return false;
			}

			if (supportedElemTypes.find (elem.header.type) == supportedElemTypes.end ()) {
				DBBREAK_STR ("Unsupported elem type!");
				return false;
			}

			elem.header.guid = elemGuid;
			if (ACAPI_Element_Get (&elem) != NoError) {
				throw ErrorHandler ();
			}

			clickedPoint2D = { clickedPoint3D.x, clickedPoint3D.y };

			return true;
		}
	};


	class DimensionsToRailing
	{
		ElemSelection& selection;
	public:
		DimensionsToRailing (ElemSelection& selection) : selection (selection) {}

		void Create ()
		{
			GS::Array<API_ElementHotspot> hotspots;
			ACAPI_Element_GetHotspots (selection.elemGuid, &hotspots);

			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			
			element.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}

			element.dimension.dimAppear = APIApp_Normal;
			element.dimension.textPos = APIPos_Above;
			element.dimension.textWay = APIDir_Parallel;
			element.dimension.defStaticDim = false;
			element.dimension.usedIn3D = false;
			element.dimension.horizontalText = false;
			element.dimension.refC = selection.clickedPoint2D;
			element.dimension.direction = { 1, 0 };

			element.dimension.nDimElem = hotspots.GetSize ();
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			static bool fillInIndex = false;

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = selection.elemGuid;

				dimElem.base.base.line = false;
				if (fillInIndex) {
					dimElem.base.base.inIndex = hotspots[dimElemIdx].first.inIndex;
				} else {
					dimElem.base.base.inIndex = 0;
				}
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 0;
				dimElem.base.base.node_status = 0;
				dimElem.base.base.node_id = hotspots[dimElemIdx].first.supplUnId;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}
	};


	class AssociativeDimensionsOnWallThickness
	{
		class ErrorHandler { public : ErrorHandler () { DBBREAK (); } };
	public:
		void Create ()
		{
			try {
				const API_Coord wallBeg { 1, 0 };
				const API_Coord wallEnd { 1, 5 };
				const double wallThickness = 0.3;
				const API_Guid wallGuid = CreateBasicStraightWall (wallBeg, wallEnd, wallThickness);

				const API_Coord dimRefPoint { 1, 6 };
				const API_Vector dimDirection { 1, 0 };	// perpendicular to wall

				CreateDimension (dimRefPoint, dimDirection, wallGuid);

			} catch (ErrorHandler&) {
				ErrorBeep ("Associative Dimension Creation Error", 42);
			}
		}

	private:
		API_Guid CreateBasicStraightWall (const API_Coord& beg, const API_Coord& end, double thickness)
		{
			API_Element element = {};
			element.header.type = API_WallID;
			if (ACAPI_Element_GetDefaults (&element, nullptr) != NoError) {
				throw ErrorHandler ();
			}

			element.wall.begC = beg;
			element.wall.endC = end;
			element.wall.angle = 0.0;
			element.wall.referenceLineLocation = APIWallRefLine_Outside;
			element.wall.flipped = false;
			element.wall.modelElemStructureType = API_BasicStructure;
			element.wall.thickness = thickness;
			element.wall.relativeTopStory = 0;
			element.wall.height = 5.0;

			if (ACAPI_Element_Create (&element, nullptr) != NoError) {
				throw ErrorHandler ();
			}
			return element.header.guid;
		}

		void CreateDimension (const API_Coord& refPoint, const API_Vector& direction, const API_Guid& wallGuid)
		{
			API_Element element = {};
			API_ElementMemo memo = {};

			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });

			element.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
			
			element.dimension.dimAppear = APIApp_Normal;
			element.dimension.textPos = APIPos_Above;
			element.dimension.textWay = APIDir_Parallel;
			element.dimension.defStaticDim = false;
			element.dimension.usedIn3D = false;
			element.dimension.horizontalText = false;
			element.dimension.refC = refPoint;
			element.dimension.direction = direction;
			
			element.dimension.nDimElem = 2;	
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}
			
			// double prevX = element.dimension.refC.x;
			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];
			
				dimElem.base.base.type = API_ElemType (API_WallID);
				dimElem.base.base.line = true;
				dimElem.base.base.inIndex = (dimElemIdx == 0) ? 11 : 21;
				dimElem.base.base.guid = wallGuid;

				dimElem.base.base.special = 0;
				dimElem.base.base.node_id = 0;
				dimElem.base.base.node_status = 0;
				dimElem.base.base.node_typ = 0;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}
			
			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}		
	};


	try {
		ElemSelection selection;
		selection.Create ();
		if (selection.Create ()) {
			switch (selection.elem.header.type.typeID) {
				case API_WallID: break;
				case API_RailingID: DimensionsToRailing (selection).Create (); break;
				default: DBBREAK_STR ("Not supported elem type!"); break;
			}
		}
	} catch (ErrorHandler&) {
		DBBREAK ();
		return;
	}
}


static API_Coord operator+ (const API_Coord& lhs, const API_Coord& rhs)
{
	return { lhs.x + rhs.x, lhs.y + rhs.y };
}


void EnumerateProfileHotspots (const API_AttributeIndex& profileAttrIdx, const std::function<void (const Sy_HotType& hotspot)>& enumerator)
{
	const ProfileVectorImage* profileImage = nullptr;
	{
		API_AttributeDefExt	attrDefs;
		if (ACAPI_Attribute_GetDefExt (API_ProfileID, profileAttrIdx, &attrDefs) != NoError) {
			DBBREAK ();
			return;
		}
		profileImage = attrDefs.profile_vectorImageItems;
		if (profileImage == nullptr) {
			DBBREAK ();
			return;
		}
	}

	for (ConstProfileVectorImageIterator imageIt (*profileImage); !imageIt.IsEOI (); ++imageIt) {
		if (imageIt->item_Typ == SyHots) {
			const Sy_HotType* hotspot = imageIt;
			if (DBVERIFY (hotspot != nullptr)) {
				enumerator (*hotspot);
			}
		}
	}
}


void	Do_CreateAssociativeDimensionsOnSection ()
{
	class ErrorHandler {};

	class SectElemSelection
	{
	public:
		API_Guid sectElemGuid;
		API_Coord3D clickedPoint3D;
		API_Coord clickedPoint2D;
		API_Element sectElem;
		API_Element parentElem;

		bool Create ()
		{
			if (!ClickAnElem ("Click an elem to place dimensions", API_SectElemID, nullptr, &sectElem.header.type, &sectElemGuid, &clickedPoint3D)) {
				return false;
			}

			sectElem.header.type = API_SectElemID;
			sectElem.header.guid = sectElemGuid;
			if (ACAPI_Element_Get (&sectElem) != NoError) {
				throw ErrorHandler ();
			}

			parentElem.header.type = sectElem.sectElem.parentType;
			parentElem.header.guid = sectElem.sectElem.parentGuid;
			if (ACAPI_Element_Get (&parentElem) != NoError) {
				throw ErrorHandler ();
			}

			clickedPoint2D = { clickedPoint3D.x, clickedPoint3D.y };

			return true;
		}
	};

	class DimensionsToWall
	{
		const SectElemSelection& selection;
	public:
		DimensionsToWall (const SectElemSelection& selection) : selection (selection) {}

		void Create ()
		{
			const API_WallType& wall = selection.parentElem.wall;
			if (wall.modelElemStructureType == API_ProfileStructure) {
				CreateToProfileHotspots (wall, selection.sectElemGuid, selection.clickedPoint2D);
			} else {
				CreateToSpecialSkinBorders (selection.sectElemGuid, selection.clickedPoint2D);
				CreateToSkinBorders (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0, 0.5 }, { 1, 3, 4 });
			}
		}

	private:
		void CreateToProfileHotspots (const API_WallType& wall, const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			GS::Array<UIndex> hotspotsToPlaceDimensions;
			{
				UIndex actHotspotIndex = 0;
				auto NeedPlaceDimensionTo = [] (const Sy_HotType&) { return true; };	// customization point
				EnumerateProfileHotspots (wall.profileAttr, [&] (const Sy_HotType& hotspot) {
					if (NeedPlaceDimensionTo (hotspot)) {
						hotspotsToPlaceDimensions.Push (actHotspotIndex);
					}
					++actHotspotIndex;
				});
			}

			// create dimension
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint);

			element.dimension.nDimElem = hotspotsToPlaceDimensions.GetSize ();
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;
				
				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 2100;
				dimElem.base.base.node_status = 0;
				static bool placeToWallBeginPlane = true;
				if (placeToWallBeginPlane) {
					dimElem.base.base.node_id = UINT_MAX - 2 * hotspotsToPlaceDimensions[dimElemIdx];
				} else {
					dimElem.base.base.node_id = UINT_MAX - 2 * hotspotsToPlaceDimensions[dimElemIdx] - 1;
				}

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void CreateToSpecialSkinBorders (const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint);

			element.dimension.nDimElem = 5;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;
				
				dimElem.base.base.line = true;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				switch (dimElemIdx) {
					case 0:	// outer face 1  
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 256;
						break;
					case 1:	// outer face 2
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 1024;
						break;
					case 2:	// core face 1
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 512;
						break;
					case 3:	// core face 2
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 768;
						break;
					case 4:	// reference line
						dimElem.base.base.node_typ = 131;
						dimElem.base.base.node_status = 0;
						break;
				}
				dimElem.base.base.node_id = 0;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}
	
		void CreateToSkinBorders (const API_Guid& sectElemGuid, const API_Coord& refPoint, const GS::Array<Int32>& skinBorderIndices)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint);

			element.dimension.nDimElem = skinBorderIndices.GetSize ();
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;
				
				dimElem.base.base.line = true;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 130;
				dimElem.base.base.node_status = 1280;
				dimElem.base.base.node_id = skinBorderIndices[dimElemIdx];

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void FillDimensionMembers (API_Element& element, API_ElementMemo& memo, const API_Coord& refPoint) const
		{
			element.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}

			element.dimension.dimAppear = APIApp_Normal;
			element.dimension.textPos = APIPos_Above;
			element.dimension.textWay = APIDir_Parallel;
			element.dimension.defStaticDim = false;
			element.dimension.usedIn3D = false;
			element.dimension.horizontalText = false;
			element.dimension.refC = refPoint;
			element.dimension.direction = { 1, 0 };
		}

	};

	class DimensionsToSlab
	{
		const SectElemSelection& selection;
		const API_SlabType& slab;
	public:
		DimensionsToSlab (const SectElemSelection& selection) : 
			selection (selection),
			slab (selection.parentElem.slab)
		{
		}

		void Create ()
		{
			API_ElementMemo memo;
			if (ACAPI_Element_GetMemo (selection.parentElem.header.guid, &memo) != NoError) {
				throw ErrorHandler ();
			}
			const UInt32* vertexIDs = *memo.vertexIDs;
			CreateToVetrices_BasePolygon (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0.0, 0.5 }, vertexIDs);
			CreateToVetrices_RealModel (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0.0, 1.0 }, vertexIDs);
			CreateToSpecialSkinBorders (selection.sectElemGuid, selection.clickedPoint2D);
			CreateToSkinBorders (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0.5, 0.0 }, { 1, 3, 4 });
		}

	private:
		void CreateToVetrices_BasePolygon (const API_Guid& sectElemGuid, const API_Coord& refPoint, const UInt32* vertexIDs)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 0 });

			element.dimension.nDimElem = slab.poly.nCoords;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 2100;		//HOTS_GENERALEDIT
				switch (dimElemIdx % 3) {
					case 0: dimElem.base.base.node_status = 16; break; // HOTS_ZMIN, bottom plane of the slab
					case 1: dimElem.base.base.node_status = 32; break; // HOTS_ZMAX, top plane of the slab
					case 2: dimElem.base.base.node_status = 48; break; // HOTS_ZMID, reference plane of the slab
				}
				dimElem.base.base.node_id = vertexIDs[dimElemIdx];

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}
		
		void CreateToVetrices_RealModel (const API_Guid& sectElemGuid, const API_Coord& refPoint, const UInt32* vertexIDs)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 0 });

			element.dimension.nDimElem = slab.poly.nCoords;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				static bool placeToCoreSkins = true;
				if (placeToCoreSkins) {
					dimElem.base.base.node_typ = 420;	//HOTS_SLAB_CORE
				} else {
					dimElem.base.base.node_typ = 430;	//HOTS_SLAB_PSD
				}
				switch (dimElemIdx % 2) {
					case 0: dimElem.base.base.node_status = 16; break; // HOTS_ZMIN, bottom plane of the slab
					case 1: dimElem.base.base.node_status = 32; break; // HOTS_ZMAX, top plane of the slab
				}
				dimElem.base.base.node_id = vertexIDs[dimElemIdx];

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void CreateToSpecialSkinBorders (const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 0, 1 });

			element.dimension.nDimElem = 5;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = true;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				switch (dimElemIdx) {
					case 0:	// outer face 1  
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 256;
						break;
					case 1:	// outer face 2
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 1024;
						break;
					case 2:	// core face 1
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 512;
						break;
					case 3:	// core face 2
						dimElem.base.base.node_typ = 130;
						dimElem.base.base.node_status = 768;
						break;
					case 4:	// reference line
						dimElem.base.base.node_typ = 131;
						dimElem.base.base.node_status = 0;
						break;
				}
				dimElem.base.base.node_id = 0;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void CreateToSkinBorders (const API_Guid& sectElemGuid, const API_Coord& refPoint, const GS::Array<Int32>& skinBorderIndices)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 0, 1 } );

			element.dimension.nDimElem = skinBorderIndices.GetSize ();
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = true;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 130;
				dimElem.base.base.node_status = 1280;
				dimElem.base.base.node_id = skinBorderIndices[dimElemIdx];

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void FillDimensionMembers (API_Element& element, API_ElementMemo& memo, const API_Coord& refPoint, const API_Vector& direction) const
		{
			element.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}

			element.dimension.dimAppear = APIApp_Normal;
			element.dimension.textPos = APIPos_Above;
			element.dimension.textWay = APIDir_Parallel;
			element.dimension.defStaticDim = false;
			element.dimension.usedIn3D = false;
			element.dimension.horizontalText = false;
			element.dimension.refC = refPoint;
			element.dimension.direction = direction;
		}

	};

	class DimensionsToBeamOrColumn
	{
		const SectElemSelection& selection;
	public:
		DimensionsToBeamOrColumn (const SectElemSelection& selection) :
			selection (selection)
		{
		}

		void Create ()
		{
			CreateToRefLineEndPoints (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0, 1 });
			CreateToBoundingBoxCorners (selection.sectElemGuid, selection.clickedPoint2D);
			CreateToProfileHotspots (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0, 2 }, selection.parentElem);
			//#KiP_Refactor_DIM Beam Hole points are currently missing
		}

	private:
		void CreateToRefLineEndPoints (const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 0 });

			element.dimension.nDimElem = 2;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 0;
				dimElem.base.base.node_status = 0;
				dimElem.base.base.node_id = (dimElemIdx == 0) ? 1049586 /*begin*/ : 2099172 /*end*/;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void CreateToBoundingBoxCorners (const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 2 });

			element.dimension.nDimElem = 9;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 0;
				dimElem.base.base.node_status = 0;
				
				static bool placeOnBeginPlane = true;
				const UInt32 planePart = placeOnBeginPlane ? 4128768 : 8257537;
				
				UInt32 supplUnId = planePart;
				switch (dimElemIdx) {
					case 0: supplUnId += 0;	 break;		// bottom left
					case 1: supplUnId += 12; break;		// left
					case 2: supplUnId += 4;	 break;		// top left
					case 3: supplUnId += 48; break;		// bottom
					case 4: supplUnId += 60; break;		// middle
					case 5: supplUnId += 52; break;		// top
					case 6: supplUnId += 16; break;		// bottom right
					case 7: supplUnId += 28; break;		// right
					case 8: supplUnId += 20; break;		// top right
				}

				static bool planeOnTotalSize = false;
				if (planeOnTotalSize) {
					supplUnId += 2;
				}

				dimElem.base.base.node_id = supplUnId;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void CreateToProfileHotspots (const API_Guid& sectElemGuid, const API_Coord& refPoint, const API_Element& parentElem)
		{
			std::optional<API_AttributeIndex> profileAttr = GetProfileIfParentIsOneSegmentElem (parentElem);
			if (!profileAttr.has_value ()) {
				return;
			}

			GS::Array<GS::Guid> hotspotsToPlaceDimensions;
			EnumerateProfileHotspots (profileAttr.value (), [&] (const Sy_HotType& hotspot) {
				hotspotsToPlaceDimensions.Push (hotspot.GetUniqueId ());
			});

			// create dimension
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 2 });

			element.dimension.nDimElem = hotspotsToPlaceDimensions.GetSize ();
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 0;
				dimElem.base.base.node_status = 0;
				static bool beginPlane = true;
				dimElem.base.base.node_id = GenerateProfileHotspotNodeId (beginPlane, hotspotsToPlaceDimensions[dimElemIdx]);

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		static std::optional<API_AttributeIndex> GetProfileIfParentIsOneSegmentElem (const API_Element& parentElem)
		{
			API_ElementMemo memo {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			if (ACAPI_Element_GetMemo (parentElem.header.guid, &memo) != NoError) {
				DBBREAK ();
				return std::nullopt;
			}

			std::optional<API_AssemblySegmentData> segmentData = std::nullopt;
			{
				if (parentElem.header.type == API_BeamID) {
					if (parentElem.beam.nSegments == 1) {
						segmentData = memo.beamSegments[0].assemblySegmentData;
					}
				} else if (parentElem.header.type == API_ColumnID) {
					if (parentElem.column.nSegments == 1) {
						segmentData = memo.columnSegments[0].assemblySegmentData;
					}
				} 
			}

			if (segmentData.has_value () && segmentData->modelElemStructureType == API_ProfileStructure) {
				return segmentData->profileAttr;
			} else {
				return std::nullopt;
			}
		}

		static UInt32 GenerateProfileHotspotNodeId (bool beginPlane, const GS::Guid& hotspotUniqueId)
		{
			UInt32 result = 0;
			result |= beginPlane ? 0 : 1;
			result |= GS::CalculateHashValue (hotspotUniqueId) << 1;
			result |= (beginPlane ? 65599 : 131198) << 16;
			return result;
		}

		void FillDimensionMembers (API_Element& element, API_ElementMemo& memo, const API_Coord& refPoint, const API_Vector& direction) const
		{
			element.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}

			element.dimension.dimAppear = APIApp_Normal;
			element.dimension.textPos = APIPos_Above;
			element.dimension.textWay = APIDir_Parallel;
			element.dimension.defStaticDim = false;
			element.dimension.usedIn3D = false;
			element.dimension.horizontalText = false;
			element.dimension.refC = refPoint;
			element.dimension.direction = direction;
		}

	};

	class DimensionsToDoorWindow
	{
		const SectElemSelection& selection;
	public:
		DimensionsToDoorWindow (const SectElemSelection& selection) :
			selection (selection)
		{
		}

		void Create ()
		{
			CreateToWallHoleCorners (selection.sectElemGuid, selection.clickedPoint2D);
			CreateToModelHotspots (selection.sectElemGuid, selection.clickedPoint2D + API_Coord { 0, 0.5 });
		}

	private:
		void CreateToWallHoleCorners (const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 0 });

			element.dimension.nDimElem = 4;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			const UInt32 nodeId = 0;
			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 2100;
				dimElem.base.base.node_id = nodeId;
				static bool onDoorWindowTop = false;
				if (onDoorWindowTop) {
					dimElem.base.base.node_status = static_cast<short> (2 + 2 * dimElemIdx + 1);
				} else {
					dimElem.base.base.node_status = static_cast<short> (2 + 2 * dimElemIdx);
				}

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void CreateToModelHotspots (const API_Guid& sectElemGuid, const API_Coord& refPoint)
		{
			API_Element element = {};
			API_ElementMemo memo = {};
			GS::OnExit disposeElemMemoHdls ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
			FillDimensionMembers (element, memo, refPoint, { 1, 0 });

			element.dimension.nDimElem = 2;
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (element.dimension.nDimElem * sizeof (API_DimElem)));
			if (memo.dimElems == nullptr || *memo.dimElems == nullptr) {
				throw ErrorHandler ();
			}

			for (Int32 dimElemIdx = 0; dimElemIdx < element.dimension.nDimElem; ++dimElemIdx) {
				API_DimElem& dimElem = (*memo.dimElems)[dimElemIdx];

				dimElem.base.base.type = API_ElemType (API_SectElemID);
				dimElem.base.base.guid = sectElemGuid;

				dimElem.base.base.line = false;
				dimElem.base.base.inIndex = 0;
				dimElem.base.base.special = 0;
				dimElem.base.base.node_typ = 0;
				dimElem.base.base.node_id = dimElemIdx == 0 ? 11111 : 11113;
				dimElem.base.base.node_status = 0;

				dimElem.note = element.dimension.defNote;
				dimElem.witnessVal = element.dimension.defWitnessVal;
				dimElem.witnessForm = element.dimension.defWitnessForm;
			}

			if (ACAPI_Element_Create (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}
		}

		void FillDimensionMembers (API_Element& element, API_ElementMemo& memo, const API_Coord& refPoint, const API_Vector& direction) const
		{
			element.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&element, &memo) != NoError) {
				throw ErrorHandler ();
			}

			element.dimension.dimAppear = APIApp_Normal;
			element.dimension.textPos = APIPos_Above;
			element.dimension.textWay = APIDir_Parallel;
			element.dimension.defStaticDim = false;
			element.dimension.usedIn3D = false;
			element.dimension.horizontalText = false;
			element.dimension.refC = refPoint;
			element.dimension.direction = direction;
		}
	};


	try {
		SectElemSelection selection;
		if (selection.Create ()) {
			switch (selection.parentElem.header.type.typeID) {
				case API_WallID : DimensionsToWall (selection).Create (); break;
				case API_SlabID: DimensionsToSlab (selection).Create (); break;
				case API_BeamID:
				case API_ColumnID: DimensionsToBeamOrColumn (selection).Create (); break;
				case API_DoorID:
				case API_WindowID: DimensionsToDoorWindow (selection).Create (); break;
				default: DBBREAK_STR ("Not supported elem type!"); break;
			}
		}
	} catch (ErrorHandler&) {
		DBBREAK ();
		return;
	}
}



// -----------------------------------------------------------------------------
// Create a static angle dimension with leaderline
// -----------------------------------------------------------------------------
void	Do_CreateStaticAngleDimension (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	GSErrCode			err = NoError;

	element.header.type = API_AngleDimensionID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Static Angle Dimension)", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	element.angleDimension.textPos				= APIPos_Above;
	element.angleDimension.textWay				= APIDir_Parallel;
	element.angleDimension.smallArc				= true;

	element.angleDimension.base[0].loc.x		= 0.0;
	element.angleDimension.base[0].loc.y		= 0.0;
	element.angleDimension.base[1].loc.x		= 5.0;
	element.angleDimension.base[1].loc.y		= 0.0;
	element.angleDimension.base[2].loc.x		= 0.0;
	element.angleDimension.base[2].loc.y		= 0.0;
	element.angleDimension.base[3].loc.x		= 5.0;
	element.angleDimension.base[3].loc.y		= 5.0;

	element.angleDimension.pos.x				= 5.0 * cos (PI / 8.0);
	element.angleDimension.pos.y				= 5.0 * sin (PI / 8.0);
	element.angleDimension.origo.x				= 0.0;
	element.angleDimension.origo.y				= 0.0;

	element.angleDimension.note.fixPos			= true;
	element.angleDimension.note.useLeaderLine	= true;
	element.angleDimension.note.anchorAtTextEnd = false;
	element.angleDimension.note.begC.x			= element.angleDimension.pos.x;
	element.angleDimension.note.begC.y			= element.angleDimension.pos.y;
	element.angleDimension.note.midC.x			= element.angleDimension.note.begC.x + 1.0;
	element.angleDimension.note.midC.y			= element.angleDimension.note.begC.y + 2.0;
	element.angleDimension.note.endC.x			= element.angleDimension.note.midC.x + 5.0;
	element.angleDimension.note.endC.y			= element.angleDimension.note.midC.y;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (Static Angle Dimension)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
}


typedef enum {
	CHF_None				=	0,
	CHF_API_HatchFlags_Mask	=	0x0F,	//for API_HatchFlags
	CHF_ColoredContPen		=	0x10,
	CHF_ColoredFillPen		=	0x20,
} CreateHatchFlags;

// -----------------------------------------------------------------------------
// Create a hatch with RGB_Pen and/or override colors
// -----------------------------------------------------------------------------
static void	Do_CreateHatch (Int32 hatchFlags, const API_Coord& centerPoint)
{
	API_Element element = {};
	element.header.type = API_HatchID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Fill)", err);
		return;
	}


	element.hatch.contPen.penIndex = 3;
	if (hatchFlags & CHF_ColoredContPen) {
		element.hatch.showArea = true;
		element.hatch.contPen.colorOverridePenIndex = 4;
	}


	element.hatch.fillPen.penIndex = 11;
	if (hatchFlags & CHF_ColoredFillPen) {
		element.hatch.fillPen.colorOverridePenIndex = 12;
	}


	element.hatch.hatchFlags = (hatchFlags & CHF_API_HatchFlags_Mask);
	if (hatchFlags & APIHatch_HasFgRGBColor) {
		element.hatch.foregroundRGB.f_red	= 0.0;
		element.hatch.foregroundRGB.f_green	= 0.9;
		element.hatch.foregroundRGB.f_blue	= 0.9;
	}


	if (hatchFlags & APIHatch_HasBkgRGBColor) {
		element.hatch.backgroundRGB.f_red	= 0.9;
		element.hatch.backgroundRGB.f_green	= 0.9;
		element.hatch.backgroundRGB.f_blue	= 0.0;
	}


	if (hatchFlags & APIHatch_OverrideFgPen) {
		element.hatch.fillPen.penIndex = 21;
//		element.hatch.fillPen.colorOverridePenIndex;
	}


	if (hatchFlags & APIHatch_OverrideBkgPen) {
		element.hatch.fillBGPen = 11;
	}


	element.hatch.poly.nCoords		= 9;
	element.hatch.poly.nSubPolys	= 1;
	element.hatch.poly.nArcs		= 0;

	API_ElementMemo memo = {};

	memo.coords	= reinterpret_cast<API_Coord**>		(BMAllocateHandle ((element.hatch.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.pends	= reinterpret_cast<Int32**>			(BMAllocateHandle ((element.hatch.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.parcs	= reinterpret_cast<API_PolyArc**>	(BMAllocateHandle (element.hatch.poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));

	if (memo.coords && memo.pends) {
		Int32 i = 1;
		// 9 coords, because the last one must be equal the first one
		(*memo.coords)[i].x = centerPoint.x +  0.0;		(*memo.coords)[i].y = centerPoint.y + 0.0;		i++;
		(*memo.coords)[i].x = centerPoint.x +  4.0;		(*memo.coords)[i].y = centerPoint.y + 0.0;		i++;
		(*memo.coords)[i].x = centerPoint.x +  6.0;		(*memo.coords)[i].y = centerPoint.y + 2.0;		i++;
		(*memo.coords)[i].x = centerPoint.x +  6.0;		(*memo.coords)[i].y = centerPoint.y + 6.0;		i++;
		(*memo.coords)[i].x = centerPoint.x +  4.0;		(*memo.coords)[i].y = centerPoint.y + 8.0;		i++;
		(*memo.coords)[i].x = centerPoint.x +  0.0;		(*memo.coords)[i].y = centerPoint.y + 8.0;		i++;
		(*memo.coords)[i].x = centerPoint.x + -2.0;		(*memo.coords)[i].y = centerPoint.y + 6.0;		i++;
		(*memo.coords)[i].x = centerPoint.x + -2.0;		(*memo.coords)[i].y = centerPoint.y + 2.0;		i++;
		(*memo.coords)[i].x = centerPoint.x +  0.0;		(*memo.coords)[i].y = centerPoint.y + 0.0;		(*memo.pends)[1] = i; i++;
	}

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (hatch)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateHatch


// -----------------------------------------------------------------------------
// Create hatches with RGB_Pen and/or override colors
// -----------------------------------------------------------------------------
void	Do_CreateHatches ()
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a point to create a hatch", &centerPoint))
		return;

	Do_CreateHatch (CHF_None,					centerPoint);	centerPoint.y += 10.0;

	Do_CreateHatch (APIHatch_HasBkgRGBColor,	centerPoint);	centerPoint.x += 10.0;
	Do_CreateHatch (APIHatch_HasFgRGBColor,		centerPoint);	centerPoint.x += 10.0;
	Do_CreateHatch (APIHatch_OverrideFgPen,		centerPoint);	centerPoint.x += 10.0;
	Do_CreateHatch (APIHatch_OverrideBkgPen,	centerPoint);	centerPoint.x -= 30.0;	centerPoint.y += 10.0;

	Do_CreateHatch (CHF_ColoredContPen,			centerPoint);	centerPoint.x += 10.0;
	Do_CreateHatch (CHF_ColoredFillPen,			centerPoint);	centerPoint.x += 10.0;
	Do_CreateHatch (CHF_ColoredFillPen | CHF_ColoredContPen,	centerPoint);

	return;
}		// Do_CreateHatch


// -----------------------------------------------------------------------------
// Create line with override colors
// -----------------------------------------------------------------------------
void	Do_CreateLine (API_Coord point, short colorOverridePenIndex)
{
	API_Element element = {};

	element.header.type = API_LineID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Line)", err);
		return;
	}

	element.line.begC = point;	point.x += 6.0;
	element.line.endC = point;	point.x -= 6.0;
	element.line.linePen.penIndex = 3;
	element.line.linePen.colorOverridePenIndex = colorOverridePenIndex;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (line)", err);
	}
}


// -----------------------------------------------------------------------------
// Create polyLine with override colors
// -----------------------------------------------------------------------------
void	Do_CreatePolyLine (API_Coord point, short colorOverridePenIndex)
{
	API_Element element = {};

	element.header.type = API_PolyLineID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Polyline)", err);
		return;
	}

	element.polyLine.poly.nCoords = 5;
	element.polyLine.poly.nSubPolys = 1;
	element.polyLine.poly.nArcs = 0;

	API_ElementMemo memo = {};

	memo.coords = (API_Coord**) BMAllocateHandle ((element.polyLine.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.pends  = (Int32**) BMAllocateHandle ((element.polyLine.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0);
	if (memo.pends != nullptr) {
		(*memo.pends)[0] = 0;
		(*memo.pends)[1] = element.polyLine.poly.nCoords;
	}

	if (memo.coords != nullptr) {
		(*memo.coords)[0].x = -1.0;
		(*memo.coords)[1] = point;	point.x += 6.0;
		(*memo.coords)[2] = point;	point.y += 1.0;
		(*memo.coords)[3] = point;	point.x -= 3.0;
		(*memo.coords)[4] = point;	point.y += 1.0;
		(*memo.coords)[5] = point;
	}

	element.polyLine.linePen.penIndex = 3;
	element.polyLine.linePen.colorOverridePenIndex = colorOverridePenIndex;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (polyLine)", err);
		return;
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreatePolyLine


// -----------------------------------------------------------------------------
// Create circle with override colors
// -----------------------------------------------------------------------------
void	Do_CreateCircle (const API_Coord& point, short colorOverridePenIndex)
{
	API_Element element = {};

	element.header.type = API_ArcID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Arc)", err);
		return;
	}

	element.arc.r = 2.0;
	element.arc.angle = 0.0;
	element.arc.ratio = 1.0;
	element.arc.origC = point;
	element.arc.linePen.penIndex = 3;
	element.arc.linePen.colorOverridePenIndex = colorOverridePenIndex;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (Arc/Circle)", err);
	}
}	// Do_CreateCircle


// -----------------------------------------------------------------------------
// Create arcs with override colors
// -----------------------------------------------------------------------------
void	Do_CreateArc (const API_Coord& point, short colorOverridePenIndex)
{
	API_Element element = {};
	element.header.type = API_ArcID;

	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Arc)", err);
		return;
	}

	element.arc.r = 2.0;
	element.arc.angle = 0.0;
	element.arc.ratio = 1.0;
	element.arc.begAng = 1.0;
	element.arc.endAng = 2.5;
	element.arc.origC = point;
	element.arc.linePen.penIndex = 3;
	element.arc.linePen.colorOverridePenIndex = colorOverridePenIndex;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (Arc)", err);
	}
}	// Do_CreateArc


// -----------------------------------------------------------------------------
// Create spline with override colors
// -----------------------------------------------------------------------------
void	Do_CreateSpline (API_Coord point, short colorOverridePenIndex)
{
	API_Element element = {};

	element.header.type = API_SplineID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Spline)", err);
		return;
	}

	// element.spline.autoSmooth = true;
	element.spline.linePen.penIndex = 3;
	element.spline.linePen.colorOverridePenIndex = colorOverridePenIndex;

	API_ElementMemo memo = {};

	const short nCoords = 5;
	memo.coords = (API_Coord**) BMAllocateHandle (nCoords * sizeof (API_Coord), ALLOCATE_CLEAR, 0);

	if (memo.coords != nullptr) {
		(*memo.coords)[0] = point;	point.x += 2.0;	point.y += 0.5;
		(*memo.coords)[1] = point;	point.x += 2.0;	point.y -= 0.5;
		(*memo.coords)[2] = point;	point.x += 2.0;	point.y += 0.5;
		(*memo.coords)[3] = point;	point.x += 2.0;	point.y -= 0.5;
		(*memo.coords)[4] = point;

		memo.bezierDirs = (API_SplineDir**) BMhAllClear (nCoords * sizeof (Geometry::DirType));
		if (memo.bezierDirs != nullptr)
			Geometry::CalcSpline ((Coord*) *memo.coords, (Geometry::DirType*) *memo.bezierDirs, nCoords, false /*closed*/);
	}



	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (spline)", err);
		return;
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}


// -----------------------------------------------------------------------------
// Create lines with override colors
// -----------------------------------------------------------------------------
void	Do_CreateLines ()
{
	API_Coord point;
	if (!ClickAPoint ("Click a point to create lines ", &point))
		return;

	Do_CreateLine (point, 0);		point.x += 12.0;
	Do_CreateLine (point, 4);		point.x -= 12.0;	point.y += 2.0;

	Do_CreatePolyLine (point, 0);	point.x += 12.0;
	Do_CreatePolyLine (point, 4);	point.x -= 12.0;	point.y += 3.0;

	Do_CreateCircle (point, 0);		point.x += 12.0;
	Do_CreateCircle (point, 4);		point.x -= 12.0;	point.y += 1.0;

	Do_CreateArc (point, 0);		point.x += 12.0;
	Do_CreateArc (point, 4);		point.x -= 12.0;	point.y += 4.0;

	Do_CreateSpline (point, 0);		point.x += 12.0;
	Do_CreateSpline (point, 4);		point.x -= 12.0;
}		// Do_CreateLines


// -----------------------------------------------------------------------------
// Create a worksheet element
// -----------------------------------------------------------------------------
void	Do_CreateWorksheet (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_SubElement		marker = {};
	API_AddParType		**markAddPars;
	API_Coord			c2;
	GSErrCode			err = NoError;

	element.header.type = API_WorksheetID;
	marker.subType = (API_SubElementType) (APISubElement_MainMarker | APISubElement_NoParams);

	err = ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaultsExt", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	API_LibPart libPart = {};

	err = ACAPI_LibraryPart_GetMarkerParent (element.header.type, libPart);
	if (err != NoError) {
		ErrorBeep ("APIAny_GetMarkerParentID", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	err = ACAPI_LibraryPart_Search (&libPart, false, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_LibraryPart_Search", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	delete libPart.location;

	double a, b;
	Int32 addParNum;
	err = ACAPI_LibraryPart_GetParams (libPart.index, &a, &b, &addParNum, &markAddPars);
	if (err != NoError) {
		ErrorBeep ("ACAPI_LibraryPart_GetParams", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	marker.memo.params = markAddPars;

	if (!ClickAPoint ("Place the worksheet", &c2)) {
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	usprintf (element.worksheet.detailName, sizeof (element.worksheet.detailName), "Worksheet_API (%.3f,%.3f)", c2.x, c2.y);
	usprintf (element.worksheet.detailIdStr, sizeof (element.worksheet.detailIdStr), "IDstring_API");
	element.worksheet.pos = c2;
	element.worksheet.poly.nCoords = 5;
	element.worksheet.poly.nSubPolys = 1;
	element.worksheet.poly.nArcs = 0;
	memo.coords = (API_Coord**) BMAllocateHandle ((element.worksheet.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.pends  = (Int32**) BMAllocateHandle ((element.worksheet.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0);
	if (memo.coords != nullptr && memo.pends != nullptr) {
		(*memo.coords)[1].x = c2.x - 1.0;
		(*memo.coords)[1].y = c2.y;
		(*memo.coords)[2].x = c2.x;
		(*memo.coords)[2].y = c2.y - 1.0;
		(*memo.coords)[3].x = c2.x + 1.0;
		(*memo.coords)[3].y = c2.y;
		(*memo.coords)[4].x = c2.x;
		(*memo.coords)[4].y = c2.y + 1.0;
		(*memo.coords)[5].x = (*memo.coords)[1].x;
		(*memo.coords)[5].y = (*memo.coords)[1].y;

		(*memo.pends)[0] = 0;
		(*memo.pends)[1] = element.worksheet.poly.nCoords;
	}

	marker.subElem.object.pen = 3;
	marker.subElem.object.useObjPens = true;
	marker.subElem.object.pos.x = c2.x + 1.5;
	marker.subElem.object.pos.y = c2.y + 1.0;

	marker.subType = APISubElement_MainMarker;
	err = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_CreateExt (Worksheet)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);

	return;
}		// Do_CreateWorksheet


// -----------------------------------------------------------------------------
// Creates a drawing from the worksheet
//		- create a drawing
//		- the drawing's poly is a square
//		- make a circle in the top right corner
//		- shift the drawing in order to show the circle in the center of the rectangle
// -----------------------------------------------------------------------------
void CreateDrawingFromWorksheet (API_DatabaseUnId worksheetDbUnId)
{
	GSErrCode err;

	API_Element element = {};

	element.header.type = API_DrawingID;
	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (drawing)", err);
		return;
	}

	// create drawing data
	GS::Array<API_NavigatorItem> navItems;
	API_NavigatorItem	navItem = {};

	navItem.mapId = API_PublicViewMap;
	navItem.itemType = API_WorksheetDrawingNavItem;
	navItem.db.typeID = APIWind_WorksheetID;
	navItem.db.databaseUnId = worksheetDbUnId; // search for this worksheet in the navigator

	GSPtr		drawingData = nullptr;

	err = ACAPI_Navigator_SearchNavigatorItem (&navItem, &navItems);
	if (err != NoError)
		return;

	if (navItems.GetSize () > 0) {
		element.drawing.drawingGuid = navItems[0].guid;	// link the drawing to the first worksheet
	} else {
		WriteReport_Alert ("There is no worksheet.");
		return;
	}

	// create a circle into the drawing
	err = ACAPI_Drawing_StartDrawingData ();
	if (err == NoError) {
		API_Element	elemInDrawing = {};

		elemInDrawing.header.type = API_CircleID;
		elemInDrawing.header.layer = ACAPI_CreateAttributeIndex (5);
		elemInDrawing.circle.linePen.penIndex = 4;
		elemInDrawing.circle.ltypeInd = ACAPI_CreateAttributeIndex (2);

		elemInDrawing.circle.origC.x = 0.1;
		elemInDrawing.circle.origC.y = 0.1;
		elemInDrawing.circle.r = 0.025;
		elemInDrawing.circle.ratio = 1;
		err = ACAPI_Element_Create (&elemInDrawing, nullptr);

		if (err == NoError)
			err = ACAPI_Drawing_StopDrawingData (&drawingData, &element.drawing.bounds);
	}

	if (err != NoError || drawingData == nullptr) {
		return;
	}

	API_ElementMemo memo = {};
	memo.drawingData = drawingData;

	// create drawing element
	element.header.type = API_DrawingID;
	CHCopyC ("Drawing element from worksheet", element.drawing.name);
	element.drawing.nameType = APIName_CustomName;
	element.drawing.ratio = 1.0;
	element.drawing.anchorPoint = APIAnc_LB;
	element.drawing.pos.x = 0.0;
	element.drawing.pos.y = 0.0;
	element.drawing.isCutWithFrame = true;
	element.drawing.poly.nCoords = 5;
	element.drawing.poly.nSubPolys = 1;
	element.drawing.poly.nArcs = 0;

	memo.coords = (API_Coord**) BMAllocateHandle ((element.drawing.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.pends = (Int32**) BMAllocateHandle ((element.drawing.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0);
	if (memo.coords != nullptr && memo.pends != nullptr) {
		(*memo.coords)[1].x = 0;
		(*memo.coords)[1].y = 0;
		(*memo.coords)[2].x = 0.1;
		(*memo.coords)[2].y = 0;
		(*memo.coords)[3].x = 0.1;
		(*memo.coords)[3].y = 0.1;
		(*memo.coords)[4].x = 0;
		(*memo.coords)[4].y = 0.1;
		(*memo.coords)[5].x = (*memo.coords)[1].x;
		(*memo.coords)[5].y = (*memo.coords)[1].y;

		(*memo.pends)[0] = 0;
		(*memo.pends)[1] = element.drawing.poly.nCoords;
	}

	// shift the circle to the center of the poly
	element.drawing.modelOffset.x = -0.05;
	element.drawing.modelOffset.y = -0.05;

	err = ACAPI_Element_Create (&element, &memo);
	ACAPI_DisposeElemMemoHdls (&memo);

	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (drawing)", err);

	return;
}

// -----------------------------------------------------------------------------
// Place an existing worksheet on a layout
// -----------------------------------------------------------------------------
void	Do_PlaceWorksheetOnLayout (void)
{
	GSErrCode           err;

	// Get the worksheets
	API_DatabaseUnId    *worksheetDbUnIds = nullptr;

	err = ACAPI_Database_GetWorksheetDatabases (&worksheetDbUnIds);
	if (err == NoError && worksheetDbUnIds == nullptr) {
		ACAPI_WriteReport ("There is no worksheet in the database.", true);
		return;
	} else if (err != NoError) {
		ACAPI_WriteReport ("APIDb_GetWorksheetDatabasesID has failed with error code %ld!", true, err);
		return;
	}

	CreateDrawingFromWorksheet (worksheetDbUnIds[0]);

	if (worksheetDbUnIds != nullptr)
		BMpFree (reinterpret_cast<GSPtr>(worksheetDbUnIds));

	return;
}		// Do_PlaceWorksheetOnLayout


// -----------------------------------------------------------------------------
// Create an independent Label with default settings
//   - the label is created with the default settings
//   - try it both in text and symbol mode
//   - the empty string in text mode will be substituted by a predefined string
// -----------------------------------------------------------------------------
void	Do_CreateLabel (void)
{
	API_Coord c;
	if (!ClickAPoint ("Click label reference point", &c))
		return;

	GSErrCode			err;
	API_Element			element = {};
	API_ElementMemo		memo = {};

	element.header.type = API_LabelID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults", err);
		return;
	}

	element.label.parent		= APINULLGuid;
	element.label.begC			= c;
	element.label.midC.x		= c.x + 1.0;
	element.label.midC.y		= c.y + 0.5;
	element.label.endC.x		= c.x + 3.0;
	element.label.endC.y		= c.y + 0.5;

	if (element.label.labelClass == APILblClass_Text) {
		ReplaceEmptyTextWithPredefined (memo);
		element.label.u.text.nonBreaking = true;
	}

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (Label)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
}		// Do_CreateLabel


// -----------------------------------------------------------------------------
// Create an associative Label with default settings
//   - the label is created with the default settings
//   - try it both in text and symbol mode
//   - the empty string in text mode will be substituted by a predefined string
// -----------------------------------------------------------------------------
void	Do_CreateLabel_Associative (void)
{
	API_ElemType	type;
	API_Guid		guid;
	API_Coord3D		begC3D;
	if (!ClickAnElem ("Click the element to assign the Label to. The clicked point will be the \"begC\" of the Label.", API_ZombieElemID, nullptr, &type, &guid, &begC3D)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}

	bool	bAutoText;
	ACAPI_AutoText_GetAutoTextFlag (&bAutoText);

	if (bAutoText) {
		bool _bAutoText = false;
		ACAPI_AutoText_ChangeAutoTextFlag (&_bAutoText);
	}

	GSErrCode		err;
	API_Element		element = {};
	API_ElementMemo	memo = {};

	element.header.type = API_LabelID;
	element.label.parentType = type;

	err = ACAPI_Element_GetDefaults (&element, &memo);

	if (bAutoText) {
		ACAPI_AutoText_ChangeAutoTextFlag (&bAutoText);
	}

	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults", err);
		return;
	}

	element.label.begC.x = begC3D.x;
	element.label.begC.y = begC3D.y;

	if (ClickAPoint ("Click the \"midC\" of the Label. (Esc = default position)", &element.label.midC) &&
		ClickAPoint ("Click the \"endC\" of the Label. (Esc = default position)", &element.label.endC))
		element.label.createAtDefaultPosition = false;
	else
		element.label.createAtDefaultPosition = true;

	element.label.parent = guid;

	if (element.label.labelClass == APILblClass_Text) {
		ReplaceEmptyTextWithPredefined (memo);
		element.label.u.text.nonBreaking = true;
	}

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (Associative Label)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
}		// Do_CreateLabel_Associative


// -----------------------------------------------------------------------------
// Create an independent text Label with fixed frame size
//   - label text frame is set to Circle shape with fixed size
//   - several parameters are changed to get a customized look
// -----------------------------------------------------------------------------
void	Do_CreateLabel_FixedFrame (void)
{
	API_Coord c;
	if (!ClickAPoint ("Click label reference point", &c))
		return;

	GSErrCode			err;
	API_Element			element = {};
	API_ElementMemo		memo = {};

	element.header.type = API_LabelID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults", err);
		return;
	}

	element.label.labelClass			= APILblClass_Text;
	element.label.parent				= APINULLGuid;
	element.label.hasLeaderLine			= true;
	element.label.arrowData.arrowType	= APIArr_PepitaCirc;
	element.label.arrowData.begArrow	= true;
	element.label.arrowData.arrowPen	= 8;
	element.label.anchorPoint			= APILbl_MiddleAnchor;
	element.label.begC					= c;
	element.label.midC.x				= c.x + 0.5;
	element.label.midC.y				= c.y + 1.5;
	element.label.endC.x				= c.x - 3.0;
	element.label.endC.y				= c.y + 0.5;

	const char* textContent = "This label has been created from the Element Test add-on";
	delete memo.textContent;
	memo.textContent = new GS::UniString { textContent };
	ACAPI_DisposeParagraphsHdl (&memo.paragraphs);
	memo.paragraphs = reinterpret_cast<API_ParagraphType **> (BMhAllClear (sizeof (API_ParagraphType)));
	(*memo.paragraphs)[0].from = 0;
	(*memo.paragraphs)[0].range	= Strlen32 (textContent);
	(*memo.paragraphs)[0].just = APIJust_Center;
	(*memo.paragraphs)[0].spacing = -1.0;
	(*memo.paragraphs)[0].run = reinterpret_cast<API_RunType*> (BMAllocatePtr (sizeof (API_RunType), ALLOCATE_CLEAR, 0));
	(*memo.paragraphs)[0].run[0].range = Strlen32 (textContent);
	(*memo.paragraphs)[0].run[0].pen = 1;
	(*memo.paragraphs)[0].run[0].faceBits = APIFace_Bold;
	(*memo.paragraphs)[0].run[0].font = element.label.font;
	(*memo.paragraphs)[0].run[0].size = 2.5;

	element.label.u.text.nonBreaking			= false;
	element.label.u.text.width					= 25.0;
	element.label.framed						= true;
	element.label.pen							= 8;
	element.label.u.text.usedFill				= true;
	element.label.u.text.fillPen				= 103;
	element.label.u.text.angle					= 3.14159265358979323846;
	element.label.u.text.flipEnabled			= true;

	element.label.u.text.textFrame.shapeType	= API_TextFrameShapeType_Circle;
	element.label.u.text.textFrame.isSizeFixed	= true;
	element.label.u.text.textFrame.fixedWidth	= 30.0;
	element.label.u.text.textFrame.fixedHeight	= 30.0;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (Label)", err);

	ACAPI_DisposeElemMemoHdls (&memo);
}		// Do_CreateLabel_FixedFrame


// -----------------------------------------------------------------------------
// Return actual story number
// -----------------------------------------------------------------------------
short GetActStory ()
{
	API_StoryInfo	storyInfo;
	GSErrCode		err;
	short			actStory = 0;

	err = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
	if (err == NoError) {
		actStory = storyInfo.actStory;
		BMKillHandle ((GSHandle*) &storyInfo.data);
	}
	return actStory;
}

// -----------------------------------------------------------------------------
// Create a zone with the given geometry
// -----------------------------------------------------------------------------
GSErrCode	CreateZoneWithGeometry (const API_Coord&		pos,
									const API_Polygon&		poly,
									const API_Coord*		coords,
									const Int32*			pends,
									const API_PolyArc*		parcs,
									const double			height,
									const API_Coord&		posText,
									const GS::UniString&	roomName,
									const GS::UniString&	roomNoStr,
									API_Guid*				newZoneGuid)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	GSErrCode			err;

	element.header.type = API_ZoneID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Zone)", err);
		return err;
	}

	element.header.type		= API_ZoneID;
	element.header.floorInd	= GetActStory ();
	element.zone.manual		= true;
	element.zone.poly		= poly;
	memo.coords	= reinterpret_cast<API_Coord**>		(BMAllocateHandle ((poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.pends	= reinterpret_cast<Int32**>			(BMAllocateHandle ((poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.parcs	= reinterpret_cast<API_PolyArc**>	(BMAllocateHandle (poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	if (memo.coords && memo.pends && memo.parcs) {
		for (Int32 i = 1; i <= poly.nCoords; i++) {
			(*memo.coords)[i].x = coords[i].x + pos.x;
			(*memo.coords)[i].y = coords[i].y + pos.y;

		}

		for (Int32 i = 0; i <= poly.nSubPolys; i++) {
			(*memo.pends)[i] = pends[i];
		}

		for (Int32 i = 0; i < poly.nArcs; i++) {
			(*memo.parcs)[i].begIndex  = parcs[i].begIndex;
			(*memo.parcs)[i].endIndex  = parcs[i].endIndex;
			(*memo.parcs)[i].arcAngle  = parcs[i].arcAngle;
		}

		GS::snuprintf (element.zone.roomName, sizeof (element.zone.roomName), roomName.ToUStr ());
		GS::snuprintf (element.zone.roomNoStr, sizeof (element.zone.roomNoStr), roomNoStr.ToUStr ());
		element.zone.pos.x		= posText.x + pos.x;
		element.zone.pos.y		= posText.y + pos.y;

		element.zone.roomHeight	= height;

		err = ACAPI_Element_Create (&element, &memo);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Create (Zone)", err);
			*newZoneGuid = APINULLGuid;
		} else
			*newZoneGuid = element.header.guid;
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}

// -----------------------------------------------------------------------------
// Create an automatic zone with the default settings
// -----------------------------------------------------------------------------
GSErrCode	CreateAutoZone (const API_Coord&		pos,
							const GS::UniString&	roomName,
							const GS::UniString&	roomNoStr,
							API_Guid*				newZoneGuid)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	GSErrCode			err;

	GS::Array<API_Guid> zoneList;
	ACAPI_Element_GetElemList (API_ZoneID, &zoneList);

	element.header.type = API_ZoneID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Zone)", err);
		return err;
	}

	element.header.type = API_ZoneID;
	element.zone.catInd   = ACAPI_CreateAttributeIndex (1);
	element.zone.manual   = false;

	GS::snuprintf (element.zone.roomName, sizeof (element.zone.roomName), roomName.ToUStr ());
	GS::snuprintf (element.zone.roomNoStr, sizeof (element.zone.roomNoStr), roomNoStr.ToUStr ());

	element.zone.pos.x = pos.x;
	element.zone.pos.y = pos.y;
	element.zone.refPos.x = pos.x;
	element.zone.refPos.y = pos.y;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (Zone)", err);
		*newZoneGuid = APINULLGuid;
	} else
		*newZoneGuid = element.header.guid;

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}		// CreateAutoZone

// -----------------------------------------------------------------------------
// Create a zone with the default settings to the given point
//   - geometryID:
//        0 = Automatic
//        1 = Octagon
//        2 = Square with hole and arcs
// -----------------------------------------------------------------------------
void Do_CreateZone (short geometryID)
{
	GSErrCode			err;
	API_Coord			pos;

	if (!ClickAPoint ("Click zone reference point", &pos))
		return;

	API_Polygon		poly;
	API_Coord**		coords = nullptr;
	Int32**			pends = nullptr;
	API_PolyArc**	parcs = nullptr;
	double			height;

	GS::UniString		roomName;
	GS::Array<API_Guid> zoneList;
	ACAPI_Element_GetElemList (API_ZoneID, &zoneList);
	GS::UniString		roomNoStr	= GS::UniString::Printf ("%lu", (GS::ULongForStdio) zoneList.GetSize () + 1);
	API_Coord			posText;

	API_Guid		newZoneGuid = APINULLGuid;

	// Create the given geometry or automatic
	switch (geometryID) {
		case 0:
			roomName = GS::UniString::Printf ("AutoZone_%lu", (GS::ULongForStdio) zoneList.GetSize () + 1);

			err = CreateAutoZone (pos, roomName, roomNoStr, &newZoneGuid);
			if (err != NoError)
				ErrorBeep ("CreateAutoZone", err);
			break;

		case 1:
			roomName		= GS::UniString ("Octagon");
			poly.nCoords	= 9;
			poly.nSubPolys	= 1;
			poly.nArcs		= 0;

			coords	= reinterpret_cast<API_Coord**>		(BMAllocateHandle ((poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
			pends	= reinterpret_cast<Int32**>			(BMAllocateHandle ((poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
			parcs	= reinterpret_cast<API_PolyArc**>	(BMAllocateHandle (poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
			if (coords && pends && parcs) {
				Int32 i = 1;
				// 9 coords, because the last one must be equal the first one
				(*coords)[i].x = 0.0; (*coords)[i].y = 0.0; i++;
				(*coords)[i].x = 4.0; (*coords)[i].y = 0.0; i++;
				(*coords)[i].x = 6.0; (*coords)[i].y = 2.0; i++;
				(*coords)[i].x = 6.0; (*coords)[i].y = 6.0; i++;
				(*coords)[i].x = 4.0; (*coords)[i].y = 8.0; i++;
				(*coords)[i].x = 0.0; (*coords)[i].y = 8.0; i++;
				(*coords)[i].x =-2.0; (*coords)[i].y = 6.0; i++;
				(*coords)[i].x =-2.0; (*coords)[i].y = 2.0; i++;
				(*coords)[i].x = 0.0; (*coords)[i].y = 0.0; (*pends)[1] = i; i++;

				height = 1.5;

				posText.x = 2.0; posText.y = 4.0;

				err = CreateZoneWithGeometry (pos, poly, *coords, *pends, *parcs, height, posText, roomName, roomNoStr, &newZoneGuid);
				if (err != NoError)
					ErrorBeep ("CreateZoneWithGeometry (Octagon)", err);
			} else {
				WriteReport_Alert ("BMAllocateHandle failed");
			}
			break;

		case 2:
		default:
			roomName		= GS::UniString ("With hole and arcs");
			poly.nCoords	= 10;
			poly.nSubPolys	= 2;
			poly.nArcs		= 2;

			coords	= reinterpret_cast<API_Coord**>		(BMAllocateHandle ((poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
			pends	= reinterpret_cast<Int32**>			(BMAllocateHandle ((poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
			parcs	= reinterpret_cast<API_PolyArc**>	(BMAllocateHandle (poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
			if (coords && pends && parcs) {
				Int32 i = 1;
				// Contour square polygon
				(*coords)[i].x = 0.0; (*coords)[i].y = 0.0; i++;
				(*coords)[i].x = 4.0; (*coords)[i].y = 0.0; i++;
				(*coords)[i].x = 4.0; (*coords)[i].y = 4.0; i++;
				(*coords)[i].x = 0.0; (*coords)[i].y = 4.0; i++;
				(*coords)[i].x = 0.0; (*coords)[i].y = 0.0; (*pends)[1] = i; i++;

				// Hole polygon
				(*coords)[i].x = 1.0; (*coords)[i].y = 1.0; i++;
				(*coords)[i].x = 3.0; (*coords)[i].y = 1.0; i++;
				(*coords)[i].x = 3.0; (*coords)[i].y = 3.0; i++;
				(*coords)[i].x = 1.0; (*coords)[i].y = 3.0; i++;
				(*coords)[i].x = 1.0; (*coords)[i].y = 1.0; (*pends)[2] = i; i++;

				i = 0;
				// Two arcs:
				// between the 1. and 2.
				(*parcs)[i].begIndex  = 1; (*parcs)[i].endIndex  = 2; (*parcs)[i].arcAngle  = 90.0 * DEGRAD; i++;
				// between the 7. and 8.
				(*parcs)[i].begIndex  = 7; (*parcs)[i].endIndex  = 8; (*parcs)[i].arcAngle  = 45.0 * DEGRAD; i++;

				height = 2.0;

				posText.x = 1.0; posText.y = 1.0;

				err = CreateZoneWithGeometry (pos, poly, *coords, *pends, *parcs, height, posText, roomName, roomNoStr, &newZoneGuid);
				if (err != NoError)
					ErrorBeep ("CreateZoneWithGeometry (Square with hole and arcs)", err);
			} else {
				WriteReport_Alert ("BMAllocateHandle failed");
			}
			break;
	}

	if (coords)
		BMKillHandle ((GSHandle*) &coords);
	if (pends)
		BMKillHandle ((GSHandle*) &pends);
	if (parcs)
		BMKillHandle ((GSHandle*) &parcs);

}		// Do_CreateZone


// -----------------------------------------------------------------------------
// Create a zone with automatic label
//   - the label is created with the settings of the selected label favorite
//   - the label is created only if it's set on
// -----------------------------------------------------------------------------
void Do_CreateZoneWithAutoLabelFavorite (void)
{
	Do_CreateZone (0);

	API_ElemType	type;
	API_Guid		guid;
	API_Coord3D		begC3D;

	if (!ClickAnElem ("Enter Zone Text Position.", API_ZombieElemID, nullptr, &type, &guid, &begC3D)) {
		WriteReport_Alert ("No element was clicked.");
		return;
	}
	bool	bAutoText;
	ACAPI_AutoText_GetAutoTextFlag (&bAutoText);

	if (bAutoText) {
		bool _bAutoText = false;
		ACAPI_AutoText_ChangeAutoTextFlag (&_bAutoText);
	}

	ACAPI::ZoneLabelingSettings zoneLabelingSetting {ACAPI::CreateZoneLabelingSettings ()};

	if (zoneLabelingSetting.IsAutomaticLabelingOn ())
	{
		// ! [ZoneAutoLabel_GetFavorite-Example]
		ACAPI::Result<GS::UniString> result = zoneLabelingSetting.GetSelectedFavoriteName ();
		
		if (result.IsErr ()) {
			ACAPI_WriteReport (result.UnwrapErr ().text.c_str (), true);
			return;
		}

		API_Favorite favorite (result.Unwrap ());
		// ! [ZoneAutoLabel_GetFavorite-Example]

		favorite.memo.New ();
		favorite.properties.New ();
		favorite.classifications.New ();
		favorite.elemCategoryValues.New ();

		favorite.element.label.parent = guid;
		favorite.element.label.parentType = type;

		GSErrCode err = ACAPI_Favorite_Get (&favorite);

		API_Element element = {};
		API_ElementMemo	memo = {};

		element.header.type = API_LabelID;

		element.label.parent = guid;
		element.label.parentType = type;
		err = ACAPI_Element_GetDefaults (&element, &memo);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_GetDefaults", err);
			return;
		}

		favorite.element.label.parent = guid;
		favorite.element.label.parentType = type;

		if (bAutoText) {
			ACAPI_AutoText_ChangeAutoTextFlag (&bAutoText);
		}

		favorite.element.label.begC.x = begC3D.x;
		favorite.element.label.begC.y = begC3D.y;
		favorite.element.label.createAtDefaultPosition = true;

		if (favorite.element.label.labelClass == APILblClass_Text) {
			ReplaceEmptyTextWithPredefined (favorite.memo.Get ());
			favorite.element.label.u.text.nonBreaking = true;
		}

		err = ACAPI_Element_Create (&favorite.element, &favorite.memo.Get ());
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Create (Associative Label)", err);
		}

		ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
	}
}


// -----------------------------------------------------------------------------
// Set the last label favorite from the list as the selected label favorite for zone
// -----------------------------------------------------------------------------
void Do_SetAutoZoneLabelFavorite_FirstByABC (void)
{
	short count;
	GS::Array<GS::UniString> names;
	GSErrCode err = ACAPI_Favorite_GetNum (API_ElemType (API_LabelID, APIVarId_Generic), &count, nullptr, &names);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Favorite_GetNum", err);
		return;
	}

	if (count > 0) {
		GS::NthElement (names.Begin (), names.Begin (), names.End ());
		// ! [ZoneAutoLabel_SetFavorite-Example]
		ACAPI::ZoneLabelingSettings zoneLabelingSetting {ACAPI::CreateZoneLabelingSettings ()};
		ACAPI::Result<void> result = zoneLabelingSetting.Modify ([&] (ACAPI::ZoneLabelingSettings::Modifier& modifier) {
			modifier.SetSelectedFavorite (names[0]);
		});
		if (result.IsErr ()) {
			ACAPI_WriteReport (result.UnwrapErr ().text.c_str (), true);
		return;
		}
		// ! [ZoneAutoLabel_SetFavorite-Example]
	} else {
		ACAPI_WriteReport ("There is no favorite in the favorite list.", true);
	}
}


// -----------------------------------------------------------------------------
// Switch the setting of automatic zone labeling between on and off
// -----------------------------------------------------------------------------
void Do_ChangeAutoZoneLabeling (void)
{
	// ! [ZoneAutoLabel-Example]
	ACAPI::ZoneLabelingSettings zoneLabelingSetting = ACAPI::CreateZoneLabelingSettings ();
	ACAPI::Result<void> result = zoneLabelingSetting.Modify ([&] (ACAPI::ZoneLabelingSettings::Modifier& modifier) {
		modifier.SetAutomaticLabeling (!zoneLabelingSetting.IsAutomaticLabelingOn ());
	});
	// ! [ZoneAutoLabel-Example]
}


// -----------------------------------------------------------------------------
// Create a camera set
//   - same type as the active path
//   - camera set is activated
//   - check Camera Settings if it is open
// -----------------------------------------------------------------------------
void	Do_CreateCamset (void)
{
	API_Element		element = {};
	API_Guid		actGuid = {};
	GSErrCode		err;

	SearchActiveCamset (&actGuid, nullptr);
	if (actGuid == APINULLGuid)
		return;

	element.header.type = API_CamSetID;

	GS::ucscpy (element.camset.name, L("Perspective Camset from API"));
	element.camset.perspPars.openedPath = true;
	element.camset.perspPars.bezierPath = false;
	element.camset.perspPars.smoothTarget = true;
	element.camset.perspPars.pen = 15;
	element.camset.perspPars.inFrames = 7;

	element.camset.active = true;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (camera set)", err);
		return;
	}

	return;
}		// Do_CreateCamset


// -----------------------------------------------------------------------------
// Create a perspective camera
//   - camera click: insert after
//   - empty click: create a new camera set for the camera
// -----------------------------------------------------------------------------
void	Do_CreatePerspCam (void)
{
	API_ElemType	type;
	API_Guid		prevGuid, camSetGuid;
	API_Coord		pos, target;
	double			cameraZ, targetZ;
	API_Guid		guid;
	API_Coord3D		pos3D;
	GSErrCode		err;

	err = NoError;

	if (!ClickAnElem ("Click a camera to insert a new camera after or click a place to create a new path/camera", API_ZombieElemID, nullptr, &type, &guid, &pos3D)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	API_Element element = {};

	if (type == API_CameraID) {
		element.header.guid = guid;
		ACAPI_Element_Get (&element);

		prevGuid   = element.header.guid;
		camSetGuid = element.camera.camSetGuid;

		pos    = element.camera.perspCam.persp.pos;
		target = element.camera.perspCam.persp.target;
		pos.x += 5.0;
		target.x += 3.0;

		cameraZ = element.camera.perspCam.persp.cameraZ + 1.0;
		targetZ = element.camera.perspCam.persp.targetZ + 1.0;
	} else {
		element.header.type = API_CamSetID;

		GS::ucscpy (element.camset.name, L("Perspective Camset from API"));

		element.camset.perspPars.openedPath = true;
		element.camset.perspPars.bezierPath = false;
		element.camset.perspPars.smoothTarget = false;
		element.camset.perspPars.pen = 15;
		element.camset.perspPars.inFrames = 7;

		element.camset.active = true;

		err = ACAPI_Element_Create (&element, nullptr);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Create (persp. camset)", err);
			return;
		}

		prevGuid = APINULLGuid;
		camSetGuid = element.header.guid;

		pos.x = pos3D.x;
		pos.y = pos3D.y;
		target.x = pos.x;
		target.y = pos.y + 3.0;

		cameraZ = 0.5;
		targetZ = 1.0;
	}

	element.header.type = API_CameraID;

	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults", err);
		return;
	}

	element.camera.perspCam.persp.viewCone = 82.0 * DEGRAD;
	element.camera.perspCam.persp.pos = pos;
	element.camera.perspCam.persp.target = target;
	element.camera.perspCam.persp.cameraZ = cameraZ;
	element.camera.perspCam.persp.targetZ = targetZ;

	element.camera.perspCam.waitFrames = 6;
	element.camera.perspCam.pen = 33;

	element.camera.perspCam.prevCam = prevGuid;
	element.camera.perspCam.nextCam = APINULLGuid;
	element.camera.perspCam.lenPrev = 2.0;
	element.camera.perspCam.lenNext = 1.0;
	element.camera.perspCam.dirAng = PI / 2.0;

	element.camera.camSetGuid = camSetGuid;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (persp. camera)", err);
}		// Do_CreatePerspCam


// -----------------------------------------------------------------------------
// Place a picture (TIFF)
//   - it must be called "picture1.tif" and located at: "D:\"
// -----------------------------------------------------------------------------
void	Do_CreatePicture (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_Coord			c = {};
	IO::Location		location;
	USize				nBytes;
	USize				rwnum = 0;
	Int32				hSize, vSize, hRes, vRes, pixelBitNum;
	GSErrCode			err;

	if (!ClickAPoint ("Click picture reference point", &c))
		return;

	element.header.type = API_PictureID;
	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (picture)", err);
		return;
	}

	location.Set ("D:\\picture1.tif");

	err = GX::ImageBase::GetFileInfo (location, &hSize, &vSize, &hRes, &vRes, &pixelBitNum);
	if (err != NoError) {
		ErrorBeep ("GX::ImageBase::GetFileInfo", err);
		return;
	}

	element.picture.usePixelSize = true;
	element.picture.mirrored = false;
	element.picture.destBox.xMin = c.x;
	element.picture.destBox.yMin = c.y;
	element.picture.pixelSizeX = (short) (hSize);
	element.picture.pixelSizeY = (short) (vSize);
	element.picture.rotAngle = 0.0;
	element.picture.anchorPoint = APIAnc_LB;
	element.picture.storageFormat = APIPictForm_TIFF;
	element.picture.transparent = false;

	GS::ucscpy (element.picture.pictName, L("First try"));

	IO::File file (location);
	file.Open (IO::File::ReadMode);

	if (file.GetDataLength (&nBytes) == NoError) {
		memo.pictHdl = BMAllocateHandle (nBytes, ALLOCATE_CLEAR, 0);
		if (memo.pictHdl != nullptr) {
			DBPrintf ("Tiff file size: %d", nBytes);

			file.ReadBin (*memo.pictHdl, nBytes, &rwnum);
			if (rwnum != nBytes)
				err = ErrIO;
		}
	} else
		err = ErrIO;

	file.Close ();


	if (err == NoError) {
		err = ACAPI_Element_Create (&element, &memo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create (picture)", err);
	}

	if (memo.pictHdl != nullptr) {
		ACAPI_DisposeElemMemoHdls (&memo);
	}

	return;
}		// Do_CreatePicture


// -----------------------------------------------------------------------------
// Create a group from lines with user input
// -----------------------------------------------------------------------------
void	Do_CreateGroupedLines (void)
{
	API_Coord			c = {};
	API_GetPolyType		polyInfo = {};
	API_Element			element = {};
	Int32				i;
	GS::Array<API_Guid> group;
	GSErrCode			err;

	if (!ClickAPoint ("Enter the first node of the polyline", &c))
		return;

	strcpy (polyInfo.prompt, "Enter the next node of the polyline");
	polyInfo.method = APIPolyGetMethod_Polyline;
	polyInfo.startCoord.x = c.x;
	polyInfo.startCoord.y = c.y;

	err = ACAPI_UserInput_GetPoly (&polyInfo);
	if (err != NoError)
		return;

	element.header.type = API_LineID;
	ACAPI_Element_GetDefaults (&element, nullptr);

	for (i = 1; i < polyInfo.nCoords; i++) {
		element.line.begC = (*polyInfo.coords)[i];
		element.line.endC = (*polyInfo.coords)[i + 1];

		err = ACAPI_Element_Create (&element, nullptr);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create (line)", err);
		else
			group.Push (element.header.guid);
	}

	API_Guid groupGuid = APINULLGuid;
	err = ACAPI_Grouping_CreateGroup (group, &groupGuid);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Grouping_CreateGroup (group, groupGuid)", err);
	} else {
		GSHandle textHdl;
		const char* text = "Grouped lines test.";
		textHdl = BMAllocateHandle (Strlen32 (text) + 1, ALLOCATE_CLEAR, 0);
		if (DBVERIFY (textHdl != nullptr)) {
			CHCopyC (text, *textHdl);

			API_UserData userData;

			userData.dataVersion	= 1;
			userData.platformSign	= GS::Act_Platform_Sign;
			userData.dataHdl		= textHdl;

			err = ACAPI_Grouping_SetUserData (groupGuid, &userData);
			if (err != NoError)
				ErrorBeep ("ACAPI_Grouping_SetUserData (groupGuid, &userData)", err);
		}

		BMKillHandle ((GSHandle*) &textHdl);
	}

	BMKillHandle ((GSHandle*) &polyInfo.coords);
	BMKillHandle ((GSHandle*) &polyInfo.parcs);

	return;
}		// Do_CreateGroupedLines


// -----------------------------------------------------------------------------
// Create a cutting plane
//
// -----------------------------------------------------------------------------
void	Do_CreateCutPlane (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_SubElement		marker = {};
	GSErrCode			err = {};

	element.header.type = API_CutPlaneID;
	marker.subType = (API_SubElementType) (APISubElement_MainMarker);

	err = ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker);
	if (err != NoError)
		return;

	marker.subElem.object.useObjPens = true;
	GS::ucscpy (element.cutPlane.segment.cutPlName, L("Section_API"));
	GS::ucscpy (element.cutPlane.segment.cutPlIdStr, L("IDstring_API"));

	// Get the first point
	API_GetPointType pointInfo = {};
	CHCopyC ("Click the first point of your cutplane", pointInfo.prompt);
	ACAPI_UserInput_GetPoint (&pointInfo);

	// Get the line
	API_GetLineType lineInfo = {};
	CHCopyC ("Click the second point of your cutplane", lineInfo.prompt);
	lineInfo.startCoord = pointInfo.pos;
	ACAPI_UserInput_GetLine (&lineInfo);

	memo.sectionSegmentMainCoords = (API_Coord*) BMpAll (2 * sizeof (API_Coord));
	memo.sectionSegmentMainCoords[0].x = lineInfo.startCoord.x;
	memo.sectionSegmentMainCoords[0].y = lineInfo.startCoord.y;
	memo.sectionSegmentMainCoords[1].x = lineInfo.pos.x;
	memo.sectionSegmentMainCoords[1].y = lineInfo.pos.y;

	element.cutPlane.segment.nMainCoord = 2;
	marker.subType = APISubElement_MainMarker;
	element.cutPlane.linkData.sourceMarker = true;

	element.cutPlane.segment.nDepthCoord = 2;
	Coord oc (memo.sectionSegmentMainCoords[0].x, memo.sectionSegmentMainCoords[0].y);
	Coord wc (memo.sectionSegmentMainCoords[1].x, memo.sectionSegmentMainCoords[1].y);
	Coord rotCoord = Geometry::RotCoord (oc, wc, sin (PI/2), cos (PI/2));

	Vector v = Geometry::UnitVector (rotCoord - oc);

	Coord refCoord = Geometry::ORIGO2  + ((oc - Geometry::ORIGO2) + v);
	memo.sectionSegmentDepthCoords = (API_Coord*) BMpAll (2 * sizeof (API_Coord));
	memo.sectionSegmentDepthCoords[0].x = refCoord.x;
	memo.sectionSegmentDepthCoords[0].y = refCoord.y;

	err = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);

	return;
}		// Do_CreateCutPlane


// -----------------------------------------------------------------------------
// Create an interior elevation
//
// -----------------------------------------------------------------------------
void	Do_CreateInteriorElevation (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_SubElement		marker = {};
	API_AddParType		**markAddPars;
	double				a, b;
	Int32				addParNum;
	GSErrCode			err;

	element.header.type = API_InteriorElevationID;
	marker.subType = (API_SubElementType) (APISubElement_MainMarker | APISubElement_NoParams);

	err = ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker);
	if (err != NoError)
		return;

	err = ACAPI_LibraryPart_GetParams (marker.subElem.object.libInd, &a, &b, &addParNum, &markAddPars);
	if (err != NoError)
		return;

	marker.memo.params = markAddPars;
	marker.subElem.object.useObjPens = true;
	GS::ucscpy (element.interiorElevation.segment.cutPlName, L("IntElev_API"));
	GS::ucscpy (element.interiorElevation.segment.cutPlIdStr, L("IDstring_API"));

	element.interiorElevation.segment.ieCreationSegmentDepth   = 0.75;

	// Get the first point
	API_GetPointType pointInfo = {};
	CHCopyC ("Click the first point of your interior elevation", pointInfo.prompt);
	ACAPI_UserInput_GetPoint (&pointInfo);

	// Get the polyline
	API_GetPolyType	polyInfo = {};
	CHCopyC ("Click the points of your interior elevation", polyInfo.prompt);
	polyInfo.method = APIPolyGetMethod_Polyline;
	polyInfo.startCoord = pointInfo.pos;
	if (ACAPI_UserInput_GetPoly (&polyInfo) == NoError) {
		ULong	lastInd = polyInfo.nCoords;

		if (!polyInfo.polylineWas)
			lastInd--;

		element.interiorElevation.segment.nMainCoord = (short) lastInd;
		memo.sectionSegmentMainCoords = reinterpret_cast<API_Coord*> (BMAllocatePtr (lastInd * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
		memo.intElevSegments = reinterpret_cast<API_SectionSegment*> (BMAllocatePtr ((lastInd - 1) * sizeof (API_SectionSegment), ALLOCATE_CLEAR, 0));
		for (ULong ii = 1; ii <= lastInd; ii++) {
			memo.sectionSegmentMainCoords[ii-1] = (*polyInfo.coords)[ii];
		}
		for (ULong i = 0; i < lastInd-1; i++) {
			memo.intElevSegments[i].ieCreationSegmentHorizontalOffset = (i + 1) * 0.5;
		}

		marker.subType = APISubElement_MainMarker;
		err = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);

		BMKillHandle (reinterpret_cast<GSHandle*> (&polyInfo.coords));
		BMKillHandle (reinterpret_cast<GSHandle*> (&polyInfo.parcs));
	}

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);
} // Do_CreateInteriorElevation


// -----------------------------------------------------------------------------
// Create a window in the clicked wall
//	- does not work on polygonal walls
//	- try to place both an empty opening and a real window object
// -----------------------------------------------------------------------------
void Do_CreateWindow (void)
{
	API_Guid		 wallGuid;
	API_Element		 element = {};
	API_ElementMemo	 memo	 = {};
	API_SubElement	 marker	 = {};
	API_Coord3D		 c3		 = {};
	API_Coord		 c2, begC = {};
	API_AddParType** markAddPars;
	GSErrCode		 err = NoError;

	if (!ClickAnElem ("Click a wall to place a window", API_WallID, nullptr, &element.header.type, &wallGuid, &c3)) {
		WriteReport_Alert ("Please click a wall");
		return;
	}

	element.header.type = API_WallID;
	element.header.guid = wallGuid;

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	if (element.wall.type == APIWtyp_Poly) {
		WriteReport_Alert ("No way to put openings into polygonal walls");
		return;
	}

	begC = element.wall.begC;
	c2.x = c3.x;
	c2.y = c3.y;

	element.header.type = API_WindowID;
	marker.subType		= APISubElement_MainMarker;

	err = ACAPI_Element_GetDefaultsExt (&element, &memo, 1UL, &marker);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaultsExt", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	API_LibPart libPart;

	err = ACAPI_LibraryPart_GetMarkerParent (element.header.type, libPart);
	if (err != NoError) {
		ErrorBeep ("APIAny_GetMarkerParentID", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	err = ACAPI_LibraryPart_Search (&libPart, false, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_LibraryPart_Search", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	delete libPart.location;

	double a, b;
	Int32  addParNum;
	err = ACAPI_LibraryPart_GetParams (libPart.index, &a, &b, &addParNum, &markAddPars);
	if (err != NoError) {
		ErrorBeep ("ACAPI_LibraryPart_GetParams", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		ACAPI_DisposeElemMemoHdls (&marker.memo);
		return;
	}

	marker.memo.params				 = markAddPars;
	marker.subElem.object.pen		 = 166;
	marker.subElem.object.useObjPens = true;
	element.window.objLoc			 = DistCPtr (&c2, &begC);
	element.window.owner			 = wallGuid;
	err								 = ACAPI_Element_CreateExt (&element, &memo, 1UL, &marker);
	if (err != NoError) ErrorBeep ("ACAPI_Element_CreateExt", err);

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);

	return;
} // Do_CreateWindow


// -----------------------------------------------------------------------------
// Create a skylight in the clicked roof
// -----------------------------------------------------------------------------
void	Do_CreateSkylight (void)
{
	API_ElemType		type;
	API_Guid			guid;
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_Coord3D			c3 = {};
	GSErrCode			err = NoError;

	if (!ClickAnElem ("Click on a roof or a shell to place a skylight", API_ZombieElemID, nullptr, &type, &guid, &c3) ||
		(type != API_RoofID && type != API_ShellID))
	{
		WriteReport_Alert ("Please click a roof or a shell element");
		return;
	}

	API_Coord c2;
	c2.x = c3.x;
	c2.y = c3.y;

	element.header.type = API_SkylightID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	element.skylight.owner = guid;
	element.skylight.anchorPosition = c2;
	element.skylight.anchorLevel = 2.0;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateSkylight


// -----------------------------------------------------------------------------
// Create a Multi-plane Roof
// -----------------------------------------------------------------------------
void	Do_CreatePolyRoof (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a Roof", &centerPoint))
		return;

	API_Element element;
	element.header.type = API_RoofID;
	element.roof.roofClass = API_PolyRoofID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (roof)", err);
		return;
	}

	API_ElementMemo memo;

	// constructing pivot polygon data
	element.roof.u.polyRoof.pivotPolygon.nCoords	= 8;
	element.roof.u.polyRoof.pivotPolygon.nSubPolys	= 2;
	element.roof.u.polyRoof.pivotPolygon.nArcs		= 2;

	memo.additionalPolyCoords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((element.roof.u.polyRoof.pivotPolygon.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.additionalPolyPends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.roof.u.polyRoof.pivotPolygon.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.additionalPolyParcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.roof.u.polyRoof.pivotPolygon.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));

	(*memo.additionalPolyCoords)[1].x = centerPoint.x + 10.0;
	(*memo.additionalPolyCoords)[1].y = centerPoint.x + 10.0;
	(*memo.additionalPolyCoords)[2].x = centerPoint.x + 10.0;
	(*memo.additionalPolyCoords)[2].y = centerPoint.x - 10.0;
	(*memo.additionalPolyCoords)[3].x = centerPoint.x - 10.0;
	(*memo.additionalPolyCoords)[3].y = centerPoint.x - 10.0;
	(*memo.additionalPolyCoords)[4].x = centerPoint.x - 10.0;
	(*memo.additionalPolyCoords)[4].y = centerPoint.x + 10.0;
	(*memo.additionalPolyCoords)[5] = (*memo.additionalPolyCoords)[1];
	(*memo.additionalPolyPends)[1] = 5;
	(*memo.additionalPolyCoords)[6].x = centerPoint.x + 5.0;
	(*memo.additionalPolyCoords)[6].y = centerPoint.x + 0.0;
	(*memo.additionalPolyCoords)[7].x = centerPoint.x - 5.0;
	(*memo.additionalPolyCoords)[7].y = centerPoint.x + 0.0;
	(*memo.additionalPolyCoords)[8] = (*memo.additionalPolyCoords)[6];
	(*memo.additionalPolyPends)[2] = 8;
	(*memo.additionalPolyParcs)[0].begIndex = 6;							// makes a circle-shaped hole in the pivot polygon
	(*memo.additionalPolyParcs)[0].endIndex = 7;
	(*memo.additionalPolyParcs)[0].arcAngle = PI;
	(*memo.additionalPolyParcs)[1].begIndex = 7;
	(*memo.additionalPolyParcs)[1].endIndex = 8;
	(*memo.additionalPolyParcs)[1].arcAngle = PI;

	// setting eaves overhang and plane levels
	element.roof.u.polyRoof.overHangType = API_OffsetOverhang;		// contourPolygon will be calculated automatically by offsetting the pivot polygon
	element.roof.u.polyRoof.eavesOverHang = 1.0;
	element.roof.u.polyRoof.levelNum = 16;
	for (Int32 i = 0; i < element.roof.u.polyRoof.levelNum; i++) {
		element.roof.u.polyRoof.levelData[i].levelAngle = 5.0 * DEGRAD * (i + 1);
		element.roof.u.polyRoof.levelData[i].levelHeight = 0.05 * (i + 1);
	}

	// create the roof element
	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (roof)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreatePolyRoof


// -----------------------------------------------------------------------------
// Create an Extruded Shell
// -----------------------------------------------------------------------------
void	Do_CreateExtrudedShell (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a junction of Extruded Shells", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_ShellID;
	element.shell.shellClass = API_ExtrudedShellID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (shell)", err);
		return;
	}

	API_ElementMemo memo = {};

	element.shell.u.extrudedShell.shellShape.nCoords	= 5;
	element.shell.u.extrudedShell.shellShape.nSubPolys	= 1;
	element.shell.u.extrudedShell.shellShape.nArcs		= 1;

	memo.shellShapes[0].coords = (API_Coord**) BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellShapes[0].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.shell.u.extrudedShell.shellShape.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].bodyFlags = (bool**) BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nCoords + 1) * sizeof (bool), ALLOCATE_CLEAR, 0);

	(*memo.shellShapes[0].coords)[1].x = -2.0;
	(*memo.shellShapes[0].coords)[1].y =  0.0;
	(*memo.shellShapes[0].coords)[2].x = -2.5;
	(*memo.shellShapes[0].coords)[2].y =  2.0;
	(*memo.shellShapes[0].coords)[3].x =  2.5;
	(*memo.shellShapes[0].coords)[3].y =  2.0;
	(*memo.shellShapes[0].coords)[4].x =  2.0;
	(*memo.shellShapes[0].coords)[4].y =  0.0;
	(*memo.shellShapes[0].coords)[5] =  (*memo.shellShapes[0].coords)[1];
	(*memo.shellShapes[0].pends)[1] = element.shell.u.extrudedShell.shellShape.nCoords;
	(*memo.shellShapes[0].parcs)[0].begIndex = 2;
	(*memo.shellShapes[0].parcs)[0].endIndex = 3;
	(*memo.shellShapes[0].parcs)[0].arcAngle = -200 * DEGRAD;
	(*memo.shellShapes[0].bodyFlags)[1] = true;
	(*memo.shellShapes[0].bodyFlags)[2] = true;
	(*memo.shellShapes[0].bodyFlags)[3] = true;
	(*memo.shellShapes[0].bodyFlags)[4] = false;
	(*memo.shellShapes[0].bodyFlags)[5] = (*memo.shellShapes[0].bodyFlags)[1];

	element.shell.u.extrudedShell.begPlaneTilt = 120 * DEGRAD;
	element.shell.u.extrudedShell.endPlaneTilt =  60 * DEGRAD;

	GS::Array<API_Guid> createdShells;

	// create 3 shell elements and connect them
	for (Int32 i = 0; i < 3 && err == NoError; i++) {
		element.shell.u.extrudedShell.begC.x = centerPoint.x - 10.0 * cos (i * 2*PI/3);
		element.shell.u.extrudedShell.begC.y = centerPoint.y - 10.0 * sin (i * 2*PI/3);
		element.shell.u.extrudedShell.extrusionVector.x = 20.0 * cos (i * 2*PI/3);
		element.shell.u.extrudedShell.extrusionVector.y = 20.0 * sin (i * 2*PI/3);
		element.shell.u.extrudedShell.extrusionVector.z = 0.0;
		err = ACAPI_Element_Create (&element, &memo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create (extruded)", err);
		else
			createdShells.Push (element.header.guid);
	}

	if (err == NoError)
		ACAPI_Element_Trim_Elements (createdShells);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateExtrudedShell


// -----------------------------------------------------------------------------
// Create a Revolved Shell
// -----------------------------------------------------------------------------
void	Do_CreateRevolvedShell (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a Revolved Shell", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_ShellID;
	element.shell.shellClass = API_RevolvedShellID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (shell)", err);
		return;
	}

	double* tmx = element.shell.basePlane.tmx;
	tmx[ 0]	= 1.0;				tmx[ 4]	= 0.0;				tmx[ 8]	= 0.0;
	tmx[ 1]	= 0.0;				tmx[ 5]	= 0.0;				tmx[ 9]	= 1.0;
	tmx[ 2]	= 0.0;				tmx[ 6]	=-1.0;				tmx[10]	= 0.0;
	tmx[ 3]	= centerPoint.x;	tmx[ 7]	= centerPoint.y;	tmx[11]	= 0.0;

	element.shell.isFlipped = true;

	element.shell.u.revolvedShell.slantAngle			= 0;
	element.shell.u.revolvedShell.revolutionAngle		= 360 * DEGRAD;
	element.shell.u.revolvedShell.distortionAngle		= 90 * DEGRAD;
	element.shell.u.revolvedShell.segmentedSurfaces		= false;
	element.shell.u.revolvedShell.segmentType			= APIShellBase_SegmentsByCircle;
	element.shell.u.revolvedShell.segmentsByCircle		= 36;
	element.shell.u.revolvedShell.axisBase.tmx[0]		= 1.0;
	element.shell.u.revolvedShell.axisBase.tmx[6]		= 1.0;
	element.shell.u.revolvedShell.axisBase.tmx[9]		= -1.0;
	element.shell.u.revolvedShell.distortionVector.x	= 0.0;
	element.shell.u.revolvedShell.distortionVector.y	= 0.0;
	element.shell.u.revolvedShell.begAngle				= 0.0;

	API_ElementMemo memo = {};

	// constructing the revolving profile polyline
	element.shell.u.revolvedShell.shellShape.nCoords	= 13;
	element.shell.u.revolvedShell.shellShape.nSubPolys	= 1;
	element.shell.u.revolvedShell.shellShape.nArcs		= 3;

	memo.shellShapes[0].coords = (API_Coord**) BMAllocateHandle ((element.shell.u.revolvedShell.shellShape.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellShapes[0].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.shell.u.revolvedShell.shellShape.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.shell.u.revolvedShell.shellShape.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].bodyFlags = (bool**) BMAllocateHandle ((element.shell.u.revolvedShell.shellShape.nCoords + 1) * sizeof (bool), ALLOCATE_CLEAR, 0);

	(*memo.shellShapes[0].coords)[ 1].x =  0.0;
	(*memo.shellShapes[0].coords)[ 1].y =  0.0;
	(*memo.shellShapes[0].coords)[ 2].x =  0.4;
	(*memo.shellShapes[0].coords)[ 2].y =  5.0;
	(*memo.shellShapes[0].coords)[ 3].x =  1.0;
	(*memo.shellShapes[0].coords)[ 3].y =  5.0;
	(*memo.shellShapes[0].coords)[ 4].x =  1.0;
	(*memo.shellShapes[0].coords)[ 4].y =  6.0;
	(*memo.shellShapes[0].coords)[ 5].x =  1.7;
	(*memo.shellShapes[0].coords)[ 5].y =  6.0;
	(*memo.shellShapes[0].coords)[ 6].x =  1.7;
	(*memo.shellShapes[0].coords)[ 6].y =  7.0;
	(*memo.shellShapes[0].coords)[ 7].x =  2.4;
	(*memo.shellShapes[0].coords)[ 7].y =  7.0;
	(*memo.shellShapes[0].coords)[ 8].x =  2.4;
	(*memo.shellShapes[0].coords)[ 8].y =  7.4;
	(*memo.shellShapes[0].coords)[ 9].x =  0.0;
	(*memo.shellShapes[0].coords)[ 9].y =  7.7;
	(*memo.shellShapes[0].coords)[10].x =  0.0;
	(*memo.shellShapes[0].coords)[10].y =  8.0;
	(*memo.shellShapes[0].coords)[11].x =  8.0;
	(*memo.shellShapes[0].coords)[11].y = 10.0;
	(*memo.shellShapes[0].coords)[12].x = 12.0;
	(*memo.shellShapes[0].coords)[12].y =  0.0;
	(*memo.shellShapes[0].coords)[13] = (*memo.shellShapes[0].coords)[1];
	(*memo.shellShapes[0].pends)[1] = element.shell.u.revolvedShell.shellShape.nCoords;
	(*memo.shellShapes[0].parcs)[0].begIndex = 1;
	(*memo.shellShapes[0].parcs)[0].endIndex = 2;
	(*memo.shellShapes[0].parcs)[0].arcAngle = -0.143099565651258;
	(*memo.shellShapes[0].parcs)[1].begIndex = 10;
	(*memo.shellShapes[0].parcs)[1].endIndex = 11;
	(*memo.shellShapes[0].parcs)[1].arcAngle = 0.566476134070805;
	(*memo.shellShapes[0].parcs)[2].begIndex = 11;
	(*memo.shellShapes[0].parcs)[2].endIndex = 12;
	(*memo.shellShapes[0].parcs)[2].arcAngle = 0.385936923743763;
	(*memo.shellShapes[0].bodyFlags)[ 1] = true;
	(*memo.shellShapes[0].bodyFlags)[ 2] = true;
	(*memo.shellShapes[0].bodyFlags)[ 3] = true;
	(*memo.shellShapes[0].bodyFlags)[ 4] = true;
	(*memo.shellShapes[0].bodyFlags)[ 5] = true;
	(*memo.shellShapes[0].bodyFlags)[ 6] = true;
	(*memo.shellShapes[0].bodyFlags)[ 7] = true;
	(*memo.shellShapes[0].bodyFlags)[ 8] = true;
	(*memo.shellShapes[0].bodyFlags)[ 9] = true;
	(*memo.shellShapes[0].bodyFlags)[10] = true;
	(*memo.shellShapes[0].bodyFlags)[11] = true;
	(*memo.shellShapes[0].bodyFlags)[12] = false;
	(*memo.shellShapes[0].bodyFlags)[13] = (*memo.shellShapes[0].bodyFlags)[1];

	// constructing the shell contour data
	element.shell.hasContour = false;		// this shell will not be clipped
	element.shell.numHoles = 1;				// but will have a hole

	USize nContours = element.shell.numHoles + (element.shell.hasContour ? 1 : 0);
	memo.shellContours = (API_ShellContourData *) BMAllocatePtr (nContours * sizeof (API_ShellContourData), ALLOCATE_CLEAR, 0);
	memo.shellContours[0].poly.nCoords = 5;
	memo.shellContours[0].poly.nSubPolys = 1;
	memo.shellContours[0].poly.nArcs = 1;
	memo.shellContours[0].coords = (API_Coord**) BMAllocateHandle ((memo.shellContours[0].poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellContours[0].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((memo.shellContours[0].poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellContours[0].parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (memo.shellContours[0].poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));

	(*memo.shellContours[0].coords)[1].x = -1.5;
	(*memo.shellContours[0].coords)[1].y = -0.3;
	(*memo.shellContours[0].coords)[2].x = -1.5;
	(*memo.shellContours[0].coords)[2].y =  3.1;
	(*memo.shellContours[0].coords)[3].x =  1.5;
	(*memo.shellContours[0].coords)[3].y =  3.1;
	(*memo.shellContours[0].coords)[4].x =  1.5;
	(*memo.shellContours[0].coords)[4].y = -0.3;
	(*memo.shellContours[0].coords)[5] = (*memo.shellContours[0].coords)[1];
	(*memo.shellContours[0].pends)[1] = 5;
	(*memo.shellContours[0].parcs)[0].begIndex = 2;
	(*memo.shellContours[0].parcs)[0].endIndex = 3;
	(*memo.shellContours[0].parcs)[0].arcAngle = -240.0 * DEGRAD;

	memo.shellContours[0].height = -5.2;
	tmx = memo.shellContours[0].plane.tmx;
	tmx[ 0] = 1.0;		tmx[ 4] = 0.0;		tmx[ 8] = 0.0;
	tmx[ 1] = 0.0;		tmx[ 5] = 1.0;		tmx[ 9] = 0.0;
	tmx[ 2] = 0.0;		tmx[ 6] = 0.0;		tmx[10] = 1.0;
	tmx[ 3] = 0.0;		tmx[ 7] = 0.0;		tmx[11] = 10.0;

	// create the shell element
	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (revolved)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateRevolvedShell


// -----------------------------------------------------------------------------
// Create a Ruled Shell
// -----------------------------------------------------------------------------
void	Do_CreateRuledShell (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a Ruled Shell", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_ShellID;
	element.shell.shellClass = API_RuledShellID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (shell)", err);
		return;
	}

	double dx = 10.0;
	double dy = 10.0;

	double* tmx = element.shell.basePlane.tmx;
	tmx[ 0]	= 1.0;						tmx[ 4]	= 0.0;						tmx[ 8]	= 0.0;
	tmx[ 1]	= 0.0;						tmx[ 5]	= cos (45*DEGRAD);			tmx[ 9]	= sin (45*DEGRAD);
	tmx[ 2]	= 0.0;						tmx[ 6]	= -sin (45*DEGRAD);			tmx[10]	= cos (45*DEGRAD);
	tmx[ 3]	= centerPoint.x - dx/2.0;	tmx[ 7]	= centerPoint.y - dy/2.0;	tmx[11]	= 0.0;

	API_ElementMemo memo = {};

	// constructing the two ruling profiles
	element.shell.u.ruledShell.shellShape1.nCoords		= 3;
	element.shell.u.ruledShell.shellShape1.nSubPolys	= 1;
	element.shell.u.ruledShell.shellShape1.nArcs		= 0;
	tmx = element.shell.u.ruledShell.plane1.tmx;
	tmx[ 0]	=  0.0;		tmx[ 4]	=  0.0;		tmx[ 8]	=  1.0;
	tmx[ 1]	=  0.0;		tmx[ 5]	=  1.0;		tmx[ 9]	=  0.0;
	tmx[ 2]	= -1.0;		tmx[ 6]	=  0.0;		tmx[10]	=  0.0;
	tmx[ 3]	=  0.0;		tmx[ 7]	=  0.0;		tmx[11]	=  0.0;

	element.shell.u.ruledShell.shellShape2.nCoords		= 3;
	element.shell.u.ruledShell.shellShape2.nSubPolys	= 1;
	element.shell.u.ruledShell.shellShape2.nArcs		= 0;
	tmx = element.shell.u.ruledShell.plane2.tmx;
	tmx[ 0]	=  0.0;		tmx[ 4]	=  0.0;		tmx[ 8]	= -1.0;
	tmx[ 1]	=  0.0;		tmx[ 5]	=  1.0;		tmx[ 9]	=  0.0;
	tmx[ 2]	=  1.0;		tmx[ 6]	=  0.0;		tmx[10]	=  0.0;
	tmx[ 3]	=  dx;		tmx[ 7]	=  0.0;		tmx[11]	=  3.0;

	memo.shellShapes[0].coords = (API_Coord**) BMAllocateHandle ((element.shell.u.ruledShell.shellShape1.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellShapes[0].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.shell.u.ruledShell.shellShape1.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].bodyFlags = (bool**) BMAllocateHandle ((element.shell.u.ruledShell.shellShape1.nCoords + 1) * sizeof (bool), ALLOCATE_CLEAR, 0);
	memo.shellShapes[1].coords = (API_Coord**) BMAllocateHandle ((element.shell.u.ruledShell.shellShape2.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellShapes[1].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.shell.u.ruledShell.shellShape2.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellShapes[1].bodyFlags = (bool**) BMAllocateHandle ((element.shell.u.ruledShell.shellShape2.nCoords + 1) * sizeof (bool), ALLOCATE_CLEAR, 0);

	(*memo.shellShapes[0].coords)[1].x = 4.0;
	(*memo.shellShapes[0].coords)[1].y = 0.0;
	(*memo.shellShapes[0].coords)[2].x = 0.0;
	(*memo.shellShapes[0].coords)[2].y = dy;
	(*memo.shellShapes[0].coords)[3] = (*memo.shellShapes[0].coords)[1];

	(*memo.shellShapes[0].pends)[1] = element.shell.u.ruledShell.shellShape1.nCoords;

	(*memo.shellShapes[0].bodyFlags)[ 1] = true;
	(*memo.shellShapes[0].bodyFlags)[ 2] = false;
	(*memo.shellShapes[0].bodyFlags)[ 3] = (*memo.shellShapes[0].bodyFlags)[ 1];

	(*memo.shellShapes[1].coords)[1].x = 4.0;
	(*memo.shellShapes[1].coords)[1].y = 0.0;
	(*memo.shellShapes[1].coords)[2].x = 0.0;
	(*memo.shellShapes[1].coords)[2].y = dy;
	(*memo.shellShapes[1].coords)[3] = (*memo.shellShapes[1].coords)[1];

	(*memo.shellShapes[1].pends)[1] = element.shell.u.ruledShell.shellShape2.nCoords;

	(*memo.shellShapes[1].bodyFlags)[ 1] = true;
	(*memo.shellShapes[1].bodyFlags)[ 2] = false;
	(*memo.shellShapes[1].bodyFlags)[ 3] = (*memo.shellShapes[1].bodyFlags)[ 1];

	// constructing the shell contour data
	element.shell.hasContour = true;
	memo.shellContours = (API_ShellContourData *) BMAllocatePtr (sizeof (API_ShellContourData), ALLOCATE_CLEAR, 0);
	memo.shellContours[0].poly.nCoords = 3;
	memo.shellContours[0].poly.nSubPolys = 1;
	memo.shellContours[0].poly.nArcs = 2;
	memo.shellContours[0].coords = (API_Coord**) BMAllocateHandle ((memo.shellContours[0].poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellContours[0].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((memo.shellContours[0].poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellContours[0].parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (memo.shellContours[0].poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));

	(*memo.shellContours[0].coords)[1].x = 0.0;
	(*memo.shellContours[0].coords)[1].y = dy;
	(*memo.shellContours[0].coords)[2].x = dx;
	(*memo.shellContours[0].coords)[2].y = 0.0;
	(*memo.shellContours[0].coords)[3] = (*memo.shellContours[0].coords)[1];
	(*memo.shellContours[0].pends)[1] = 3;
	(*memo.shellContours[0].parcs)[0].begIndex = 1;
	(*memo.shellContours[0].parcs)[0].endIndex = 2;
	(*memo.shellContours[0].parcs)[0].arcAngle = PI/2;
	(*memo.shellContours[0].parcs)[1].begIndex = 2;
	(*memo.shellContours[0].parcs)[1].endIndex = 3;
	(*memo.shellContours[0].parcs)[1].arcAngle = PI/2;

	tmx = memo.shellContours[0].plane.tmx;
	tmx[ 0] = 1.0;		tmx[ 4] = 0.0;		tmx[ 8] = 0.0;
	tmx[ 1] = 0.0;		tmx[ 5] = 1.0;		tmx[ 9] = 0.0;
	tmx[ 2] = 0.0;		tmx[ 6] = 0.0;		tmx[10] = 1.0;
	tmx[ 3] = 0.0;		tmx[ 7] = 0.0;		tmx[11] = 0.0;

	// create the shell element
	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (ruled)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateRuledShell


// -----------------------------------------------------------------------------
// Create a Morph
// -----------------------------------------------------------------------------
void	Do_CreateMorph (void)
{
	API_Coord referencePoint;
	if (!ClickAPoint ("Click a point to create a Morph", &referencePoint))
		return;

	API_Element element = {};
	element.header.type = API_MorphID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (morph)", err);
		return;
	}

	double* tmx = element.morph.tranmat.tmx;
	tmx[ 0]	= 1.0;				tmx[ 4]	= 0.0;				tmx[ 8]	= 0.0;
	tmx[ 1]	= 0.0;				tmx[ 5]	= 1.0;				tmx[ 9]	= 0.0;
	tmx[ 2]	= 0.0;				tmx[ 6]	= 0.0;				tmx[10]	= 1.0;
	tmx[ 3]	= referencePoint.x;	tmx[ 7]	= referencePoint.y;	tmx[11]	= 1.0;

	// build the body structure
	void* bodyData = nullptr;
	ACAPI_Body_Create (nullptr, nullptr, &bodyData);

	// define the vertices
	double dx = 3.0, dy = 2.0, dz = 1.0;	// the dimensions of the morph element to be created
	API_Coord3D coords [] = { {     0.0,     0.0,     0.0  },	// #1
							  {      dx,     0.0,     0.0  },	// #2
							  {     0.0,      dy,     0.0  },	// #3
							  {     0.0,     0.0,      dz  },	// #4
							  {  dx/4.0,     0.0,  dz/4.0  },	// #5
							  {  dx/2.0,     0.0,  dz/4.0  },	// #6
							  {  dx/4.0,     0.0,  dz/2.0  },	// #7
							  {     0.0,  dy/2.0,  dz/4.0  },	// #8
							  {     0.0,  dy/4.0,  dz/4.0  },	// #9
							  {     0.0,  dy/4.0,  dz/2.0  } };	// #10
	UInt32 vertices[10];
	for (UIndex i = 0; i < 10; i++)
		ACAPI_Body_AddVertex (bodyData, coords[i], vertices[i]);

	// connect the vertices to determine edges
	Int32 edges[15];
	ACAPI_Body_AddEdge (bodyData, vertices[0], vertices[1], edges[0]);
	ACAPI_Body_AddEdge (bodyData, vertices[0], vertices[2], edges[1]);
	ACAPI_Body_AddEdge (bodyData, vertices[0], vertices[3], edges[2]);
	ACAPI_Body_AddEdge (bodyData, vertices[1], vertices[2], edges[3]);
	ACAPI_Body_AddEdge (bodyData, vertices[1], vertices[3], edges[4]);
	ACAPI_Body_AddEdge (bodyData, vertices[2], vertices[3], edges[5]);

	ACAPI_Body_AddEdge (bodyData, vertices[4], vertices[5], edges[6]);
	ACAPI_Body_AddEdge (bodyData, vertices[5], vertices[6], edges[7]);
	ACAPI_Body_AddEdge (bodyData, vertices[6], vertices[4], edges[8]);

	ACAPI_Body_AddEdge (bodyData, vertices[7], vertices[8], edges[9]);
	ACAPI_Body_AddEdge (bodyData, vertices[8], vertices[9], edges[10]);
	ACAPI_Body_AddEdge (bodyData, vertices[9], vertices[7], edges[11]);

	ACAPI_Body_AddEdge (bodyData, vertices[4], vertices[8], edges[12]);
	ACAPI_Body_AddEdge (bodyData, vertices[6], vertices[9], edges[13]);
	ACAPI_Body_AddEdge (bodyData, vertices[5], vertices[7], edges[14]);

	// add polygon normal vector
	Int32	polyNormals[1];
	API_Vector3D normal;
	normal.x = normal.z = 0.0;
	normal.y = 1.0;
	ACAPI_Body_AddPolyNormal (bodyData, normal, polyNormals[0]);

	// determine polygons from edges
	API_OverriddenAttribute material;

	UInt32 polygons[7];
	material = ACAPI_CreateAttributeIndex (1);
	ACAPI_Body_AddPolygon (bodyData,
						   {edges[0], edges[4], -edges[2], 0, -edges[8], -edges[7], -edges[6]},
						   -polyNormals[0],
						   material,
						   polygons[0]);

	material = ACAPI_CreateAttributeIndex (2);
	ACAPI_Body_AddPolygon (bodyData,
						   {edges[1], -edges[3], -edges[0]},
						   0,
						   material,
						   polygons[1]);

	material = ACAPI_CreateAttributeIndex (3);
	ACAPI_Body_AddPolygon (bodyData,
						   {-edges[4], edges[3], edges[5]},
						   0,
						   material,
						   polygons[2]);

	material = ACAPI_CreateAttributeIndex (4);
	ACAPI_Body_AddPolygon (bodyData,
						   {edges[2], -edges[5], -edges[1], 0, -edges[11], -edges[10], -edges[9]},
						   0,
						   material,
						   polygons[3]);

	material = ACAPI_CreateAttributeIndex (5);
	ACAPI_Body_AddPolygon (bodyData,
						   {edges[6], edges[14], edges[9], -edges[12]},
						   0,
						   material,
						   polygons[4]);

	material = ACAPI_CreateAttributeIndex (6);
	ACAPI_Body_AddPolygon (bodyData,
						   {edges[7], edges[13], edges[11], -edges[14]},
						   0,
						   material,
						   polygons[5]);

	material = ACAPI_CreateAttributeIndex (7);
	ACAPI_Body_AddPolygon (bodyData,
						   {-edges[13], edges[8], edges[12], edges[10]},
						   0,
						   material,
						   polygons[6]);

	// close the body and copy it to the memo
	API_ElementMemo memo = {};
	ACAPI_Body_Finish (bodyData, &memo.morphBody, &memo.morphMaterialMapTable);
	ACAPI_Body_Dispose (&bodyData);

	// create the morph element
	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (morph)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateMorph


// -----------------------------------------------------------------------------
// Create a special shaped slab with custom edge trims
// -----------------------------------------------------------------------------
void	Do_CreateSlab (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a Slab", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_SlabID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (slab)", err);
		return;
	}

	element.slab.poly.nCoords	= 65;
	element.slab.poly.nSubPolys	= 9;
	element.slab.poly.nArcs		= 24;

	API_ElementMemo memo = {};
	memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((element.slab.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.slab.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.slab.poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	memo.edgeTrims = reinterpret_cast<API_EdgeTrim**> (BMAllocateHandle ((element.slab.poly.nCoords + 1) * sizeof (API_EdgeTrim), ALLOCATE_CLEAR, 0));
	memo.sideMaterials = reinterpret_cast<API_OverriddenAttribute*> (BMAllocatePtr ((element.slab.poly.nCoords + 1) * sizeof (API_OverriddenAttribute), ALLOCATE_CLEAR, 0));

	double divisionAngle = PI/12.0;
	double sideAngle1 = 120 * DEGRAD;
	double sideAngle2 = 160 * DEGRAD;

	Int32 iCoord = 1;
	for (Int32 i = 0; i < 24; i++) {						// slab contour coordinates
		double radius = ((i % 3 == 0) ? 6.0 : 4.0);
		(*memo.coords)[iCoord].x = centerPoint.x + radius * cos (i * divisionAngle);
		(*memo.coords)[iCoord].y = centerPoint.y + radius * sin (i * divisionAngle);
		(*memo.edgeTrims)[iCoord].sideType = APIEdgeTrim_CustomAngle;
		(*memo.edgeTrims)[iCoord].sideAngle = ((i % 3 == 1) ? sideAngle2 : sideAngle1);
		memo.sideMaterials[iCoord] = ((i % 3 == 1) ? element.slab.topMat : element.slab.sideMat);
		++iCoord;
	}
	(*memo.coords)[iCoord] = (*memo.coords)[1];
	(*memo.pends)[1] = iCoord;
	(*memo.edgeTrims)[iCoord].sideType = (*memo.edgeTrims)[1].sideType;
	(*memo.edgeTrims)[iCoord].sideAngle = (*memo.edgeTrims)[1].sideAngle;
	memo.sideMaterials[iCoord] = memo.sideMaterials[1];
	++iCoord;

	Int32 iArc = 0;
	for (Int32 i = 0; i < 8; i++) {							// curved edges of the slab contour
		(*memo.parcs)[iArc].begIndex = i * 3 + 2;
		(*memo.parcs)[iArc].endIndex = i * 3 + 3;
		(*memo.parcs)[iArc].arcAngle = divisionAngle;
		++iArc;
	}

	double outerRadius = 3.5;								// coordinates of the slab holes
	double innerRadius = 0.5;
	for (Int32 i = 0; i < 8; i++) {
		Int32 iStart = iCoord;
		double angle = (i * 3 + 1) * divisionAngle;
		(*memo.coords)[iCoord].x = centerPoint.x + outerRadius * cos (angle);
		(*memo.coords)[iCoord].y = centerPoint.y + outerRadius * sin (angle);
		(*memo.edgeTrims)[iCoord].sideType = APIEdgeTrim_CustomAngle;
		(*memo.edgeTrims)[iCoord].sideAngle = sideAngle1;
		memo.sideMaterials[iCoord] = element.slab.sideMat;
		++iCoord;
		(*memo.coords)[iCoord].x = centerPoint.x + innerRadius * cos (angle);
		(*memo.coords)[iCoord].y = centerPoint.y + innerRadius * sin (angle);
		(*memo.edgeTrims)[iCoord].sideType = APIEdgeTrim_CustomAngle;
		(*memo.edgeTrims)[iCoord].sideAngle = sideAngle2;
		memo.sideMaterials[iCoord] = element.slab.topMat;
		++iCoord;

		angle = (i * 3 + 2) * divisionAngle;
		(*memo.coords)[iCoord].x = centerPoint.x + innerRadius * cos (angle);
		(*memo.coords)[iCoord].y = centerPoint.y + innerRadius * sin (angle);
		(*memo.edgeTrims)[iCoord].sideType = APIEdgeTrim_CustomAngle;
		(*memo.edgeTrims)[iCoord].sideAngle = sideAngle1;
		memo.sideMaterials[iCoord] = element.slab.sideMat;
		++iCoord;
		(*memo.coords)[iCoord].x = centerPoint.x + outerRadius * cos (angle);
		(*memo.coords)[iCoord].y = centerPoint.y + outerRadius * sin (angle);
		(*memo.edgeTrims)[iCoord].sideType = APIEdgeTrim_CustomAngle;
		(*memo.edgeTrims)[iCoord].sideAngle = sideAngle2;
		memo.sideMaterials[iCoord] = element.slab.topMat;
		++iCoord;

		// kontur lezarasa
		(*memo.coords)[iCoord] = (*memo.coords)[iStart];
		(*memo.pends)[i + 2] = iCoord;
		(*memo.edgeTrims)[iCoord].sideType = (*memo.edgeTrims)[iStart].sideType;
		(*memo.edgeTrims)[iCoord].sideAngle = (*memo.edgeTrims)[iStart].sideAngle;
		memo.sideMaterials[iCoord] = memo.sideMaterials[iStart];
		++iCoord;

		(*memo.parcs)[iArc].begIndex = iStart + 1;			// curved edges of the slab holes
		(*memo.parcs)[iArc].endIndex = iStart + 2;
		(*memo.parcs)[iArc].arcAngle = divisionAngle;
		++iArc;
		(*memo.parcs)[iArc].begIndex = iStart + 3;
		(*memo.parcs)[iArc].endIndex = iStart + 4;
		(*memo.parcs)[iArc].arcAngle = -divisionAngle;
		++iArc;
	}

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (slab)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateSlab


// -----------------------------------------------------------------------------
// Create a curved wall using the default settings and user input
// -----------------------------------------------------------------------------
void		Do_CreateCurvedWall (void)
{
	API_Coord			c;
	API_Element			element = {}, mask = {};
	GSErrCode			err;

	element.header.type = API_WallID;
	ACAPI_Element_GetDefaults (&element, nullptr);

	// input the arc
	if (!GetAnArc ("Input an arc", &c, &element.wall.begC, &element.wall.endC))
		return;

	double	fa = ComputeFiPtr (&c, &element.wall.begC);
	double	fe = ComputeFiPtr (&c, &element.wall.endC);
	while (fa + EPS >= 2*PI)
		fa -= 2*PI;
	while (fe + EPS >= 2*PI)
		fe -= 2*PI;
	element.wall.angle = fa - fe;

    element.wall.relativeTopStory = 0;
    element.wall.height = 5.0;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (curved wall)", err);
		return;
	}

	ACAPI_Element_Get (&element);
	ACAPI_WriteReport ("GUID of the Curved Wall: %s", true, APIGuidToString (element.header.guid).ToCStr ().Get ());

	err = ACAPI_CallUndoableCommand ("Modify curved wall",
		[&] () -> GSErrCode {
			ACAPI_ELEMENT_MASK_CLEAR (mask);
			double	len = DistCPtr (&c, &element.wall.endC);
			fe = fa + element.wall.angle * 3/4;
			fa += element.wall.angle/4;
			element.wall.angle /= 2;
			element.wall.endC.x = c.x + len * cos (fe);
			element.wall.endC.y = c.y + len * sin (fe);
			element.wall.begC.x = c.x + len * cos (fa);
			element.wall.begC.y = c.y + len * sin (fa);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, begC);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, endC);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, angle);

			return ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
		});

	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Change (curved wall)", err);

	return;
}		// Do_CreateCurvedWall


// -----------------------------------------------------------------------------
// Create a curved beam using the default settings and user input
// -----------------------------------------------------------------------------
void		Do_CreateCurvedBeam (void)
{
	API_Coord		centrePos, startPos, endPos;
	bool			isArcNegative;
	API_Element		element = {};
	API_ElementMemo memo = {};
	GSErrCode		err;

	element.header.type = API_BeamID;
	ACAPI_Element_GetDefaults (&element, &memo);

	// input the arc
	if (!GetAnArc ("Input an arc", &centrePos, &startPos, &endPos, &isArcNegative))
		return;

	element.beam.begC = isArcNegative ? endPos : startPos;
	element.beam.endC = isArcNegative ? startPos : endPos;
	double	fa = ComputeFiPtr (&centrePos, &element.beam.begC, !isArcNegative);
	double	fe = ComputeFiPtr (&centrePos, &element.beam.endC, isArcNegative);
	element.beam.curveAngle = fe - fa;
	while (element.beam.curveAngle + EPS >= 2*PI)
		element.beam.curveAngle -= 2*PI;
	while (element.beam.curveAngle - EPS <= -2*PI)
		element.beam.curveAngle += 2*PI;

	// drill circular holes into the beam
	const double holeDistance = 0.5;
	double radius = DistCPtr (&centrePos, &element.beam.begC);
	double length = radius * fabs (element.beam.curveAngle);
	Int32 nHoles = (Int32) (length / holeDistance);
	if (nHoles > 0) {
		memo.beamHoles = reinterpret_cast<API_Beam_Hole**> (BMAllocateHandle (nHoles * sizeof (API_Beam_Hole), ALLOCATE_CLEAR, 0));
		if (memo.beamHoles != nullptr) {
			for (Int32 i = 0; i < nHoles; ++i) {
				(*memo.beamHoles)[i].holeType		= APIBHole_Circular;
				(*memo.beamHoles)[i].holeContureOn	= element.beam.holeContureOn;
				(*memo.beamHoles)[i].holeID			= i + 1;
				(*memo.beamHoles)[i].centerx		= (i + 1) * holeDistance;
				(*memo.beamHoles)[i].centerz		= element.beam.holeLevel;
				(*memo.beamHoles)[i].width			= element.beam.holeWidth;
				(*memo.beamHoles)[i].height			= element.beam.holeHeight;
			}
		}
	}

	// create the beam
	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (curved beam)", err);
		return;
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateCurvedBeam


// -----------------------------------------------------------------------------
// Create a Curtain Wall
// -----------------------------------------------------------------------------
static void	Do_CreateCurtainWall (const std::function<void (API_Element& element, API_ElementMemo& memo)>& processor)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	API_Coord			c = {};
	API_GetPolyType		polyInfo = {};
	GSErrCode			err;

	if (!ClickAPoint ("Enter the first corner of the curtain wall", &c))
		return;

	polyInfo.useStandardPetPalette = true;
	strcpy (polyInfo.prompt, "Enter the next corner of the curtain wall");
	polyInfo.method = APIPolyGetMethod_Polyline;
	polyInfo.startCoord.x = c.x;
	polyInfo.startCoord.y = c.y;

	err = ACAPI_UserInput_GetPoly (&polyInfo);
	if (err != NoError)
		return;

	element.header.type = API_CurtainWallID;
	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults", err);
		return;
	}

	memo.coords = polyInfo.coords;
	memo.parcs = polyInfo.parcs;
	element.curtainWall.nSegments = BMhGetSize (reinterpret_cast<GSHandle> (memo.coords)) / Sizeof32 (API_Coord) - 2;

	processor (element, memo);

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Create (Curtain Wall) [%ld]", true, err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}


void	Do_CreateCurtainWall_UsingPattern (void)
{
	Do_CreateCurtainWall ([] (API_Element& element, API_ElementMemo& memo) {
		// Create Pattern
		// 2 columns, each with 0.5 meter Fixed Sizes
		{
			memo.cWSegPrimaryPattern.nPattern = 2;
			memo.cWSegPrimaryPattern.endWithID = 1;
			memo.cWSegPrimaryPattern.logic = APICWSePL_FixedSizes;
			auto newPattern = reinterpret_cast<double*> (BMpAll (sizeof (double) * (memo.cWSegPrimaryPattern.nPattern)));
			for (UInt32 i = 0; i < memo.cWSegPrimaryPattern.nPattern; ++i)
				newPattern[i] = 0.5;

			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPrimaryPattern.pattern));
			memo.cWSegPrimaryPattern.pattern = newPattern;
		}
		// 6 rows, each with 0.5 meter size Fixed Sizes
		{
			memo.cWSegSecondaryPattern.nPattern = 6;
			memo.cWSegSecondaryPattern.endWithID = 5;
			memo.cWSegSecondaryPattern.logic = APICWSePL_FixedSizes;
			auto newPattern = reinterpret_cast<double*> (BMpAll (sizeof (double) * (memo.cWSegSecondaryPattern.nPattern)));
			for (UInt32 i = 0; i < memo.cWSegSecondaryPattern.nPattern; ++i)
				newPattern[i] = 0.5;

			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegSecondaryPattern.pattern));
			memo.cWSegSecondaryPattern.pattern = newPattern;
		}
		// set CW height to the pattern height
		element.curtainWall.height = memo.cWSegSecondaryPattern.nPattern * 0.5;

		// add new Panel Class with "API Panel" name
		{
			const UInt32 nPanelClasses = BMpGetSize (reinterpret_cast<GSPtr> (memo.cWallPanelDefaults)) / sizeof (API_CWPanelType);
			auto newPanelClasses = reinterpret_cast<API_CWPanelType*> (BMpAll ((nPanelClasses + 1) * sizeof (API_CWPanelType)));
			for (UInt32 i = 0; i < nPanelClasses; ++i) {
				newPanelClasses[i] = memo.cWallPanelDefaults[i];
			}
			if (nPanelClasses > 0)
				newPanelClasses[nPanelClasses] = memo.cWallPanelDefaults[0];

			newPanelClasses[nPanelClasses] = newPanelClasses[0];
			newPanelClasses[nPanelClasses].outerSurfaceMaterial = ACAPI_CreateAttributeIndex (48);
			newPanelClasses[nPanelClasses].innerSurfaceMaterial = ACAPI_CreateAttributeIndex (48);
			newPanelClasses[nPanelClasses].cutSurfaceMaterial = ACAPI_CreateAttributeIndex (48);
			GS::ucscpy (newPanelClasses[nPanelClasses].className, GS::UniString ("API Panel").ToUStr ().Get ());

			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWallPanelDefaults));
			memo.cWallPanelDefaults = newPanelClasses;
		}
		const UInt32 nPanelClasses = BMpGetSize (reinterpret_cast<GSPtr> (memo.cWallPanelDefaults)) / sizeof (API_CWPanelType);
		element.curtainWall.nPanelDefaults = nPanelClasses;

		// set the cells in the pattern
		{
			const UInt32 nCells = memo.cWSegPrimaryPattern.nPattern * memo.cWSegSecondaryPattern.nPattern;
			const UInt32 sizeCells = nCells * sizeof (API_CWSegmentPatternCellData);
			auto newCells = reinterpret_cast<API_CWSegmentPatternCellData*> (BMpAll (sizeCells));
			for (UInt32 i = 0; i < nCells; ++i) {
				newCells[i].crossingFrameType = (i % 2 == 0) ? APICWCFT_FromTopLeftToBottomRight
															 : APICWCFT_FromBottomLeftToTopRight;
				newCells[i].leftPanelID		= APICWPanelClass_FirstCustomClass + (nPanelClasses > 1 ? nPanelClasses - 2 : nPanelClasses - 1);
				newCells[i].rightPanelID	= APICWPanelClass_FirstCustomClass + nPanelClasses - 1;
				if (i % 2 == 1)
					GS::Swap (newCells[i].leftPanelID, newCells[i].rightPanelID);
				newCells[i].leftFrameID		= APICWFrameClass_Merged;
				newCells[i].bottomFrameID	= APICWFrameClass_Division;
				newCells[i].crossingFrameID	= APICWFrameClass_Division;
			}

			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPatternCells));
			memo.cWSegPatternCells = newCells;
		}
	});
}	// Do_CreateCurtainWall_UsingPattern


void	Do_CreateCurtainWall_WithCustomFrames (void)
{
	Do_CreateCurtainWall ([] (API_Element& element, API_ElementMemo& memo) {
		// Set empty pattern
		{
			memo.cWSegPrimaryPattern.nPattern = 1;
			memo.cWSegPrimaryPattern.logic = APICWSePL_NumberOfDivisions;
			memo.cWSegPrimaryPattern.nDivisions = 1;
			memo.cWSegPrimaryPattern.nFlexibleIDs = 1;
			memo.cWSegPrimaryPattern.endWithID = 0;
			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPrimaryPattern.pattern));
			memo.cWSegPrimaryPattern.pattern = reinterpret_cast<double*> (BMpAll (sizeof (double)));
			memo.cWSegPrimaryPattern.pattern[0] = 1;
			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPrimaryPattern.flexibleIDs));
			memo.cWSegPrimaryPattern.flexibleIDs = reinterpret_cast<UInt32*> (BMpAll (sizeof (UInt32)));
			memo.cWSegPrimaryPattern.flexibleIDs[0] = 0;

			memo.cWSegSecondaryPattern.nPattern = 1;
			memo.cWSegSecondaryPattern.logic = APICWSePL_NumberOfDivisions;
			memo.cWSegSecondaryPattern.nDivisions = 1;
			memo.cWSegSecondaryPattern.nFlexibleIDs = 1;
			memo.cWSegSecondaryPattern.endWithID = 0;
			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegSecondaryPattern.pattern));
			memo.cWSegSecondaryPattern.pattern = reinterpret_cast<double*> (BMpAll (sizeof (double)));
			memo.cWSegSecondaryPattern.pattern[0] = 1;
			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegSecondaryPattern.flexibleIDs));
			memo.cWSegSecondaryPattern.flexibleIDs = reinterpret_cast<UInt32*> (BMpAll (sizeof (UInt32)));
			memo.cWSegSecondaryPattern.flexibleIDs[0] = 0;

			BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPatternCells));
			memo.cWSegPatternCells = reinterpret_cast<API_CWSegmentPatternCellData*> (BMpAll (sizeof (API_CWSegmentPatternCellData)));
			memo.cWSegPatternCells[0].crossingFrameType = APICWCFT_NoCrossingFrame;
			memo.cWSegPatternCells[0].leftPanelID		= APICWPanelClass_Deleted;
			memo.cWSegPatternCells[0].rightPanelID		= APICWPanelClass_Deleted;
			memo.cWSegPatternCells[0].leftFrameID		= APICWFrameClass_Merged;
			memo.cWSegPatternCells[0].bottomFrameID		= APICWFrameClass_Merged;
			memo.cWSegPatternCells[0].crossingFrameID	= APICWFrameClass_Merged;
		}
		// set CW height to 3 meter
		element.curtainWall.height = 3;

		// Add custom frames
		{
			const UInt32 nFrameClasses = BMpGetSize (reinterpret_cast<GSPtr> (memo.cWallFrameDefaults)) / sizeof (API_CWFrameType);
			DBASSERT (nFrameClasses > 0);
			const UInt32 nCustomFrames = 8 * element.curtainWall.nSegments;
			memo.cWallFrames = reinterpret_cast<API_CWFrameType *> (BMpAll (sizeof (API_CWFrameType) * nCustomFrames));
			for (UInt32 i = 0; i < nCustomFrames; ++i) {
				memo.cWallFrames[i] = memo.cWallFrameDefaults[nFrameClasses - 1];
				memo.cWallFrames[i].segmentID = i / 8;
				memo.cWallFrames[i].cellID = 0;
			}

			for (UInt32 i = 0; i < element.curtainWall.nSegments; ++i) {
				memo.cWallFrames[0 + i*8].endRel.x = memo.cWallFrames[1 + i*8].begRel.x = 0.2;
				memo.cWallFrames[0 + i*8].endRel.y = memo.cWallFrames[1 + i*8].begRel.y = 0.5;
				memo.cWallFrames[1 + i*8].endRel.x = memo.cWallFrames[2 + i*8].begRel.x = 0.4;
				memo.cWallFrames[1 + i*8].endRel.y = memo.cWallFrames[2 + i*8].begRel.y = 0.6;
				memo.cWallFrames[2 + i*8].endRel.x = memo.cWallFrames[3 + i*8].begRel.x = 0.5;
				memo.cWallFrames[2 + i*8].endRel.y = memo.cWallFrames[3 + i*8].begRel.y = 0.8;
				memo.cWallFrames[3 + i*8].endRel.x = memo.cWallFrames[4 + i*8].begRel.x = 0.6;
				memo.cWallFrames[3 + i*8].endRel.y = memo.cWallFrames[4 + i*8].begRel.y = 0.6;
				memo.cWallFrames[4 + i*8].endRel.x = memo.cWallFrames[5 + i*8].begRel.x = 0.8;
				memo.cWallFrames[4 + i*8].endRel.y = memo.cWallFrames[5 + i*8].begRel.y = 0.5;
				memo.cWallFrames[5 + i*8].endRel.x = memo.cWallFrames[6 + i*8].begRel.x = 0.6;
				memo.cWallFrames[5 + i*8].endRel.y = memo.cWallFrames[6 + i*8].begRel.y = 0.4;
				memo.cWallFrames[6 + i*8].endRel.x = memo.cWallFrames[7 + i*8].begRel.x = 0.5;
				memo.cWallFrames[6 + i*8].endRel.y = memo.cWallFrames[7 + i*8].begRel.y = 0.2;
				memo.cWallFrames[7 + i*8].endRel.x = memo.cWallFrames[0 + i*8].begRel.x = 0.4;
				memo.cWallFrames[7 + i*8].endRel.y = memo.cWallFrames[0 + i*8].begRel.y = 0.4;
			}
		}
	});
}	// Do_CreateCurtainWall_WithCustomFrames


// -----------------------------------------------------------------------------
// Create a Stair on the floor plan
// -----------------------------------------------------------------------------
void	Do_CreateStair (void)
{
	API_Coord point;
	if (!ClickAPoint ("Enter the beginning of the Stair's baseline.", &point)) {
		return;
	}

	API_GetPolyType polyInfo = {};
	polyInfo.useStandardPetPalette = true;
	CHCopyC ("Enter the next coordinate of the Stair's baseline.", polyInfo.prompt);
	polyInfo.method = APIPolyGetMethod_Polyline;
	polyInfo.startCoord.x = point.x;
	polyInfo.startCoord.y = point.y;

	GSErrCode err = ACAPI_UserInput_GetPoly (&polyInfo);
	if (err != NoError) {
		ACAPI_WriteReport ("APIIo_GetPolyID has failed with error code %ld!", true, err);
		return;
	}

	API_Element element = {};
	API_ElementMemo memo = {};

	element.header.type = API_StairID;
	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetDefaults (Stair) has failed with error code %ld!", true, err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	element.stair.baselinePosition = APILP_Center;
	memo.stairBaseLine.coords = polyInfo.coords;
	memo.stairBaseLine.parcs = polyInfo.parcs;
	memo.stairBaseLine.polygon.nCoords = polyInfo.nCoords;
	memo.stairBaseLine.polygon.nArcs = polyInfo.nArcs;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Create (Stair) has failed with error code %ld!", true, err);
	}

	err = ACAPI_Element_Change (&element, nullptr, nullptr, 0, false);
	//asking user for reference point
	API_Coord c;
	ClickAPoint("Click reference point", &c);

	API_Neig item;
	GS::Array<API_Neig> items;
	API_EditPars editPars = {};
	editPars.typeID = APIEdit_Drag;
	editPars.withDelete = true;
	editPars.begC.x = (*memo.stairBaseLine.coords)[1].x + 20;
	editPars.begC.y = (*memo.stairBaseLine.coords)[1].y + 20;
	editPars.begC.z = 0;
	editPars.endC.x = c.x; //reference point
	editPars.endC.y = c.y; //reference point
	editPars.endC.z = 0;
	item.neigID = APINeig_StairOn;
	item.guid = element.header.guid;
	items.Push(item);
	err = ACAPI_Element_Edit(&items, editPars);

	ACAPI_DisposeElemMemoHdls (&memo);
}	// Do_CreateStair


	// -----------------------------------------------------------------------------
	// Create a Column on the floor plan
	// -----------------------------------------------------------------------------
void	Do_CreateColumn (void)
{
	API_Coord point;

	if (!ClickAPoint ("Click to place column at this position.", &point)) {
		return;
	}

	API_Element element = {};
	API_ElementMemo memo = {};

	element.header.type = API_ColumnID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, &memo);

	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetDefaults (Column) has failed with error code %ld!", true, err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	element.column.origoPos = point;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Create (Column) has failed with error code %ld!", true, err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}	// Do_CreateColumn


// -----------------------------------------------------------------------------
// Create a Beam on the floor plan
// -----------------------------------------------------------------------------
void	Do_CreateBeam (void)
{
	API_Coord pointBeg, pointEnd;

	if (!ClickAPoint ("Click the start position of the beam.", &pointBeg)) {
		return;
	}

	if (!ClickAPoint ("Click the end position of the beam.", &pointEnd)) {
		return;
	}

	API_Element element = {};
	API_ElementMemo memo = {};

	element.header.type = API_BeamID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, &memo);

	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetDefaults (Beam) has failed with error code %ld!", true, err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	element.beam.begC = pointBeg;
	element.beam.endC = pointEnd;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Create (Beam) has failed with error code %ld!", true, err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}	// Do_CreateBeam


// -----------------------------------------------------------------------------
// Create a Railing on the floor plan
// -----------------------------------------------------------------------------
void	Do_CreateRailing (void)
{
	API_Coord point;
	if (!ClickAPoint ("Enter the beginning of the Railing's reference line.", &point)) {
		return;
	}

	API_GetPolyType polyInfo = {};
	polyInfo.useStandardPetPalette = true;
	CHCopyC ("Enter the next coordinate of the Railing's reference line.", polyInfo.prompt);
	polyInfo.method = APIPolyGetMethod_Polyline;
	polyInfo.startCoord.x = point.x;
	polyInfo.startCoord.y = point.y;

	GSErrCode err = ACAPI_UserInput_GetPoly (&polyInfo);
	if (err != NoError) {
		ACAPI_WriteReport ("APIIo_GetPolyID has failed with error code %ld!", true, err);
		return;
	}

	API_Element element = {};
	API_ElementMemo memo = {};

	element.header.type = API_RailingID;
	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetDefaults (Railing) has failed with error code %ld!", true, err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	memo.coords = polyInfo.coords;
	memo.parcs = polyInfo.parcs;
	memo.polyZCoords = reinterpret_cast<double**> (BMhAllClear (sizeof (double) * (polyInfo.nCoords + 1)));
	element.railing.nVertices = polyInfo.nCoords;

	err = ACAPI_Element_Create (&element, &memo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Create (Railing) has failed with error code %ld!", true, err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}	// Do_CreateRailing


// -----------------------------------------------------------------------------
// Create meshes from irregular polygon
// -----------------------------------------------------------------------------
void	Do_CreateIrregularMesh (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a Mesh", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_MeshID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (mesh)", err);
		return;
	}

	element.mesh.poly.nCoords	= 5;
	element.mesh.poly.nSubPolys	= 1;
	element.mesh.poly.nArcs		= 0;

	API_ElementMemo memo = {};
	memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((element.mesh.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.mesh.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.mesh.poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	memo.meshPolyZ = reinterpret_cast<double**> (BMAllocateHandle ((element.mesh.poly.nCoords + 1) * sizeof (double), ALLOCATE_CLEAR, 0));

	(*memo.coords)[1].x = centerPoint.x + 0.0;
	(*memo.coords)[1].y = centerPoint.y + 0.0;
	(*memo.coords)[2].x = centerPoint.x + 5.0;
	(*memo.coords)[2].y = centerPoint.y + 3.0;
	(*memo.coords)[3].x = centerPoint.x + 5.0;
	(*memo.coords)[3].y = centerPoint.y + 0.0;
	(*memo.coords)[4].x = centerPoint.x + 0.0;
	(*memo.coords)[4].y = centerPoint.y + 2.0;
	(*memo.coords)[element.mesh.poly.nCoords] = (*memo.coords)[1];

	(*memo.pends)[1] = element.mesh.poly.nCoords;

	(*memo.meshPolyZ)[1] = 1.0;
	(*memo.meshPolyZ)[2] = 2.0;
	(*memo.meshPolyZ)[3] = 3.0;
	(*memo.meshPolyZ)[4] = 4.0;
	(*memo.meshPolyZ)[5] = (*memo.meshPolyZ)[1];

	err = ACAPI_Element_Create (&element, &memo);
	if (err == APIERR_IRREGULARPOLY) {
		API_RegularizedPoly poly = {};
		poly.coords = memo.coords;
		poly.pends = memo.pends;
		poly.parcs = memo.parcs;
		poly.vertexIDs = memo.vertexIDs;
		poly.needVertexAncestry = 1;
		Int32 nResult = 0;
		API_RegularizedPoly** polys = nullptr;
		err = ACAPI_Polygon_RegularizePolygon (&poly, &nResult, &polys);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create (mesh) regularize", err);
		if (err == NoError) {
			for (Int32 i = 0; i < nResult && err == NoError; i++) {
				element.mesh.poly.nCoords = BMhGetSize (reinterpret_cast<GSHandle> ((*polys)[i].coords)) / sizeof (API_Coord) - 1;
				element.mesh.poly.nSubPolys = BMhGetSize (reinterpret_cast<GSHandle> ((*polys)[i].pends)) / sizeof (Int32) - 1;
				element.mesh.poly.nArcs = BMhGetSize (reinterpret_cast<GSHandle> ((*polys)[i].parcs)) / sizeof (API_PolyArc);

				API_ElementMemo tmpMemo = {};
				tmpMemo.coords = (*polys)[i].coords;
				tmpMemo.pends = (*polys)[i].pends;
				tmpMemo.parcs = (*polys)[i].parcs;
				tmpMemo.vertexIDs = (*polys)[i].vertexIDs;

				tmpMemo.meshPolyZ = reinterpret_cast<double**> (BMAllocateHandle ((element.mesh.poly.nCoords + 1) * sizeof (double), ALLOCATE_CLEAR, 0));

				for (Int32 j = 1; j <= element.mesh.poly.nCoords; j++) {
					Int32 oldVertexIndex = (*(*polys)[i].vertexAncestry)[j];
					if (oldVertexIndex == 0) {				// new vertex after regularization, apply default
						(*tmpMemo.meshPolyZ)[j] = 0.0;
					} else {
						(*tmpMemo.meshPolyZ)[j] = (*memo.meshPolyZ)[oldVertexIndex];
					}
				}

				err = ACAPI_Element_Create (&element, &tmpMemo);
				if ((err != NoError && err != APIERR_IRREGULARPOLY) || (err == APIERR_IRREGULARPOLY && i == 0))
					ErrorBeep ("ACAPI_Element_Create (mesh) pieces", err);
				BMKillHandle (reinterpret_cast<GSHandle*> (&tmpMemo.meshPolyZ));
			}
			for (Int32 j = 0; j < nResult; ++j)
				ACAPI_Polygon_DisposeRegularizedPoly (&(*polys)[j]);
		}
		BMKillHandle (reinterpret_cast<GSHandle*> (&polys));
	}
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (mesh)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateIrregularMesh


// -----------------------------------------------------------------------------
// Create slabs from irregular polygon
// -----------------------------------------------------------------------------
void	Do_CreateIrregularSlab (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a Slab", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_SlabID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (slab)", err);
		return;
	}

	element.slab.poly.nCoords	= 5;
	element.slab.poly.nSubPolys	= 1;
	element.slab.poly.nArcs		= 0;

	API_ElementMemo memo = {};
	memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((element.slab.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.slab.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.slab.poly.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));


	memo.edgeTrims = reinterpret_cast<API_EdgeTrim**> (BMAllocateHandle ((element.slab.poly.nCoords + 1) * sizeof (API_EdgeTrim), ALLOCATE_CLEAR, 0));
	memo.sideMaterials = reinterpret_cast<API_OverriddenAttribute*> (BMAllocatePtr ((element.slab.poly.nCoords + 1) * sizeof (API_OverriddenAttribute), ALLOCATE_CLEAR, 0));


	(*memo.coords)[1].x = centerPoint.x + 0.0;
	(*memo.coords)[1].y = centerPoint.y + 0.0;
	(*memo.coords)[2].x = centerPoint.x + 5.0;
	(*memo.coords)[2].y = centerPoint.y + 3.0;
	(*memo.coords)[3].x = centerPoint.x + 5.0;
	(*memo.coords)[3].y = centerPoint.y + 0.0;
	(*memo.coords)[4].x = centerPoint.x + 0.0;
	(*memo.coords)[4].y = centerPoint.y + 2.0;
	(*memo.coords)[element.slab.poly.nCoords] = (*memo.coords)[1];

	(*memo.pends)[1] = element.slab.poly.nCoords;

	(*memo.edgeTrims)[1].sideType = APIEdgeTrim_CustomAngle;
	(*memo.edgeTrims)[1].sideAngle = 120 * DEGRAD;
	memo.sideMaterials[1] = element.slab.botMat;
	(*memo.edgeTrims)[2].sideType = APIEdgeTrim_CustomAngle;
	(*memo.edgeTrims)[2].sideAngle = 45 * DEGRAD;
	memo.sideMaterials[2] = element.slab.topMat;
	(*memo.edgeTrims)[3].sideType = APIEdgeTrim_CustomAngle;
	(*memo.edgeTrims)[3].sideAngle = 120 * DEGRAD;
	memo.sideMaterials[3] = element.slab.botMat;
	(*memo.edgeTrims)[4].sideType = APIEdgeTrim_CustomAngle;
	(*memo.edgeTrims)[4].sideAngle = 45 * DEGRAD;
	memo.sideMaterials[4] = element.slab.topMat;

	(*memo.edgeTrims)[element.slab.poly.nCoords].sideType = (*memo.edgeTrims)[1].sideType;
	(*memo.edgeTrims)[element.slab.poly.nCoords].sideAngle = (*memo.edgeTrims)[1].sideAngle;
	memo.sideMaterials[element.slab.poly.nCoords] = memo.sideMaterials[1];

	err = ACAPI_Element_Create (&element, &memo);
	if (err == APIERR_IRREGULARPOLY) {
		API_RegularizedPoly poly = {};
		poly.coords = memo.coords;
		poly.pends = memo.pends;
		poly.parcs = memo.parcs;
		poly.vertexIDs = memo.vertexIDs;
		poly.needEdgeAncestry = 1;
		Int32 nResult = 0;
		API_RegularizedPoly** polys = nullptr;
		err = ACAPI_Polygon_RegularizePolygon (&poly, &nResult, &polys);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create (slab) regularize", err);
		if (err == NoError) {
			for (Int32 i = 0; i < nResult; i++) {
				element.slab.poly.nCoords = BMhGetSize (reinterpret_cast<GSHandle> ((*polys)[i].coords)) / sizeof (API_Coord) - 1;
				element.slab.poly.nSubPolys = BMhGetSize (reinterpret_cast<GSHandle> ((*polys)[i].pends)) / sizeof (Int32) - 1;
				element.slab.poly.nArcs = BMhGetSize (reinterpret_cast<GSHandle> ((*polys)[i].parcs)) / sizeof (API_PolyArc);

				API_ElementMemo tmpMemo = {};
				tmpMemo.coords = (*polys)[i].coords;
				tmpMemo.pends = (*polys)[i].pends;
				tmpMemo.parcs = (*polys)[i].parcs;
				tmpMemo.vertexIDs = (*polys)[i].vertexIDs;

				tmpMemo.edgeTrims = reinterpret_cast<API_EdgeTrim**> (BMAllocateHandle ((element.slab.poly.nCoords + 1) * sizeof (API_EdgeTrim), ALLOCATE_CLEAR, 0));
				tmpMemo.sideMaterials = reinterpret_cast<API_OverriddenAttribute*> (BMAllocatePtr ((element.slab.poly.nCoords + 1) * sizeof (API_OverriddenAttribute), ALLOCATE_CLEAR, 0));


				for (Int32 j = 1; j <= element.slab.poly.nCoords; j++) {
					Int32 oldEdgeIndex = (*(*polys)[i].edgeAncestry)[j];
					if (oldEdgeIndex == 0) {				// new edge after regularization, apply default
						(*tmpMemo.edgeTrims)[j] .sideType = APIEdgeTrim_Vertical;
						(*tmpMemo.edgeTrims)[j] .sideAngle = PI / 4.0;
						tmpMemo.sideMaterials[j] = element.slab.sideMat;
					} else {
						(*tmpMemo.edgeTrims)[j] = (*memo.edgeTrims)[oldEdgeIndex];
						tmpMemo.sideMaterials[j] = memo.sideMaterials[oldEdgeIndex];
					}
				}

				err = ACAPI_Element_Create (&element, &tmpMemo);
				if (err != NoError)
					ErrorBeep ("ACAPI_Element_Create (slab) pieces", err);
				ACAPI_Polygon_DisposeRegularizedPoly (&(*polys)[i]);
				BMKillHandle (reinterpret_cast<GSHandle*> (&tmpMemo.edgeTrims));
				BMpFree (reinterpret_cast<GSPtr> (tmpMemo.sideMaterials));
			}
		}
		BMKillHandle (reinterpret_cast<GSHandle*> (&polys));
	}
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (slab)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateIrregularSlab


// -----------------------------------------------------------------------------
// Create an extruded shell from irregular polyline
// -----------------------------------------------------------------------------
void 	Do_CreateIrregularExtShell (void)
{
	API_Coord centerPoint;
	if (!ClickAPoint ("Click a center point to create a junction of Extruded Shells", &centerPoint))
		return;

	API_Element element = {};
	element.header.type = API_ShellID;
	element.shell.shellClass = API_ExtrudedShellID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (shell)", err);
		return;
	}

	API_ElementMemo memo = {};

	element.shell.u.extrudedShell.shellShape.nCoords	= 6;
	element.shell.u.extrudedShell.shellShape.nSubPolys	= 1;
	element.shell.u.extrudedShell.shellShape.nArcs		= 1;

	element.shell.u.extrudedShell.begC.x = centerPoint.x;
	element.shell.u.extrudedShell.begC.y = centerPoint.y;
	element.shell.u.extrudedShell.extrusionVector.x = 20.0;
	element.shell.u.extrudedShell.extrusionVector.y = 0.0;
	element.shell.u.extrudedShell.extrusionVector.z = 0.0;

	memo.shellShapes[0].coords = (API_Coord**) BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
	memo.shellShapes[0].pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (element.shell.u.extrudedShell.shellShape.nArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	memo.shellShapes[0].bodyFlags = (bool**) BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nCoords + 1) * sizeof (bool), ALLOCATE_CLEAR, 0);

	(*memo.shellShapes[0].coords)[1].x = -2.0;
	(*memo.shellShapes[0].coords)[1].y =  0.0;
	(*memo.shellShapes[0].coords)[2].x = -2.5;
	(*memo.shellShapes[0].coords)[2].y =  2.0;
	(*memo.shellShapes[0].coords)[3].x =  2.5;
	(*memo.shellShapes[0].coords)[3].y =  2.0;
	(*memo.shellShapes[0].coords)[4].x =  2.0;
	(*memo.shellShapes[0].coords)[4].y =  0.0;
	(*memo.shellShapes[0].coords)[5].x =  2.0;
	(*memo.shellShapes[0].coords)[5].y =  0.0;
	(*memo.shellShapes[0].coords)[6] = (*memo.shellShapes[0].coords)[1];
	(*memo.shellShapes[0].pends)[1] = element.shell.u.extrudedShell.shellShape.nCoords;
	(*memo.shellShapes[0].parcs)[0].begIndex = 2;
	(*memo.shellShapes[0].parcs)[0].endIndex = 3;
	(*memo.shellShapes[0].parcs)[0].arcAngle = -200 * DEGRAD;
	(*memo.shellShapes[0].bodyFlags)[1] = true;
	(*memo.shellShapes[0].bodyFlags)[2] = true;
	(*memo.shellShapes[0].bodyFlags)[3] = true;
	(*memo.shellShapes[0].bodyFlags)[4] = false;
	(*memo.shellShapes[0].bodyFlags)[5] = false;
	(*memo.shellShapes[0].bodyFlags)[6] = (*memo.shellShapes[0].bodyFlags)[1];

	element.shell.u.extrudedShell.begPlaneTilt = 120 * DEGRAD;
	element.shell.u.extrudedShell.endPlaneTilt =  60 * DEGRAD;

	// create shell element
	err = ACAPI_Element_Create (&element, &memo);
	if (err == APIERR_IRREGULARPOLY) {
		API_RegularizedPoly poly;
		API_RegularizedPoly resPoly = {};
		poly.coords = memo.shellShapes[0].coords;
		poly.pends = memo.shellShapes[0].pends;
		poly.parcs = memo.shellShapes[0].parcs;
		poly.vertexIDs = memo.shellShapes[0].vertexIDs;
		poly.needVertexAncestry = 1;
		poly.needEdgeAncestry = 1;
		err = ACAPI_Polygon_RegularizePolyline (&poly, &resPoly);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Create (extShell) regularize", err);
		} else {
			element.shell.u.extrudedShell.shellShape.nCoords = BMhGetSize (reinterpret_cast<GSHandle> (resPoly.coords)) / sizeof (API_Coord) - 1;
			element.shell.u.extrudedShell.shellShape.nSubPolys = BMhGetSize (reinterpret_cast<GSHandle> (resPoly.pends)) / sizeof (Int32) - 1;
			element.shell.u.extrudedShell.shellShape.nArcs = BMhGetSize (reinterpret_cast<GSHandle> (resPoly.parcs)) / sizeof (API_PolyArc);

			API_ElementMemo tmpMemo = {};
			tmpMemo.shellShapes[0].coords = resPoly.coords;
			tmpMemo.shellShapes[0].pends = resPoly.pends;
			tmpMemo.shellShapes[0].parcs = resPoly.parcs;
			tmpMemo.shellShapes[0].vertexIDs = resPoly.vertexIDs;

			tmpMemo.shellShapes[0].bodyFlags = (bool**) BMAllocateHandle ((element.shell.u.extrudedShell.shellShape.nCoords + 1) * sizeof (bool), ALLOCATE_CLEAR, 0);

			for (Int32 j = 1; j <= element.shell.u.extrudedShell.shellShape.nCoords; j++) {
				Int32 oldEdgeIndex = (*resPoly.edgeAncestry)[j];
				if (oldEdgeIndex == 0)				// new edge after regularization, apply default
					(*tmpMemo.shellShapes[0].bodyFlags)[j] = 0;
				else
					(*tmpMemo.shellShapes[0].bodyFlags)[j] = (*memo.shellShapes[0].bodyFlags)[oldEdgeIndex];
			}

			err = ACAPI_Element_Create (&element, &tmpMemo);
			if (err != NoError)
				ErrorBeep ("ACAPI_Element_Create (extruded shell)", err);
			ACAPI_Polygon_DisposeRegularizedPoly (&resPoly);
			BMKillHandle (reinterpret_cast<GSHandle*> (&tmpMemo.shellShapes[0].bodyFlags));
		}
	} else if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Create (extruded shell)", err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_CreateIrregularExtShell


// -----------------------------------------------------------------------------
// Set a paragraph
// -----------------------------------------------------------------------------
static GSErrCode SetParagraph (API_ParagraphType** paragraph, UInt32 parNum, Int32 from, Int32 range, API_JustID just, double firstIndent,
							   double indent, double rightIndent, double spacing, Int32 numOfTabs, Int32 numOfRuns,
							   Int32 numOfeolPos)
{
	if (paragraph == nullptr || parNum >= (BMhGetSize (reinterpret_cast<GSHandle> (paragraph)) / sizeof (API_ParagraphType)))
		return APIERR_BADPARS;

	if (numOfTabs < 1 || numOfRuns < 1 || numOfeolPos < 0)
		return APIERR_BADPARS;

	(*paragraph)[parNum].from  		 = from;
	(*paragraph)[parNum].range 		 = range;
	(*paragraph)[parNum].just  		 = just;
	(*paragraph)[parNum].firstIndent = firstIndent;
	(*paragraph)[parNum].indent 	 = indent;
	(*paragraph)[parNum].rightIndent = rightIndent;
	(*paragraph)[parNum].spacing 	 = spacing;

	(*paragraph)[parNum].tab = reinterpret_cast<API_TabType*> (BMAllocatePtr (numOfTabs * sizeof (API_TabType), ALLOCATE_CLEAR, 0));
	(*paragraph)[parNum].run = reinterpret_cast<API_RunType*> (BMAllocatePtr (numOfRuns * sizeof (API_RunType), ALLOCATE_CLEAR, 0));

	if (numOfeolPos > 0) {
		(*paragraph)[parNum].eolPos = reinterpret_cast<Int32*> (BMAllocatePtr (numOfeolPos * sizeof (Int32), ALLOCATE_CLEAR, 0));
	}

	return NoError;
}


// -----------------------------------------------------------------------------
// Set a tab of a paragraph
// -----------------------------------------------------------------------------
static GSErrCode SetTab (API_ParagraphType** paragraph, UInt32 parNum, UInt32 tabNum, API_TabID	type, double pos)
{
	if (paragraph == nullptr || parNum >= (BMhGetSize (reinterpret_cast<GSHandle> (paragraph)) / sizeof (API_ParagraphType)))
		return APIERR_BADPARS;

	if (tabNum >= BMGetPtrSize (reinterpret_cast<GSPtr> ((*paragraph)[parNum].tab)) / sizeof (API_TabType))
		return APIERR_BADPARS;

	(*paragraph)[parNum].tab[tabNum].type = type;
	(*paragraph)[parNum].tab[tabNum].pos  = pos;

	return NoError;
}


// -----------------------------------------------------------------------------
// Set a run of a paragraph
// -----------------------------------------------------------------------------
static GSErrCode SetRun (API_ParagraphType** paragraph, UInt32 parNum, UInt32 runNum, Int32 from, Int32 range, short pen, unsigned short faceBits,
						 short font, unsigned short effectBits, double size)
{
	if (paragraph == nullptr || parNum >= (BMhGetSize (reinterpret_cast<GSHandle> (paragraph)) / sizeof (API_ParagraphType)))
		return APIERR_BADPARS;

	if (runNum >= BMGetPtrSize (reinterpret_cast<GSPtr> ((*paragraph)[parNum].run)) / sizeof (API_RunType))
		return APIERR_BADPARS;

	(*paragraph)[parNum].run[runNum].from	    = from;
	(*paragraph)[parNum].run[runNum].range	    = range;
	(*paragraph)[parNum].run[runNum].pen	    = pen;
	(*paragraph)[parNum].run[runNum].faceBits   = faceBits;
	(*paragraph)[parNum].run[runNum].font	    = font;
	(*paragraph)[parNum].run[runNum].effectBits = effectBits;
	(*paragraph)[parNum].run[runNum].size	    = size;

	return NoError;
}


// -----------------------------------------------------------------------------
// Set EOL array of a paragraph
// -----------------------------------------------------------------------------
static GSErrCode SetEOL (API_ParagraphType** paragraph, UInt32 parNum, UInt32 eolNum, Int32 offset)
{
	if (paragraph == nullptr || parNum >= (BMhGetSize (reinterpret_cast<GSHandle> (paragraph)) / sizeof (API_ParagraphType)))
		return APIERR_BADPARS;

	if (eolNum >= BMGetPtrSize (reinterpret_cast<GSPtr> ((*paragraph)[parNum].eolPos)) / sizeof (Int32))
		return APIERR_BADPARS;

	if (offset < 0)
		return APIERR_BADPARS;

	(*paragraph)[parNum].eolPos[eolNum] = offset;

	return NoError;
}


// -----------------------------------------------------------------------------
// Create a multistyle text
// -----------------------------------------------------------------------------
GSErrCode CreateMultiTextElement (const API_Coord& pos, double scale, const API_Guid& renFiltGuid)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	GSErrCode			err;
	Int32				numOfParagraphs = 3;

	memo.textContent = new GS::UniString { u"This multistyle\nword w\u00E1s created\nby the Element Test example project."sv };

	element.header.type = API_TextID;

	memo.paragraphs = reinterpret_cast<API_ParagraphType**> (BMhAll (numOfParagraphs * sizeof (API_ParagraphType)));

	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (Text)", err);
		ACAPI_DisposeElemMemoHdls (&memo);
		return err;
	}

	element.header.renovationStatus = API_ExistingStatus;
	element.header.renovationFilterGuid = renFiltGuid;		// APINULLGuid is handled internally

	element.text.loc.x = pos.x;
	element.text.loc.y = pos.y;
	element.text.anchor = APIAnc_LB;
	element.text.multiStyle = true;
	element.text.nonBreaking = false;
	element.text.useEolPos = true;
	element.text.width = 150 * scale;

	err = SetParagraph (memo.paragraphs, 0, 0, 15, APIJust_Left, 2.0 * scale, 0, 0, -1.0, 1, 2, 1);
	if (err == NoError) {
		err = SetRun (memo.paragraphs, 0, 0, 0, 5, 3, APIFace_Plain, element.text.font, 0, 4.0 * scale);		// "This "
		if (err == NoError)
			err = SetRun (memo.paragraphs, 0, 1, 5, 10, 5, APIFace_Bold, element.text.font, 0, 4.5 * scale);	// "multistyle"
		if (err == NoError)
			err = SetTab (memo.paragraphs, 0, 0, APITab_Left, 2.0 * scale);
		if (err == NoError)
			err = SetEOL (memo.paragraphs, 0, 0, 14);
	}

	if (err == NoError) {
		err = SetParagraph (memo.paragraphs, 1, 16, 16, APIJust_Right, 2.0 * scale, 0, 0, -1.5, 1, 2, 1);
		if (err == NoError) {
			err = SetRun (memo.paragraphs, 1, 0, 0, 9, 7, APIFace_Plain, element.text.font, 0, 4.0 * scale);	// "word was "
			if (err == NoError)
				err = SetRun (memo.paragraphs, 1, 1, 9, 7, 9, APIFace_Italic, element.text.font, APIEffect_StrikeOut, 3.0 * scale);
			if (err == NoError)																					// "created"
				err = SetTab (memo.paragraphs, 1, 0, APITab_Left, 0.0);
			if (err == NoError)
				err = SetEOL (memo.paragraphs, 1, 0, 15);
		}
	}

	if (err == NoError) {
		err = SetParagraph (memo.paragraphs, 2, 33, 36, APIJust_Center, 0, 0, 0, -1.0, 1, 3, 2);
		if (err == NoError) {
			err = SetRun (memo.paragraphs, 2, 0, 0, 19, 11, APIFace_Underline, element.text.font, 0, 4.0 * scale);
			if (err == NoError)																					// "by the Element Test"
				err = SetRun (memo.paragraphs, 2, 1, 19, 9, 13, APIFace_Plain, element.text.font, APIEffect_StrikeOut, 6.0 * scale);
			if (err == NoError)																					// " example "
				err = SetRun (memo.paragraphs, 2, 2, 28, 8, 15, APIFace_Bold, element.text.font, APIEffect_SubScript, 5.0 * scale);
			if (err == NoError)																					// "project."
				err = SetTab (memo.paragraphs, 2, 0, APITab_Left, 0.0);
			if (err == NoError)
				err = SetEOL (memo.paragraphs, 2, 0, 27);
			if (err == NoError)
				err = SetEOL (memo.paragraphs, 2, 1, 35);
		}
	}

	if (err == NoError) {
		err = ACAPI_Element_Create (&element, &memo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create (text)", err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}		// CreateMultiTextElement


// -----------------------------------------------------------------------------
// Place a multistyle text element
// -----------------------------------------------------------------------------
void	Do_CreateWord (const API_Guid& renFiltGuid)
{
	API_Coord	c;

	if (!ClickAPoint ("Enter the left bottom position of the text", &c))
		return;

	CreateMultiTextElement (c, 1.0, renFiltGuid);

	return;
}		// Do_CreateWord


// -----------------------------------------------------------------------------
// Select elements by clicking on them
//   - select other elements of the same group also
//   - check locked layer
//   - check hidden layer
// Deselect all if no element was clicked
// -----------------------------------------------------------------------------
void		Do_SelectElems (void)
{
	API_Neig		theNeig;
	GSErrCode		err;

	if (!ClickAnElem ("Click an elem to select", API_ZombieElemID, &theNeig)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	err = ACAPI_Selection_Select ({ theNeig }, true);
	if (err != NoError)
		ErrorBeep ("ACAPI_Selection_Select", err);
}		// Do_SelectElems


// -----------------------------------------------------------------------------
// Delete elements by clicking on them
//   - delete the other elements in the same group also
//   - check locked layer
//   - check hidden layer
//   - associated elements should be deleted also
// -----------------------------------------------------------------------------
void		Do_DeleteElems (void)
{
	GS::Array<API_Guid> elemGuids = ClickElements_Guid ("Click elements to delete", API_ZombieElemID);
	if (elemGuids.IsEmpty ()) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Delete (elemGuids);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Delete", err);
}		// Do_DeleteElems


// -----------------------------------------------------------------------------
// Pick up element defaults from an instance
//   - check the appropriate setting dialog after execution
//   - check all element types
// -----------------------------------------------------------------------------
void		Do_PickupElem (void)
{
	API_ElemType		type;
	API_Guid			guid;
	API_Element			element = {}, mask;
	API_ElementMemo		memo = {};
	API_SubElement		marker = {};
	GSErrCode			err;

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	ACAPI_ELEMENT_MASK_SETFULL (mask);
	ACAPI_ELEMENT_MASK_SETFULL (marker.mask);

	element.header.guid		= guid;
	marker.subElem.header.type = API_ObjectID;

	err = ACAPI_Element_Get (&element);
	if (err == NoError && element.header.hasMemo)
		err = ACAPI_Element_GetMemo (element.header.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get/Memo", err);
		return;
	}

	switch (type.typeID) {
		case API_WindowID:
			marker.subElem.header.guid = element.window.openingBase.markGuid;
			break;
		case API_DoorID:
			marker.subElem.header.guid = element.door.openingBase.markGuid;
			break;
		case API_SkylightID:
			marker.subElem.header.guid = element.skylight.openingBase.markGuid;
			break;
		case API_CutPlaneID:
			marker.subElem.header.guid = element.cutPlane.segment.begMarkerId;
			break;
		case API_ElevationID:
			marker.subElem.header.guid = element.elevation.segment.midMarkerId;
			break;
		case API_InteriorElevationID:
			if (element.interiorElevation.nSegments > 0 && memo.intElevSegments != nullptr) {
				marker.subElem.header.guid = memo.intElevSegments[0].endMarkerId;
				element.interiorElevation.segment = memo.intElevSegments[0];
			}
			break;
		case API_DetailID:
			marker.subElem.header.guid = element.detail.markId;
			break;
		case API_ChangeMarkerID:
			marker.subElem.header.guid = element.changeMarker.markerGuid;
			break;
		case API_WorksheetID:
			marker.subElem.header.guid = element.worksheet.markId;
			break;
		default:
			err = ACAPI_Element_ChangeDefaults (&element, &memo, &mask);
			if (err != NoError)
				ErrorBeep ("ACAPI_Element_ChangeDefaults", err);
			ACAPI_DisposeElemMemoHdls (&memo);
			return;
	}

	if (marker.subElem.header.guid != APINULLGuid) {
		err = ACAPI_Element_Get (&marker.subElem);
		if (err == NoError)
			err = ACAPI_Element_GetMemo (marker.subElem.header.guid, &marker.memo);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Get/Memo", err);
			return;
		}

		marker.subType = APISubElement_MainMarker;
		err = ACAPI_Element_ChangeDefaultsExt (&element, &memo, &mask, 1UL, &marker);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeDefaults", err);
	} else {
		marker.subElem.object.libInd = 0;
		ACAPI_ELEMENT_MASK_SET (marker.mask, API_ObjectType, libInd);
		marker.subType = (API_SubElementType) (APISubElement_MainMarker | APISubElement_NoParams);
		err = ACAPI_Element_ChangeDefaultsExt (&element, &memo, &mask, 1UL, &marker);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeDefaults", err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&marker.memo);
}		// Do_PickupElem


// -----------------------------------------------------------------------------
// Change the clicked element settings to match the default
//   - type is given by the first clicked element
//   - check the appropriate setting dialog after execution
// -----------------------------------------------------------------------------
void		Do_ChangeElem (void)
{
	API_Element			element = {},  mask;
	API_ElementMemo		memo = {};
	API_SubElement		markers[2] = {};
	API_ElemType		type;
	API_Elem_Head		head;
	API_Guid			guid;
	GSErrCode			err;

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	ACAPI_ELEMENT_MASK_SETFULL (mask);

	element.header.type = type;
	element.header.guid = guid;
	err = ACAPI_Element_Get (&element);	//for variationID
	if (err != NoError)
		return;

	head = element.header;

	if (type == API_WindowID || type == API_DoorID || type == API_SkylightID ||
		type == API_CutPlaneID || type == API_ElevationID || type == API_InteriorElevationID ||
		type == API_DetailID || type == API_ChangeMarkerID || type == API_WorksheetID)
	{
			markers[0].subType = APISubElement_MainMarker;
			markers[1].subType = APISubElement_SHMarker;
			err = ACAPI_Element_GetDefaultsExt (&element, &memo, 2UL, markers);
			if (err == NoError) {
				element.header = head;
				err = ACAPI_Element_ChangeExt (&element, &element, &memo, APIMemoMask_AddPars,
											   2UL, markers, /* withDel */ true, ACAPI_ELEMENT_CHANGEEXT_ALLSEGMENTS);
			}
	} else {
		err = ACAPI_Element_GetDefaults (&element, &memo);
		if (err == NoError) {
			element.header = head;
			err = ACAPI_Element_Change (&element, &element, &memo, APIMemoMask_AddPars, true);
		}
	}

	ACAPI_DisposeElemMemoHdls (&memo);
	ACAPI_DisposeElemMemoHdls (&markers[0].memo);
	ACAPI_DisposeElemMemoHdls (&markers[1].memo);

	return;
}		// Do_ChangeElem


// -----------------------------------------------------------------------------
// Draw (explode) the clicked element from primitives
//   - shape primitives listed in the report window
//   - primitives are returned as ARCHICAD would draw the given element onto the screen
// -----------------------------------------------------------------------------
void		Do_ExplodeElem (void)
{
	API_ElemType	type;
	API_Guid		guid;
	GSErrCode		err;

	if (!ClickAnElem ("Click an elem to explode", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	API_Elem_Head elemHead = {};
	elemHead.type = type;
	elemHead.guid = guid;
	err = ACAPI_DrawingPrimitive_ShapePrims (elemHead, Draw_ShapePrims);
	if (err != NoError)
		ErrorBeep ("ACAPI_DrawingPrimitive_ShapePrims", err);

	return;
}		// Do_ExplodeElem


// -----------------------------------------------------------------------------
// Copy the clicked element to the story above
//   - the story must exist
//   - it must be in our workspace
//   - associated elements are not handled
// There are some elements which will be refused
//   - Doors/Windows: openings must be on the same floor as the parent wall
//   - Dimensions:    the same
//   - Auto Labels:   the same
// -----------------------------------------------------------------------------
void		Do_CopyElem (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};
	GSErrCode			err;

	API_ElemType	type;
	API_Guid		guid;
	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	element.header.type	= type;
	element.header.guid	= guid;

	err = ACAPI_Element_Get (&element);
	if (err == NoError)
		err = ACAPI_Element_GetMemo (element.header.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get/Memo", err);
		return;
	}

	if (err == NoError) {
		element.header.floorInd ++;
		err = ACAPI_Element_Create (&element, &memo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Create", err);
	}

	// ---- consistency check
	if (err == NoError)
		CompareElems (element, memo);
	// ----

	ACAPI_DisposeElemMemoHdls (&memo);
}		// Do_CopyElem


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void		Do_PickupProperties (void)
{
	API_ElemType		type;
	API_Guid			guid;
	API_Elem_Head		head = {};
	GSErrCode			err;

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	Int32				inviduallyLibInd = 0;
	bool				criteria = false;

	head.type = type;
	head.guid = guid;

	err = ACAPI_ElementLink_GetLinkedPropertyObjects (&head, &criteria, &inviduallyLibInd, nullptr, nullptr);
	if (err == NoError) {
		head = {};
		head.type = type;
		//index = 0 //defaults
		err = ACAPI_ElementLink_SetLinkedPropertyObjects (&head, criteria, inviduallyLibInd);
	}
	return;
}		// Do_PickupProperties


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void		Do_FillProperties (void)
{
	API_ElemType		type;
	API_Guid			guid;
	API_Elem_Head		head = {};
	GSErrCode			err;

	if (!ClickAnElem ("Click an element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	Int32				inviduallyLibInd = 0;
	bool				criteria = false;

	head.type = type;
	//head.guid = guid;	// defaults

	err = ACAPI_ElementLink_GetLinkedPropertyObjects (&head, &criteria, &inviduallyLibInd, nullptr, nullptr);

	if (err == NoError) {
		head = {};
		head.type	= type;
		head.guid	= guid;

		err = ACAPI_ElementLink_SetLinkedPropertyObjects (&head, criteria, inviduallyLibInd);
	}
	return;
}		// Do_FillProperties


// -----------------------------------------------------------------------------
// Dump element category value
// -----------------------------------------------------------------------------
void		Do_DumpElemCategoryValue (const API_ElemCategoryValue& elemCategoryValue)
{
	WriteReport ("   %s   : %s (%s)", GS::UniString (elemCategoryValue.category.name).ToCStr ().Get (), GS::UniString (elemCategoryValue.name).ToCStr ().Get (), APIGuidToString (elemCategoryValue.guid).ToCStr ().Get ());
}


// -----------------------------------------------------------------------------
// Dump element's categories
// -----------------------------------------------------------------------------
void		Do_DumpElemCategories (const API_Guid& elemGuid, const API_ElemType& type, bool dumpDefaults)
{
	GSErrCode			err = NoError;

	GS::Array<API_ElemCategory> categoryList;
	ACAPI_Category_GetElementCategories (&categoryList);

	categoryList.Enumerate ([&] (const API_ElemCategory& category) {
		if (category.categoryID != API_ElemCategory_BRI) {
			API_ElemCategoryValue	elemCategoryValue;

			if (dumpDefaults) {
				err = ACAPI_Category_GetCategoryValueDefault (type, category, &elemCategoryValue);
				if (err == NoError)
					Do_DumpElemCategoryValue (elemCategoryValue);
				else
					ErrorBeep ("ACAPI_Category_GetCategoryValueDefault ()", err);
			} else {
				err = ACAPI_Category_GetCategoryValue (elemGuid, category, &elemCategoryValue);
				if (err == NoError)
					Do_DumpElemCategoryValue (elemCategoryValue);
				else
					ErrorBeep ("ACAPI_Category_GetCategoryValue ()", err);
			}
		}
	});
}


// -----------------------------------------------------------------------------
// Dump basic information of the clicked element into the Report Window
//   - check in Teamwork mode also
// -----------------------------------------------------------------------------
void		Do_DumpElem (API_Guid& renFiltGuid)
{
	API_ElemType	type;
	API_Guid		guid;
	API_Element		element = {};
	API_ElementMemo	memo = {};
	API_ProjectInfo	projectInfo = {};
	API_SharingInfo	sharingInfo = {};
	GSErrCode		err;

	if (!ClickAnElem ("Click an elem to get info for", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	element.header.guid = guid;

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_ElemInfoString);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	WriteReport ("Dump: %s GUID:%s", ElemID_To_Name (type).ToCStr (CC_UTF8).Get (),
	             					 APIGuidToString (element.header.guid).ToCStr ().Get ());
	if (memo.elemInfoString != nullptr)
		WriteReport ("      info: %s", memo.elemInfoString->ToCStr ().Get ());

	ACAPI_DisposeElemMemoHdls (&memo);

	ACAPI_ProjectOperation_Project (&projectInfo);

	if (projectInfo.userId != element.header.userId) {
		WriteReport ("  out of my workspace");
		err = ACAPI_Teamwork_ProjectSharing (&sharingInfo);
		if (err != NoError) {
			ErrorBeep ("APIEnv_ProjectSharingID", err);
			return;
		}
	}

	GS::UniString	typeName, renovationStatusName, renovationFilterName;
	ACAPI_Element_GetElemTypeName (element.header.type, typeName);
	ACAPI_Renovation_GetRenovationStatusName (element.header.renovationStatus, &renovationStatusName);
	ACAPI_Renovation_GetRenovationFilterName (&element.header.renovationFilterGuid, &renovationFilterName);

	WriteReport ("  typeID     : (%03d) %s", element.header.type.typeID, static_cast<const char *> (typeName.ToCStr ()));
	WriteReport ("  guid       : %s", APIGuidToString (element.header.guid).ToCStr ().Get ());
	WriteReport ("  modiStamp  : %d", element.header.modiStamp);
	WriteReport ("  groupGuid  : %s", APIGuidToString (element.header.groupGuid).ToCStr ().Get ());
	WriteReport ("  floorInd   : %d", element.header.floorInd);
	WriteReport ("  layer      : %d", element.header.layer);
	WriteReport ("  hasMemo    : %d", element.header.hasMemo);
	WriteReport ("  drwIndex   : %d", element.header.drwIndex);
	WriteReport ("  userId     : %d", element.header.userId);
	WriteReport ("  lockId     : %d", element.header.lockId);
	WriteReport ("  renovation status name : %s", static_cast<const char *> (renovationStatusName.ToCStr ()));
	WriteReport ("  renovation filter name : %s", static_cast<const char *> (renovationFilterName.ToCStr ()));
	WriteReport ("  Categories :");
	Do_DumpElemCategories (element.header.guid, element.header.type, false);
	WriteReport ("  Default categories :");
	Do_DumpElemCategories (element.header.guid, element.header.type, true);

	if (projectInfo.teamwork) {
		DumpOwner ("owned by  :", &projectInfo, &sharingInfo, element.header.userId);
		DumpOwner ("locked by :", &projectInfo, &sharingInfo, element.header.lockId);
	}

	if (sharingInfo.users != nullptr)
		BMhKill (reinterpret_cast<GSHandle*>(&sharingInfo.users));

	renFiltGuid = element.header.renovationFilterGuid;

	ACAPI_KeepInMemory (true);

	return;
}		// Do_DumpElem


// -----------------------------------------------------------------------------
// Set clicked element's following categories:
//  - Structural Function
//  - Position
// -----------------------------------------------------------------------------
void		Do_SetElemCategories (bool changeDefaults)
{
	API_ElemType	type;
	API_Guid		guid;
	API_Element		element = {};
	GSErrCode		err;

	if (!ClickAnElem ("Click an elem to set categories", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	element.header.guid = guid;

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	WriteReport ("Do_SetElemCategories:");
	WriteReport ("Clicked element: %s GUID:%s", ElemID_To_Name (type).ToCStr (CC_UTF8).Get (),
	             								APIGuidToString (element.header.guid).ToCStr ().Get ());

	WriteReport ("  Old categories :");
	Do_DumpElemCategories (element.header.guid, element.header.type, false);
	WriteReport ("  Old default categories :");
	Do_DumpElemCategories (element.header.guid, element.header.type, true);

	GS::Array<API_ElemCategory> categoryList;
	ACAPI_Category_GetElementCategories (&categoryList);

	categoryList.Enumerate ([&] (const API_ElemCategory& category) {
		if (category.categoryID == API_ElemCategory_StructuralFunction || category.categoryID == API_ElemCategory_Position) {
			GS::Array<API_ElemCategoryValue> categoryValueList;
			ACAPI_Category_GetElementCategoryValues (&category, &categoryValueList);
			if (!categoryValueList.IsEmpty ()) {
				API_ElemCategoryValue elemCategoryValue;
				err = ACAPI_Category_GetCategoryValue (element.header.guid, category, &elemCategoryValue);
				if (err != NoError)
					ErrorBeep ("ACAPI_Category_GetCategoryValue ()", err);

				if (changeDefaults) {
					// Default will pick up categories from the instance
					err = ACAPI_Category_SetCategoryValueDefault (element.header.type, category, elemCategoryValue);
					if (err != NoError)
						ErrorBeep ("ACAPI_Element_SetCategoryDefaults ()", err);
				} else {
					// Set the categories of the instance to the next value (going around the possible values)
					UIndex ii = 0;
					for (; ii < categoryValueList.GetSize (); ++ii) {
						if (categoryValueList[ii].guid == elemCategoryValue.guid)
							break;
					}
					ii = (ii + 1) % categoryValueList.GetSize ();
					err = ACAPI_Category_SetCategoryValue (element.header.guid, category, categoryValueList[ii]);
					if (err != NoError)
						ErrorBeep ("ACAPI_Category_SetCategoryValue ()", err);
				}
			}
		}
	});

	WriteReport ("  New categories :");
	Do_DumpElemCategories (element.header.guid, element.header.type, false);
	WriteReport ("  New default categories :");
	Do_DumpElemCategories (element.header.guid, element.header.type, true);

	return;
}		// Do_SetElemCategories


void Do_DumpCollisions (GS::Array<GS::Pair<API_CollisionElem, API_CollisionElem>>& collisions)
{
	WriteReport ("\nNumber of collisions in selection : %d \n", collisions.GetSize ());
	Int32 ind = 1;

	for (auto pair : collisions.AsConst ()) {
		API_Element		elem1 = {};
		elem1.header.guid = pair.first.collidedElemGuid;

		API_Element		elem2 = {};
		elem2.header.guid = pair.second.collidedElemGuid;

		GSErrCode err = ACAPI_Element_Get (&elem1);
		if (err == NoError)
			err = ACAPI_Element_Get (&elem2);

		if (err == NoError) {
			WriteReport ("\n%d. Collision: ", ind);
			WriteReport ("1. GUID: %s, type: %s", APIGuidToString (elem1.header.guid).ToCStr ().Get (), ElemID_To_Name (elem1.header.type).ToCStr (CC_UTF8).Get ());
			WriteReport ("2. GUID: %s, type: %s", APIGuidToString (elem2.header.guid).ToCStr ().Get (), ElemID_To_Name (elem2.header.type).ToCStr (CC_UTF8).Get ());
			++ind;
		}
	}
}


// -----------------------------------------------------------------------------
// Collision Detection
// -----------------------------------------------------------------------------
void		Do_DetectCollisionsBasedOnSelection (void)
{
	API_SelectionInfo 	selectionInfo = {};
	GS::Array<API_Neig>	selNeigs;
	GSErrCode			err;

	// Get selection
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}

	if (err == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
		WriteReport_Alert ("No selected elements");
		return;
	}

	GS::Array<API_Guid> guidList1, guidList2;
	GS::Array<GS::Pair<API_CollisionElem, API_CollisionElem>> collisions;

	for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
		guidList1.Push (selNeigs[i].guid);
		guidList2.Push (selNeigs[i].guid);
	}
	API_CollisionDetectionSettings colDetSettings;
	colDetSettings.volumeTolerance = 0.0001;
	colDetSettings.performSurfaceCheck = true;
	colDetSettings.surfaceTolerance = 0.0001;
	err = ACAPI_Element_GetCollisions (guidList1, guidList2, collisions, colDetSettings);
	Do_DumpCollisions (collisions);

	return;
}		// Do_DetectCollisions


void	Do_CreateOpening (void)
{
	API_Guid			wallGuid;
	API_Element			element = {};
	API_Coord3D			clickPoint = {};
	GSErrCode			err = NoError;

	if (!ClickAnElem ("Click a wall to place an opening", API_WallID, nullptr, &element.header.type, &wallGuid, &clickPoint)) {
		WriteReport_Alert ("Please click a wall");
		return;
	}

	element.header.type = API_WallID;
	element.header.guid = wallGuid;

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	element = {};
	element.header.type = API_OpeningID;
	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (opening)", err);
		return;
	}

	element.opening.owner = wallGuid;

	element.opening.extrusionGeometryData.frame.basePoint.x = clickPoint.x;
	element.opening.extrusionGeometryData.frame.basePoint.y = clickPoint.y;
	element.opening.extrusionGeometryData.frame.basePoint.z = 0.0;

	element.opening.extrusionGeometryData.frame.axisX.x = -1.0;
	element.opening.extrusionGeometryData.frame.axisX.y = 0.0;
	element.opening.extrusionGeometryData.frame.axisX.z = 0.0;

	element.opening.extrusionGeometryData.frame.axisY.x = 0.0;
	element.opening.extrusionGeometryData.frame.axisY.y = 0.0;
	element.opening.extrusionGeometryData.frame.axisY.z = 1.0;

	// extrusionDir
	element.opening.extrusionGeometryData.frame.axisZ.x = 0.0;
	element.opening.extrusionGeometryData.frame.axisZ.y = 1.0;
	element.opening.extrusionGeometryData.frame.axisZ.z = 0.0;

	err = ACAPI_Element_Create (&element, nullptr);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (opening)", err);

	return;
}		// Do_CreateOpening

