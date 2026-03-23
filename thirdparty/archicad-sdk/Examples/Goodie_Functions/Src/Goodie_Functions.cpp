// *****************************************************************************
// Source code for the Goodie Functions Add-On
// *****************************************************************************

#define	_GOODIE_FUNCTIONS_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"				// also includes APIdefs.h
#include	"APICommon.h"

#include	"UserItemDialog.hpp"

#include	"DGModule.hpp"
#include	"DGFileDlg.hpp"
#include	"FileTypeManager.hpp"
#include	"FileSystem.hpp"
#include	"MemoryIBinaryChannel.hpp"
#include	"JSON/SAJParser.hpp"

#include	"HTTP/Client/ClientConnection.hpp"
#include	"HTTP/Client/Request.hpp"
#include	"HTTP/Client/Response.hpp"
#include	"HTTP/Client/Authentication/BasicAuthentication.hpp"
#include	"JSON/JDOMParser.hpp"
#include	"IBinaryChannelUtilities.hpp"
#include	"IOBinProtocolXs.hpp"
#include	"IChannelX.hpp"
#include	"Thread.hpp"
#include	"Threading.hpp"

#include	"GSXMLDOMBuilder.hpp"
#include	"GSXMLDOMReader.hpp"
#include	"xercesc/dom/DOM.hpp"

// ---------------------------------- Types ------------------------------------

// ---------------------------------- Variables --------------------------------
#define PROJ_INFO_AUTOTEXT_CUSTOM_TYPE_STR        "autotext-"


// ---------------------------------- Prototypes -------------------------------


// =============================================================================
//
// Main functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Select a BMP file with a file dialog
// -----------------------------------------------------------------------------

static bool	Select_a_BMPFile (IO::Location* fileLoc)
{
	bool			reply;
	DGTypePopupItem	popup;

	popup.text = L("BMP File");
	popup.extensions = L("bmp");
	popup.macType = 'BMP ';

	reply = DGGetOpenFile (fileLoc, 1, &popup, nullptr, "Select a BMP file");

	return reply;
}		// Select_a_BMPFile


// -----------------------------------------------------------------------------
// Read a BMP file
// -----------------------------------------------------------------------------

static GSErrCode	Read_a_BMPFile (const IO::Location* fileLoc, GSHandle* buffer)
{
	UInt32	bufSize = 0;
	GSErrCode		err = NoError, errMem = NoError;

	IO::File	bmpFile (*fileLoc);
	err = bmpFile.GetStatus ();
	if (err != NoError) {
		ErrorBeep ("IO::File contructor failed", err);
		return err;
	}

	err = bmpFile.Open (IO::File::ReadMode);
	if (err == NoError)
		err = bmpFile.GetDataLength (&bufSize);
	if (err == NoError) {
		*buffer = BMAllocateHandle (bufSize, ALLOCATE_CLEAR, 0);
		errMem = BMError ();
		if (errMem == NoError) {
			err = bmpFile.ReadBin (**buffer, bufSize, &bufSize);
		} else
			ErrorBeep ("BMAllocateHandle", err);
	}

	err = bmpFile.Close ();

	if (err == NoError)
		err = errMem;

	return err;
}		// Read_a_BMPFile


// -----------------------------------------------------------------------------
// Write a GIF file
// -----------------------------------------------------------------------------

static GSErrCode	Write_a_GIFFile (const IO::Location* fileLoc, GSHandle buffer)
{
	IO::Location	gifDef;
	IO::Name		gifName;
	UInt32	bufSize;
	GSErrCode		err;

	// Make the specification of the GIF file (overwrite the extension)
	// Copy the file specification of the BMP file
	gifDef = *fileLoc;

	// Overwrite its extension with ".Gif"
	gifDef.GetLastLocalName (&gifName);
	// no name length check here
	if (gifName.HasExtension ()) {
		// replace extension
		gifName.DeleteExtension ();
		gifName.AppendExtension ("gif");
	} else {
		// append ".gif"
		gifName.Append (".gif");
	}
	gifDef.SetLastLocalName (gifName);

	bool contains = false;
	err = IO::fileSystem.Contains (gifDef, &contains);
	if (err != NoError || contains) {
		WriteReport_Alert ("The file \"%s\" already exists", static_cast<const char*> (gifName.ToString ().ToCStr ()));
		return err;
	}

	// Check if there is an existing GIF file there with the same name
	// default action is OnNotFound::Fail
	IO::File	gifFile (gifDef, IO::File::Create);
	err = gifFile.GetStatus ();
	if (err != NoError) {
		DBBREAK_STR ("File status error!");
		return err;
	}

	// Write the buffer into the file
	err = gifFile.Open (IO::File::WriteEmptyMode);
	if (err == NoError) {
		bufSize = BMGetHandleSize (buffer);
		err = gifFile.WriteBin (*buffer, bufSize, &bufSize);

		gifFile.Close ();
	}

	return err;
}		// Write_a_GIFFile


// -----------------------------------------------------------------------------
// Convert a Windows bitmap (BMP) file into GIF format
//	 - Select a BMP file using a file open dialog
//	 - The GIF file will be created to the same folder with the same name
// If a matching GIF file exists, it won't be overwritten
// -----------------------------------------------------------------------------

static void		Do_BMPtoGIF (void)
{
	IO::Location		fileLoc;
	API_PictureHandle	bmpHandle, gifHandle;
	GSErrCode			err;

	// Get a file specification in fileDef (on macintosh convert it to FISpec_Flat)
	if (!Select_a_BMPFile (&fileLoc))
		return;

	BNZeroMemory (&bmpHandle, sizeof (bmpHandle));
	bmpHandle.storageFormat = APIPictForm_Bitmap;

	// Read the specified file
	err = Read_a_BMPFile (&fileLoc, &bmpHandle.pictHdl);

	// Convert the BMP into a GIF
	if (err == NoError) {
		BNZeroMemory (&gifHandle, sizeof (gifHandle));
		gifHandle.storageFormat = APIPictForm_GIF;
		err = ACAPI_Conversion_ConvertPicture (&bmpHandle, &gifHandle);
		if (err == NoError) {
			if (gifHandle.pictHdl != nullptr) {
				Write_a_GIFFile (&fileLoc, gifHandle.pictHdl);
			} else {
				DBBREAK_STR ("No error is reported by API, yet gifHandle.pictHdl is nullptr");
			}
		}
		BMKillHandle (&gifHandle.pictHdl);
	}

	BMKillHandle (&bmpHandle.pictHdl);

	return;
}		// Do_BMPtoGIF


// -----------------------------------------------------------------------------
// Select the topmost clicked slab on the plan
// Only polygonal elements can be searched by APIAny_SearchElementByCoordID.
// The following elements are polygonal:
//   - Walls
//   - Slabs
//   - Roofs
//   - Shells
//   - Meshes
//   - Hatches
//   - Zones (rooms)
//   - Details
//   - Details
//   - ChangeMarkers
//   - Worksheets
// -----------------------------------------------------------------------------

static void		Do_SelectTopmostSlab (void)
{
	API_Elem_Head		head;
	API_ElemSearchPars	pars;
	GSErrCode			err;

	BNZeroMemory (&pars, sizeof (API_ElemSearchPars));
	pars.type = API_SlabID;
	pars.z = 10.0;
	pars.filterBits = APIFilt_OnVisLayer | APIFilt_OnActFloor;

	if (!ClickAPoint ("Click somewhere", &pars.loc))
		return;

	BNZeroMemory (&head, sizeof (API_Elem_Head));
	head.type = API_SlabID;

	err = ACAPI_Element_SearchElementByCoord (&pars, &head.guid);
	if (err == NoError) {
		API_Neig neig;
		ElemHead_To_Neig (&neig, &head);
		ACAPI_Selection_Select ({ neig }, true);
	} else
		ErrorBeep ("APIAny_SearchElementByCoordID", err);

	return;
}		// Do_SelectTopmostSlab


// -----------------------------------------------------------------------------
// Triangulate the clicked slab's polygon
// 	 - the edges of the triangles will be added as lines
// -----------------------------------------------------------------------------

static void		Do_TriangulateSlab (void)
{
	API_ElemType		type;
	API_Guid			guid;
	API_Element			element, slab;
	API_ElementMemo		memo;
	API_Coord**			coords;
	Int32				i, n, j;
	GSErrCode			err = NoError;

	if (!ClickAnElem ("Click a slab to triangulate", API_SlabID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No slab was clicked");
		return;
	}

	BNZeroMemory (&slab, sizeof (API_Element));
	slab.header.type = API_SlabID;
	slab.header.guid = guid;
	err = ACAPI_Element_Get (&slab);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	memo = {};
	err = ACAPI_Element_GetMemo (slab.header.guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo_Masked", err);
		return;
	}

	ACAPI_CallUndoableCommand ("Triangulate",
		[&] () -> GSErrCode {
			coords = nullptr;

			GSErrCode err1 = ACAPI_Polygon_TriangulatePoly (&memo, &coords);
			if (err1 == NoError) {
				n = BMGetHandleSize ((GSHandle) coords) / (3 * sizeof (API_Coord));
				for (i = 0; i < n && err1 == NoError; i++) {
					element = {};
					element.header.type = API_LineID;
					element.header.layer  = APIApplicationLayerAttributeIndex;
					element.header.floorInd = slab.header.floorInd;
					element.line.ltypeInd = APISolidLineAttributeIndex;
					for (j = 0; j < 3 && err1 == NoError; j++) {
						element.line.begC.x = (*coords) [3 * i + j].x;
						element.line.begC.y = (*coords) [3 * i + j].y;
						element.line.endC.x = (*coords) [3 * i + (j + 1) % 3].x;
						element.line.endC.y = (*coords) [3 * i + (j + 1) % 3].y;
						err1 = ACAPI_Element_Create (&element, nullptr);
					}
				}
				BMKillHandle ((GSHandle *) &coords);
			} else
				ErrorBeep ("APIAny_TriangulatePolyID", err1);

			return err1;
		});

	return;
}		// Do_TriangulateSlab


// -----------------------------------------------------------------------------
// Find the quality of a spline approximation
//	 - Click a spline and enter a polyline
// 	 - the maximal distance between the spline and the poly-vertices will be calculated
// Spline FIT POINTS must be clicked
// Internal algorithm:
//	 - go through the spline segments
//	 - determine what polyline nodes should be measured relatively to the segment
//	 - APIAny_GetSplineMaxDistID returns the maximal distance of those nodes
// Suggestions:
// 	- make every nth vertex a fit point
//    if the spline has inflexion points, they should be the primary fit points
//	- make each spline-dir parallel to the vector
//    between the neighbors of the vertex made to be the fit point
//	- try different lenPrev, lenNext values to get the best result
// TIP: the segments can be tuned independently (unlike B-Splines)
// -----------------------------------------------------------------------------

static bool	FindVertsOfSegment (API_TestSplineFit *test, const API_GetPolyType *pline)
{
	API_Coord	begC, endC, c;
	Int32		i, i1, i2, j;

	begC = (*test->splCoords)[test->segment - 1];
	endC = (*test->splCoords)[test->segment];

	// Find poly vertices close enough to the endpoints of the spline segment to be examined
	i1 = i2 = -1;
	for (i = 1; i < pline->nCoords; i++) {
		c = (*pline->coords)[i];
		if (fabs (c.x - begC.x) < EPS && fabs (c.y - begC.y) < EPS)
			i1 = i;
		if (fabs (c.x - endC.x) < EPS && fabs (c.y - endC.y) < EPS)
			i2 = i;
	}
	DBPrintf ("FindVertsOfSegment: (%d, %d)\n", i1, i2);
	if (i1 < 1 || i2 < 1)
		return (false);			// At least one of them didn't match!!! (this segment cannot be processed)

	// Copy the poly vertices BETWEEN i1 and i2 to the test structure (only these vertices should be examined)
	if (i1 < i2) {
		for (i = i1 + 1, j = 0; i < i2; i++, j++)
			(*test->polyCoords)[j] = (*pline->coords)[i];
		test->nCoords = i2 - i1 - 1;
	} else {
		for (i = i2 + 1, j = 0; i < i1; i++, j++)
			(*test->polyCoords)[j] = (*pline->coords)[i];
		test->nCoords = i1 - i2 - 1;
	}

	return (test->nCoords > 0);
}		// FindVertsOfSegment


static void		Do_SplineApprox (void)
{
	API_ElementMemo		memo;
	API_GetPolyType		pline;
	API_TestSplineFit	test;
	double				dist, maxDist;
	API_Guid			guid;
	GSErrCode			err;

	if (!ClickAnElem ("Click a spline to trace", API_SplineID, nullptr, nullptr, &guid)) {
		WriteReport_Alert ("No spline was clicked");
		return;
	}

	err = ACAPI_Element_GetMemo (guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo (spline)", err);
		return;
	}

	BNZeroMemory (&pline, sizeof (API_GetPolyType));
	strcpy (pline.prompt, "Please enter the tracing polyline");
	pline.method = APIPolyGetMethod_Polyline;
	pline.startCoord.x = (*memo.coords)[0].x;
	pline.startCoord.y = (*memo.coords)[0].y;

	err = ACAPI_UserInput_GetPoly (&pline);
	if (err != NoError) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	// Let's see your approximation qualities...
	BNZeroMemory (&test, sizeof (API_TestSplineFit));

	test.splCoords = memo.coords;
	test.splDirs = memo.bezierDirs;
	test.nFit = BMGetHandleSize ((GSHandle) memo.coords) / sizeof (API_Coord);

	test.polyCoords = (API_Coord **) BMAllocateHandle ((pline.nCoords - 2) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);

	maxDist = -1.0;
	for (test.segment = 1; test.segment < test.nFit; test.segment++) {			// scan spline segments
		if (FindVertsOfSegment (&test, &pline)) {								// determine nodes in interests
			err = ACAPI_ProjectSetting_GetSplineMaxDist (&test, &dist);
			if (err == NoError) {
				WriteReport ("[%d] dist: %lf", test.segment, dist);
				if (dist > maxDist)												// handle the maximal distance
					maxDist = dist;
			} else
				ErrorBeep ("APIAny_GetSplineMaxDistID", err);
		} else
			WriteReport ("[%d] Spline segment skipped", test.segment);
	}

	WriteReport_Alert ("Max. distance: %lf", maxDist);

	BMKillHandle ((GSHandle *) &test.polyCoords);
	BMKillHandle ((GSHandle *) &pline.coords);
	BMKillHandle ((GSHandle *) &pline.parcs);
	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_SplineApprox


// -----------------------------------------------------------------------------
// Deletes the specified Project Info entry
// -----------------------------------------------------------------------------
static void	Do_DeleteAutotext (void)
{
	GS::Array<GS::ArrayFB<GS::UniString, 3> >	autoTexts;
	API_AutotextType	type = APIAutoText_Custom;

	GSErrCode err = ACAPI_AutoText_GetAutoTexts (&autoTexts, type);

	if (err == NoError && !autoTexts.IsEmpty ()) {
		// delete last custom entry
		const GS::UniString& dbKey = autoTexts[autoTexts.GetSize () - 1][1];
		err = ACAPI_AutoText_DeleteAnAutoText (((const char*) dbKey.ToCStr ()));
		if (err != NoError)
			DBPrintf ("APIAny_DeleteAnAutoTextID function failed.");
	}

	return;
}


// -----------------------------------------------------------------------------
//Creates a custom Project Info entry
// -----------------------------------------------------------------------------
static void	Do_CreateAutotext (void)
{
	const char* uiKey = {"Created Via API"};
	GS::Guid guid;
	guid.Generate ();
	API_Guid dbKey = GSGuid2APIGuid (guid);

	GSErrCode err = ACAPI_AutoText_CreateAnAutoText (&dbKey,uiKey);
	if (err != NoError)
		DBPrintf ("APIAny_CreateAnAutoTextID function failed.");

	if (err == NoError) {
		GS::UniString dbKey (PROJ_INFO_AUTOTEXT_CUSTOM_TYPE_STR);
		dbKey.Append (guid.ToUniString ());
		GS::UniString value ("custom text");
		err = ACAPI_AutoText_SetAnAutoText (&dbKey, &value);
		if (err != NoError)
			DBPrintf ("APIAny_SetAnAutoTextID function failed.");
	}

	return;
}	// Do_CreateAutotext

// -----------------------------------------------------------------------------
// Get a context dependent autotexts value from ARCHICAD
// -----------------------------------------------------------------------------
static void Do_GetContextDependentAutoTextKey() {
	API_Guid guid;

	if (!ClickAnElem("Click on a slab to acquire all of its available properties.", API_SlabID, nullptr, nullptr, &guid)) {
		WriteReport_Alert("No slab was clicked!");
		return;
	}
	else {
		WriteReport("Slab clicked!");
		GS::HashTable<GS::UniString, GS::UniString> properties { GS::HashTable<GS::UniString, GS::UniString> () };
		GSErrCode err;
		err = ACAPI_AutoText_GetPropertyAutoTextKeyTable (&guid, &properties);
		if (err == NoError) {
			properties.Enumerate(
				[&err, &guid](const GS::UniString& propertyName, GS::UniString& propertyKey) -> void {
					char s0[256];
					CHTruncate(propertyName.ToCStr(), s0, sizeof(s0));
					GS::UniString value;
					err = ACAPI_AutoText_InterpretAutoText (&propertyKey, &guid, &value);
					if (err == NoError) {
						char s1[256];
						CHTruncate(value.ToCStr(), s1, sizeof(s1));
						WriteReport("%s is: %s", s0, s1);
					}
					else {
						WriteReport("Autotext for %s could not be resolved.", s0);
					}
				});
		}
		else {
			WriteReport("Error while retrieving the property autotext key table.");
		}
	}
}


// -----------------------------------------------------------------------------
// Get the autotext values from ARCHICAD
// -----------------------------------------------------------------------------

static void	Do_GetAutotexts (const GS::UniString* thisKeyOnly = nullptr)
{
	GS::Array<GS::ArrayFB<GS::UniString, 3> >	autotexts;

	API_AutotextType	type = APIAutoText_All;

	GSErrCode	err = ACAPI_AutoText_GetAutoTexts (&autotexts, type);
	if (err == NoError) {
		WriteReport ("+--------------------------------+--------------------------------+--------------------------------+");
		WriteReport ("+          User Interface        +          Database Key          +              Value             +");
		WriteReport ("+--------------------------------+--------------------------------+--------------------------------+");

		for (ULong ii = 0; ii < autotexts.GetSize (); ii++) {
			if (thisKeyOnly == nullptr || *thisKeyOnly == autotexts[ii][1]) {
				char	s0[32], s1[32], s2[32];
				autotexts[ii][0].DeleteAll (GS::UniChar ('%'));
				autotexts[ii][1].DeleteAll (GS::UniChar ('%'));
				autotexts[ii][2].DeleteAll (GS::UniChar ('%'));
				CHTruncate (autotexts[ii][0].ToCStr (), s0, sizeof (s0));
				CHTruncate (autotexts[ii][1].ToCStr (), s1, sizeof (s1));
				CHTruncate (autotexts[ii][2].ToCStr (), s2, sizeof (s2));

				WriteReport ("+%32s+%32s+%32s+", s0, s1, s2);
			}
		}

		WriteReport ("+--------------------------------+--------------------------------+--------------------------------+");
	} else {
		WriteReport_Alert ("Error %s in APIAny_GetAutoTextsID", ErrID_To_Name (err));
	}
}


// -----------------------------------------------------------------------------
// Set the value of an autotext in ARCHICAD
// -----------------------------------------------------------------------------

static void	Do_SetAutotexts (void)
{
	GS::UniString	dbKey ("CAD_TECHNICIAN_FULLNAME"), value ("John Doe");

	// Round 0 -- dump autotext for this key
	Do_GetAutotexts (&dbKey);

	// Round 1 -- clear autotext value
	GSErrCode	err = ACAPI_AutoText_SetAnAutoText (&dbKey);
	if (err == NoError) {
		Do_GetAutotexts (&dbKey);
	} else {
		WriteReport_Alert ("Error %s in APIAny_SetAnAutoTextID", ErrID_To_Name (err));
	}

	// Round 2 -- set it to something meaningful
	err = ACAPI_AutoText_SetAnAutoText (&dbKey, &value);
	if (err == NoError) {
		Do_GetAutotexts (&dbKey);
	} else {
		WriteReport_Alert ("Error %s in APIAny_SetAnAutoTextID", ErrID_To_Name (err));
	}

	// Round 3 -- test for  error code
	err = ACAPI_AutoText_SetAnAutoText ();
	if (err == NoError) {
		Do_GetAutotexts (&dbKey);
	} else {
		if (err == APIERR_BADPARS)
			WriteReport ("APIAny_SetAnAutoTextID returned correct error code");
		else
			WriteReport_Alert ("Error %s in APIAny_SetAnAutoTextID", ErrID_To_Name (err));
	}
}


// -----------------------------------------------------------------------------
// Get the library part own unique ID from the element fast
// -----------------------------------------------------------------------------

static void	Do_GetLibPartUnID (void)
{
	API_ElemType		type;
	API_Guid			guid;
	API_Element			element;
	API_LibPart			libPart;
	API_ToolBoxItem		tbItem;
	char				lpfUnID[128] = {0};
	GSErrCode			error;

	if (!ClickAnElem ("Click any library part based element", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}
	if (type != API_ObjectID && type != API_LampID && type != API_WindowID && type != API_DoorID && type != API_SkylightID) {
		WriteReport_Alert ("Bad element type");
		return;
	}

	BNZeroMemory (&element, Sizeof32 (element));
	element.header.type = type;
	element.header.guid = guid;

	// old method
	if (ACAPI_Element_Get (&element) != NoError) {
		WriteReport_Alert ("Can't get element");
		return;
	}

	libPart = {};
	switch (type.typeID) {
		case API_ObjectID:
			libPart.typeID = APILib_ObjectID;
			libPart.index = element.object.libInd;
			break;

		case API_LampID:
			libPart.typeID = APILib_LampID;
			libPart.index = element.object.libInd;
			break;

		case API_WindowID:
			libPart.typeID = APILib_WindowID;
			libPart.index = element.window.openingBase.libInd;
			break;

		case API_DoorID:
			libPart.typeID = APILib_DoorID;
			libPart.index = element.window.openingBase.libInd;
			break;

		case API_SkylightID:
			libPart.typeID = APILib_SkylightID;
			libPart.index = element.skylight.openingBase.libInd;
			break;

		default:
			break;
	}

	error = ACAPI_LibraryPart_Get (&libPart);
	if (error != NoError) {
		if (error == APIERR_MISSINGDEF)
			WriteReport_Alert ("Error code: APIERR_MISSINGDEF \nDocumentName: %s \nTypeID: %d \nOwnerID: %d \nIndex: %d \nMissingDef: %d", (const char *)GS::UniString(libPart.docu_UName).ToCStr(), libPart.typeID, libPart.ownerID, libPart.index, libPart.missingDef);
		else
			WriteReport_Alert ("Can't get library part");
		return;
	}
	delete libPart.location;

	WriteReport ("Do_GetLibPartUnID():: library part's own ID (old way): %s", libPart.ownUnID);

	// new method
	BNZeroMemory (&element, Sizeof32 (element));
	element.header.type = type;
	element.header.guid = guid;

	if (ACAPI_Element_GetHeader (&element.header) != NoError) {
		WriteReport_Alert ("Can't get element header");
		return;
	}

	if (ACAPI_LibraryPart_GetElemLibPartUnId (&element.header, lpfUnID) == NoError) {
		WriteReport ("Do_GetLibPartUnID():: library part's own ID (new way): %s", lpfUnID);
	}

	if (ACAPI_LibraryPart_GetLibPartToolVariation (&libPart, &tbItem) == NoError) {
		char *p = (char *) &tbItem.type.variationID;
		if (tbItem.type != API_ZombieElemID) {
			if (p[0] != 0)
				WriteReport ("Do_GetLibPartUnID():: creator tool & variation: %s \"%c%c%c%c\"",
				             ElemID_To_Name (tbItem.type).ToCStr (CC_UTF8).Get (),
				             p[3], p[2], p[1], p[0]);
			else
				WriteReport ("Do_GetLibPartUnID():: creator tool: %s",
				             ElemID_To_Name (tbItem.type).ToCStr (CC_UTF8).Get ());
		}
	}
}


// -----------------------------------------------------------------------------
// ExampleSAJParseTracer
// -----------------------------------------------------------------------------

class ExampleSAJParseTracer : public JSON::SAJParseHandler {
private:
	enum Status {
		Init,
		Parsing,
		Finished,
		Error
	};

	enum ContainerType {
		ObjectContainer,
		ArrayContainer
	};

	struct ParseStackInfo {
		ParseStackInfo (ContainerType containerType):
			m_containerType (containerType),
			m_first (true) {}

		ContainerType	m_containerType;
		bool			m_first;
	};

	Status								m_status;
	GS::UniString						m_output;
	GS::PagedArray	<ParseStackInfo>	m_parserStack;

public:
	ExampleSAJParseTracer ():
		m_status (Init)
	{
	}

	virtual ~ExampleSAJParseTracer ();

	virtual void StartDocument () override
	{
		m_status = Parsing;
	}

	virtual void EndDocument () override
	{
		m_status = Finished;
	}

	virtual void ParserError (const char* errorMessage, UInt32 position) override
	{
		GS::UniString error = "ExampleSAJParseTracer exception.";
		if ((errorMessage != nullptr) && (::strlen (errorMessage) > 0)) {
			error += " Error description: " + GS::UniString (errorMessage) + ".";
		}
		if (position > 0) {
			error += GS::UniString::Printf (" Parse position: %d.", position);
		}
		throw GS::Exception (error, __FILE__, __LINE__);
	}

	virtual void NullValue () override
	{
		CheckValuePrefix ();

		m_output += "null";
	}

	virtual void BoolValue (bool value) override
	{
		CheckValuePrefix ();

		m_output += value ? "true" : "false";
	}

	virtual void Int64Value (GS::Int64 value) override
	{
		CheckValuePrefix ();

		m_output += GS::UniString::Printf ("%lld", value);
	}

	virtual void UInt64Value (GS::UInt64 value) override
	{
		CheckValuePrefix ();

		m_output += GS::UniString::Printf ("%llu", value);
	}

	virtual void DoubleValue (double value) override
	{
		CheckValuePrefix ();

		GS::UniString doubleStr = GS::UniString::Printf ("%lf", value);
		doubleStr.TrimRight ('0');
		m_output += doubleStr;
	}

	virtual void Name (const GS::UniString& value) override
	{
		if (m_parserStack.GetLast ().m_first) {
			m_parserStack.GetLast ().m_first = false;
		} else {
			m_output += ",";
		}
		m_output += "\"";
		// Parsed input "\\\n" -> output "\n"
		m_output += GS::UniString::Printf (value);
		m_output += "\":";
	}

	virtual void StringValue (const GS::UniString& value) override
	{
		CheckValuePrefix ();

		m_output += "\"";
		// Parsed input "\\\n" -> output "\n"
		m_output += GS::UniString::Printf (value);
		m_output += "\"";
	}

	virtual void StartObject () override
	{
		CheckValuePrefix ();
		m_output += "{";
		m_parserStack.Push (ParseStackInfo (ObjectContainer));
	}

	virtual void EndObject () override
	{
		m_output += "}";

		m_parserStack.DeleteLast ();
	}

	virtual void StartArray () override
	{
		m_output += "[";

		m_parserStack.Push (ParseStackInfo (ArrayContainer));
	}

	virtual void EndArray () override
	{
		m_output += "]";

		m_parserStack.DeleteLast ();
	}

	void Reset ()
	{
		m_status = Init;
		m_output.Clear ();
	}

	bool ParseFinished ()
	{
		return (m_status == Finished);
	}

	GS::UniString GetOutput ()
	{
		return m_output;
	}

private:

	void CheckValuePrefix () {
		if ((m_parserStack.GetSize () > 0) && (m_parserStack.GetLast ().m_containerType == ArrayContainer)) {
			if (m_parserStack.GetLast ().m_first) {
				m_parserStack.GetLast ().m_first = false;
			} else  {
				m_output += ",";
			}
		}

	}

};

ExampleSAJParseTracer::~ExampleSAJParseTracer ()
{
}


// -----------------------------------------------------------------------------
// Handles JSON import
// -----------------------------------------------------------------------------

static void Do_ParseJSON (void)
{
	// Let the user select a JSON file

	FTM::FileTypeManager	ftMan ("Goodies JSON");
	FTM::GroupID			gid = ftMan.AddGroup ("JSON files");
	ftMan.AddType (FTM::FileType ("JSON file", "json", '    ', '    ', 0), gid);

	DG::FileDialog  flDlg (DG::FileDialog::OpenFile);
	flDlg.AddFilter (gid);

	bool ret = flDlg.Invoke ();
	if (!ret)
		return;

	IO::Location retLoc = flDlg.GetSelectedFile ();


	// Load the JSON file into memory
	IO::File jsonFile (retLoc);
	if (jsonFile.GetStatus () != NoError)
		return;

	UInt64	fSize = 0;
	jsonFile.GetDataLength (&fSize);
	char *buff = new char[fSize];
	if (buff == nullptr)
		return;

	if (jsonFile.Open (IO::File::ReadMode) != NoError) {
		delete [] buff;
		return;
	}

	jsonFile.ReadBin (buff, (USize) fSize);
	jsonFile.Close ();

	// now parse the JSON file
	using namespace JSON;

	SAJParser saxParser;
	ExampleSAJParseTracer testSAXParseTracer;
	GS::MemoryIBinaryChannel input (buff, (USize) fSize);

	saxParser.Parse (input, testSAXParseTracer);
	if (testSAXParseTracer.ParseFinished ()) {
		DBPrintf ("The content of the JSON file was:\n%s\n", testSAXParseTracer.GetOutput().ToCStr (0, MaxUSize, CC_UTF8).Get ());
	}

	delete [] buff;

}

// -----------------------------------------------------------------------------
// Download Public Webpage
// -----------------------------------------------------------------------------
static void Do_DownloadWebpage (void)
{
	using namespace HTTP::Client;
	using namespace HTTP::MessageHeader;
	using namespace GS::IBinaryChannelUtilities;

	IO::URI::URI connectionUrl ("http://www.youtube.com");
	ClientConnection clientConnection(connectionUrl);
	clientConnection.Connect ();

	Request getRequest (Method::Get, "/");

	getRequest.GetRequestHeaderFieldCollection ().Add (HeaderFieldName::UserAgent,
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/54.0.2840.99 Safari/537.36");
	clientConnection.Send (getRequest);

	Response response;
	GS::IChannelX channel (clientConnection.BeginReceive (response), GS::GetNetworkByteOrderIProtocolX ());

	DBPrintf ("HTTP %u\n", response.GetStatusCode ());
	DBPrintf ("Received data: \n");

	GS::UniString body = ReadUniStringAsUTF8 (channel, StringSerializationType::NotTerminated);
	GS::Array<GS::UniString> lines;
	body.Split ("\n", GS::UniString::KeepEmptyParts, &lines);
	for (GS::UniString& line : lines) {
		DBPrintf ("%s\n", line.ToCStr (0, MaxUSize, GSCharCode::CC_Legacy).Get ());
	}

	DBPrintf ("Read %u characters, %u lines \n", body.GetLength (), lines.GetSize ());

	clientConnection.FinishReceive ();
	clientConnection.Close (false);
}

// -----------------------------------------------------------------------------
// Download Public Webpage Asynchronously
// -----------------------------------------------------------------------------

struct DownloadWebpageAsync : public GS::Runnable {
	DownloadWebpageAsync ();
	~DownloadWebpageAsync ();

	void  Run () override;
};

DownloadWebpageAsync::DownloadWebpageAsync () { ACAPI_KeepInMemory (true); }
DownloadWebpageAsync::~DownloadWebpageAsync () { ACAPI_KeepInMemory (false); }

void  DownloadWebpageAsync::Run () { Do_DownloadWebpage (); }

static void Do_DownloadWebpageAsync (void)
{
	GS::Thread thread (GS::RunnableTask (new DownloadWebpageAsync ()), "webpageasync");
	thread.Start ();
}

// -----------------------------------------------------------------------------
// Download Weather Data
// -----------------------------------------------------------------------------
static void Do_DownloadWeatherData (void)
{
	using namespace HTTP::Client;
	using namespace HTTP::MessageHeader;
	using namespace GS::IBinaryChannelUtilities;

	// Strusoft climate data server
	IO::URI::URI connectionUrl ("https://climate-server.bim.energy/");
	connectionUrl.AddQueryParameterValue ("lat", "47.5072");
	connectionUrl.AddQueryParameterValue ("lon", "19.0453");
	connectionUrl.AddQueryParameterValue ("year", "2010");
	connectionUrl.AddQueryParameterValue ("description", "Budapest");

	ClientConnection clientConnection (connectionUrl);
	clientConnection.Connect ();

	Request getRequest (Method::Get, connectionUrl);

	getRequest.GetRequestHeaderFieldCollection ().Add (HeaderFieldName::UserAgent,
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/54.0.2840.99 Safari/537.36");
	clientConnection.Send (getRequest);

	Response response;
	GS::IChannelX channel (clientConnection.BeginReceive (response), GS::GetNetworkByteOrderIProtocolX ());

	DBPrintf ("HTTP %u\n", response.GetStatusCode ());
	DBPrintf ("Received data: \n");

	GS::UniString body = ReadUniStringAsUTF8 (channel, StringSerializationType::NotTerminated);
	GS::Array<GS::UniString> lines;
	body.Split ("\n", GS::UniString::KeepEmptyParts, &lines);
	for (GS::UniString& line : lines) {
		DBPrintf ("%s\n", line.ToCStr (0, MaxUSize, GSCharCode::CC_Legacy).Get ());
	}

	DBPrintf ("Read %u characters, %u lines \n", body.GetLength (), lines.GetSize ());

	clientConnection.FinishReceive ();
	clientConnection.Close (false);
}

// -----------------------------------------------------------------------------
// Download NBS Token
// -----------------------------------------------------------------------------
static void Do_GetNbsToken (void)
{
	using namespace HTTP::Client;
	using namespace HTTP::MessageHeader;
	using namespace GS::IBinaryChannelUtilities;

	IO::URI::URI connectionUrl ("https://identity.thenbs.com/");
	ClientConnection clientConnection (connectionUrl);
	clientConnection.Connect ();

	Request postRequest (Method::Post, "/core/connect/token");

	GS::UniString clientID = "user";
	GS::UniString clientSecret = "pass";
	// More information on the API, and on getting an API key:
	// https://toolkit.thenbs.com/articles/for-software-developers/#bimToolkitAPI

	RequestHeaderFieldCollection& headers = postRequest.GetRequestHeaderFieldCollection ();
	Authentication::BasicAuthentication auth (clientID, clientSecret);
	auth.HandleRequestHeaderFieldCollection (headers);
	headers.Add (HeaderFieldName::ContentType, "application/x-www-form-urlencoded");

	const char postBody[] = "scope=bimtoolkitapi&grant_type=client_credentials";
	clientConnection.Send (postRequest, postBody, static_cast<USize> (strlen(postBody)));

	Response response;
	JSON::JDOMParser parser;
	JSON::ValueRef parsed = parser.Parse (clientConnection.BeginReceive (response));

	DBPrintf ("HTTP %u\n", response.GetStatusCode ());
	if (response.GetStatusCode () == StatusCode::OK) {
		JSON::ObjectValueRef object = GS::DynamicCast<JSON::ObjectValue> (parsed);
		JSON::StringValueRef tokenValue = GS::DynamicCast<JSON::StringValue> (object->Get ("access_token"));
		if (tokenValue != nullptr) {
			GS::UniString token = tokenValue->Get ();
			DBPrintf ("NBS Token: %s\n", token.ToCStr ().Get ());
		}
	}

	clientConnection.FinishReceive ();
	clientConnection.Close (false);
}


// -----------------------------------------------------------------------------
// XML Test Functions
// -----------------------------------------------------------------------------
void Do_XML_Write ()
{
	GSErrCode err = 0;
	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::Desktop, &fileLocation);
	IO::Name name ("Test_AddOn_XML_Example.xml");
	fileLocation.AppendToLocal (name);
	GSXML::DOMBuilder builder (fileLocation, "Root");
	//builder.SetAutoTabsHint (false);
	err |= builder.WriteStartDocument ();

	err |= builder.WriteXML ("SimpleTagInt", 42);
	err |= builder.WriteXML ("SimpleTagUniString", GS::UniString ("Hello World! Escaped character <>"));
	err |= builder.WriteStartElement ("ComplexTag");
	err |= builder.WriteXMLAttr ("attribute", "true");
	err |= builder.WriteStartElement ("ChildTag");
	err |= builder.WriteXMLTextNode ("content");
	err |= builder.WriteEndElement (); // ChildTag
	err |= builder.WriteXML ("SiblingTag", "content");
	err |= builder.WriteEndElement (); // ComplexTag

	err |= builder.WriteEndDocument ();

	if (err != NoError) {
		ACAPI_WriteReport ("Error happened", true);
	} else {
		ACAPI_WriteReport (builder.Export ().ToCStr ().Get (), true);
	}
}

void Do_XML_Read ()
{
	GSErrCode err = 0;
	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::Desktop, &fileLocation);
	IO::Name name ("Test_AddOn_XML_Example.xml");
	fileLocation.AppendToLocal (name);

	GSXML::DOMReader reader (fileLocation, "Root");
	GS::UniString	resultstr;
	GS::UniString	resultstr2;
	int				resultint;

	err |= reader.ReadXML ("SimpleTagInt", resultint);
	err |= reader.ReadXML ("SimpleTagUniString", resultstr);
	err |= reader.ReadStartElement ("ComplexTag");
	err |= reader.ReadXMLAttr ("attribute", resultstr2);

	xercesc::DOMElement* element = reader.GetActualNode ();
	GS::UniString tagname (element->getTagName ());

	if (err != NoError) {
		ACAPI_WriteReport ("Error happened", true);
	} else {
		ACAPI_WriteReport (resultstr.ToCStr ().Get (), true);
		ACAPI_WriteReport (resultstr2.ToCStr ().Get (), true);
		ACAPI_WriteReport (tagname.ToCStr ().Get (), true);
	}
}

void Do_XML_From_String ()
{
	GS::UniString xmldata ("<Root><SimpleTagInt>42</SimpleTagInt><SimpleTagUniString>Hello World! Escaped character &lt;&gt;</SimpleTagUniString><SimpleTagInt>44</SimpleTagInt><ComplexTag attribute=\"true\"><ChildTag>content</ChildTag><SiblingTag>content</SiblingTag></ComplexTag><SimpleTagInt>46</SimpleTagInt></Root>");
	GSXML::DOMReader xmlReader (reinterpret_cast<const unsigned char*> (xmldata.ToCStr ().Get ()),
								xmldata.GetLength (), "Root");

	xercesc::DOMElement* root = xmlReader.GetActualNode ();
	GS::UniString tagname ("SimpleTagInt");
	xercesc::DOMNodeList* children = root->getElementsByTagName (tagname.ToUStr ().Get ());
	GS::UniString tagnames;
	for (XMLSize_t i = 0; i < children->getLength (); ++i)
	{
		xercesc::DOMNode* node = children->item (i);
		tagnames += node->getNodeName ();
		tagnames += ": ";
		tagnames += node->getTextContent ();
		tagnames += '\n';
	}
	ACAPI_WriteReport (tagnames.ToCStr ().Get (), true);
}


// -----------------------------------------------------------------------------

static void Do_GetRoomImage (void)
{
	static GS::Array<API_Guid>	roomGuids;
	ACAPI_KeepInMemory (true);

	for (UIndex ii = 0; ii < 3; ++ii) {
		API_Guid guid;
		if (ClickAnElem ("Click a room to get image", API_ZoneID, nullptr, nullptr, &guid))
			roomGuids.Push (guid);
		else
			break;
	}

	if (roomGuids.GetSize () < 3)
	{
		WriteReport_Alert ("Not enough room was clicked");
        return;
	}

	roomGuids.Delete (0, roomGuids.GetSize () - 3);

	API_RoomImage image = {};
	image.viewType = APIImage_Model2D;
	image.offset = 0.3;
	image.scale = 0.01;
	image.backgroundColor = { 1.0, 1.0, 1.0 };

	UserItemDialog dialog (3, 400, 400, [&] (const DG::UserItem& userItem, const UIndex index) {
		NewDisplay::NativeImage nativeImage (userItem.GetClientWidth (), userItem.GetClientHeight (), 32, nullptr, false, 0, true, userItem.GetResolutionFactor ());

		image.roomGuid = roomGuids[index];
		image.nativeImagePtr = &nativeImage;
		ACAPI_Element_GetRoomImage (&image);

		return nativeImage;
	});
	dialog.Invoke ();
}



#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case 32500:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:		Do_BMPtoGIF ();							break;
				case 2:		Do_SelectTopmostSlab ();				break;
				case 3:		Do_TriangulateSlab ();					break;
				case 4:		Do_SplineApprox ();						break;
				case 5:		Do_GetContextDependentAutoTextKey();	break;
				case 6:		Do_GetAutotexts ();						break;
				case 7:		Do_SetAutotexts ();						break;
				case 8:		Do_CreateAutotext ();					break;
				case 9:		Do_DeleteAutotext ();					break;
				case 10:	Do_GetLibPartUnID ();					break;
				// ----
				case 12:	Do_ParseJSON ();						break;
				// ----
				case 14:	Do_DownloadWebpage ();					break;
				case 15:	Do_DownloadWebpageAsync ();				break;
				case 16:	Do_DownloadWeatherData ();				break;
				case 17:	Do_GetNbsToken ();						break;
				/* -- */
				case 19:	Do_XML_Write ();						break;
				case 20:	Do_XML_Read ();							break;
				case 21:	Do_XML_From_String ();					break;
				/* -- */
				case 23:	Do_GetRoomImage ();						break;
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

#ifdef __APPLE__
#pragma mark -
#endif

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
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (32500, 32520, MenuCode_UserDef, MenuFlag_Default);
	if (err != NoError)
		DBPrintf ("Goodie_Functions:: RegisterInterface() ACAPI_MenuItem_RegisterMenu failed\n");

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Goodie_Functions:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)

{
	return NoError;
}		// FreeData
