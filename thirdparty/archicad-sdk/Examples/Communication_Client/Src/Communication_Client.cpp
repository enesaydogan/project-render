// *****************************************************************************
// Source code for the Communication Client Add-On
// *****************************************************************************

#define	_COMMUNICATION_CLIENT_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Communication_Client.h"

#include	"DGModule.hpp"
#include	"IV.hpp"

// ---------------------------------- Macros ------------------------------------

//	Dialog items
#define TextPenItm			 4
#define TextColorItm		 5
#define TextHeightItm		 7
#define TextContentItm		 8

//	Utilities
#define TRACKPENCOLOR(pen,col)									\
			itemLong = DGGetItemValLong (dialogID, pen);		\
			DGSetItemValLong (dialogID, col, itemLong)

#define SETPENCOLOR(pen,col,val)								\
			DGSetItemValLong (dialogID, pen, val);				\
			DGSetItemValLong (dialogID, col, val)


// ---------------------------------- Types ------------------------------------

typedef struct {
	short		pen;
	short		filler_1;
	Int32		filler_2;
	double		size;
	char		content [256];
} TextParams;

typedef struct {
	short		penInd;
	short		sizeInd;
	short		contentInd;
	short		filler;
} TextParamsInd;

typedef struct {
	TextParams		*options;
	TextParamsInd	*parHdlInd;
} DialParams;


// ---------------------------------- Variables --------------------------------

static Int32		gCount = 0;
static CounterProc	ShowCounter;

// =============================================================================
//
//
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif


// -----------------------------------------------------------------------------
// Set the default parameters for the text
// -----------------------------------------------------------------------------

static void		DefaultParams (TextParams	*textPars)
{
	BNZeroMemory (textPars, sizeof (TextParams));

	textPars->pen	= 65;
	textPars->size	= 4.0;
	strcpy (textPars->content, "This is the default text");

	return;
}		// DefaultParams


// -----------------------------------------------------------------------------
// Text Settings dialog handler (callback)
// -----------------------------------------------------------------------------

static short DGCALLBACK TextSettings_Handler (short				message,
											  short				dialogID,
											  short				item,
											  DGUserData		userData,
											  DGMessageData		msgData)
{
UNUSED_PARAMETER (msgData);

	DialParams	*myData;
	Int32		itemLong;
	short		result;

	result = 0;
	myData = (DialParams *) userData;

	switch (message) {
		case DG_MSG_INIT: {
				API_UCCallbackType	ucb;
				BNZeroMemory (&ucb, sizeof (ucb));
				ucb.dialogID = dialogID;
				ucb.type	 = APIUserControlType_Pen;
				ucb.itemID	 = TextColorItm;
				ACAPI_Dialog_SetUserControlCallback (&ucb);

				DGSetItemValDouble (dialogID, TextHeightItm, myData->options->size);
				SETPENCOLOR (TextPenItm, TextColorItm, myData->options->pen);
				DGSetItemText (dialogID, TextContentItm, myData->options->content);
			}
			break;

		case DG_MSG_CLICK:
			switch (item) {
				case DG_OK:
				case DG_CANCEL:
					result = item;
					break;
			}
			break;

		case DG_MSG_CHANGE:
			switch (item) {
				case TextPenItm:
					TRACKPENCOLOR (TextPenItm, TextColorItm);
					break;
				}
			break;

		case DG_MSG_TRACK:
			switch (item) {
				case TextColorItm:
					TRACKPENCOLOR (TextColorItm, TextPenItm);
					break;
				}
			break;

		case DG_MSG_CLOSE:
			result = item;
				if (item == DG_OK) {
					if (DGModified (dialogID, TextPenItm))
						myData->options->pen = (short) DGGetItemValLong (dialogID, TextPenItm);
					else if (myData->parHdlInd != nullptr)
						myData->parHdlInd->penInd = 0;

					if (DGModified (dialogID, TextHeightItm))
						myData->options->size = DGGetItemValDouble (dialogID, TextHeightItm);
					else if (myData->parHdlInd != nullptr)
						myData->parHdlInd->sizeInd = 0;

					if (DGModified (dialogID, TextContentItm)) {
						GS::UniString temp = DGGetItemText (dialogID, TextContentItm);
						CHTruncate (temp.ToCStr (), myData->options->content, 64);
					} else if (myData->parHdlInd != nullptr) {
						myData->parHdlInd->contentInd = 0;
					}
				}
			break;
	}

	return (result);

}		// TextSettings_Handler


// -----------------------------------------------------------------------------
// Handle the Text Settings dialog
// -----------------------------------------------------------------------------

static bool		InputParams (TextParams 	*textPars,
							 TextParamsInd	*parHdlInd)
{
	DialParams	myData;
	bool		okHit;

	myData.options   = textPars;
	myData.parHdlInd = parHdlInd;

	okHit = (bool) (DGModalDialog (ACAPI_GetOwnResModule (), 32500, ACAPI_GetOwnResModule (), TextSettings_Handler, (DGUserData) &myData) == DG_OK);

	return okHit;
}		// InputParams


// -----------------------------------------------------------------------------
// Create the text with the given parameters
// -----------------------------------------------------------------------------

static GSErrCode	CreateAText (const TextParams *textPars)
{
	API_Element		element = {};
	API_ElementMemo	memo = {};
	API_Coord		c;
	GSErrCode		err;



	if (!ClickAPoint ("Click the top/left corner of the text box", &c))
		return APIERR_CANCEL;

	element.header.type = API_TextID;
	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (text)", err);
		return err;
	}

	element.text.pen    = textPars->pen;
	element.text.size   = textPars->size;
	element.text.anchor = APIAnc_LT;
	element.text.just   = APIJust_Left;

	element.text.loc = c;
	element.text.nonBreaking = true;
	element.text.nLine = 1;

	memo.textContent = new GS::UniString { textPars->content };

	err = ACAPI_CallUndoableCommand ("Create text",
		[&] () -> GSErrCode {
			return ACAPI_Element_Create (&element, &memo);
		});
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (text)", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}		// CreateAText



// =============================================================================
//
// Inter add-on communication
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Process the parameters passed onto the add-on
// -----------------------------------------------------------------------------

static void		ProcessParams (TextParams		*textPars,
							   TextParamsInd	*parHdlInd,
							   GSHandle			parHdl)
{
	API_MDCLParameter	par;
	Int32				n, i;
	bool				unknown, badValue, badType;
	GSErrCode			err;

	WriteReport ("Passed parameters");

	if (parHdl == nullptr) {
		WriteReport ("  [no params]");
		return;
	}

	err = ACAPI_AddOnAddOnCommunication_GetMDCLParameterNum (parHdl, &n);
	WriteReport ("  num: %d", n);

	for (i = 1; i <= n; i++) {
		par = {};
		par.index = i;
		err = ACAPI_AddOnAddOnCommunication_GetMDCLParameter (parHdl, &par);
		if (err == NoError) {

			switch (par.type) {
				case MDCLPar_int:
						WriteReport ("  [%d]  (%s) \"%s\" %d",     i, "  int", par.name, par.int_par);
						break;
				case MDCLPar_pointer:
						WriteReport ("  [%d]  (%s) \"%s\" %p",     i, "  ptr", par.name, par.ptr_par);
						break;
				case MDCLPar_float:
						WriteReport ("  [%d]  (%s) \"%s\" %lf",    i, "float", par.name, par.float_par);
						break;
				case MDCLPar_string:
						WriteReport ("  [%d]  (%s) \"%s\" \"%s\"", i, "  str", par.name, par.string_par);
						break;
			}

			unknown  = false;
			badValue = false;
			badType  = false;

			if (CHEqualCStrings (par.name, "pen")) {
				if (par.type == MDCLPar_int) {
					if (par.int_par > 1 && par.int_par < 256)
						textPars->pen = (short) par.int_par;
					else
						badValue = true;
					parHdlInd->penInd = (short) i;
				} else
					badType = true;

			} else if (CHEqualCStrings (par.name, "size")) {
				if (par.type == MDCLPar_float) {
					if (par.float_par > 0.0001)
						textPars->size = par.float_par;
					else
						badValue = true;
					parHdlInd->sizeInd = (short) i;
				} else
					badType = true;

			} else if (CHEqualCStrings (par.name, "content")) {
				if (par.type == MDCLPar_string) {
					CHTruncate (par.string_par, textPars->content, sizeof (textPars->content));
					parHdlInd->contentInd   = (short) i;
				} else
					badType = true;

			} else
				unknown = true;

			if (unknown)
				WriteReport ("       ## unknown parameter ##");
			if (badValue)
				WriteReport ("       ## bad value ##");
			if (badType)
				WriteReport ("       ## bad type ##");

		} else
			ErrorBeep ("APIAny_GetMDCLParameterID", err);
	}

	return;
}		// ProcessParams


// -----------------------------------------------------------------------------
// Modify the passed parameters
// -----------------------------------------------------------------------------

static void		ModifyParams (const TextParams		*textPars,
							  const TextParamsInd	*parHdlInd,
							  GSHandle				parHdl)
{
	API_MDCLParameter	par;
	GSErrCode			err = NoError;

	if (parHdlInd->penInd != 0) {
		par = {};
		par.index	= parHdlInd->penInd;
		par.type	= MDCLPar_int;
		par.int_par	= textPars->pen;
		err = ACAPI_AddOnAddOnCommunication_ChangeMDCLParameter (parHdl, &par);
	}

	if (err == NoError && parHdlInd->sizeInd != 0) {
		par = {};
		par.index		= parHdlInd->sizeInd;
		par.type		= MDCLPar_float;
		par.float_par	= textPars->size;
		err = ACAPI_AddOnAddOnCommunication_ChangeMDCLParameter (parHdl, &par);
	}

	if (err == NoError && parHdlInd->contentInd != 0) {
		par = {};
		par.index		= parHdlInd->contentInd;
		par.type		= MDCLPar_string;
		par.string_par	= textPars->content;
		err = ACAPI_AddOnAddOnCommunication_ChangeMDCLParameter (parHdl, &par);
	}

	if (err != NoError)
		ErrorBeep ("APIAny_ChangeMDCLParameterID", err);

	return;
}		// ModifyParams

// -----------------------------------------------------------------------------
// Callback function
// -----------------------------------------------------------------------------

static void		ShowCounter (void)
{
	gCount++;
	WriteReport_Alert ("Client was called %d times", gCount);

	return;
}		// ShowCounter


// =============================================================================
//
// Main functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif


// -----------------------------------------------------------------------------
// Place some text
//   - parameters can be entered in a simplified text settings dialog
//   - location is given by the user
// This function is also called from the "Communication Manager" example
// -----------------------------------------------------------------------------

static void		Do_CreateAText ()
{
	bool			okHit;
	TextParams		textPars;
	Int32			version = 0;
	GSSize			nBytes = 0;
	unsigned short	platformSign = GS::Act_Platform_Sign;

	BNZeroMemory (&textPars, sizeof (TextParams));

	ACAPI_Preferences_Platform (&version, &nBytes, nullptr, nullptr);
	if (version == 1 && nBytes == sizeof (TextParams)) {
		ACAPI_Preferences_Platform (&version, &nBytes, &textPars, &platformSign);
		if (platformSign != GS::Act_Platform_Sign) {
			GS::PlatformSign	inplatform = (GS::PlatformSign) platformSign;
			IVShort (inplatform, &textPars.pen);
			IVShort (inplatform, &textPars.filler_1);
			IVLong (inplatform, &textPars.filler_2);
			IVDouble (inplatform, &textPars.size);
			// char		content [256];
		}
	} else
		DefaultParams (&textPars);


	okHit = InputParams (&textPars, nullptr);
	if (okHit) {
		ACAPI_SetPreferences (1, sizeof (TextParams), &textPars);
		CreateAText (&textPars);
	}

	return;
}		// Do_CreateAText


// -----------------------------------------------------------------------------
// Entry points to handle ARCHICAD events
//
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
UNUSED_PARAMETER (params);

	Do_CreateAText ();

	return NoError;
}	// MenuCommandHandler

// -----------------------------------------------------------------------------
// Place some text (called from another add-on)
// -----------------------------------------------------------------------------
GSErrCode CreateTextCommandHandler (GSHandle parHdl, GSPtr resultData, bool silentMode)
{
UNUSED_PARAMETER (resultData);

	TextParams		params;
	TextParamsInd	parHdlInd;
	bool			ok;
	GSErrCode		err = NoError;

	// setup command parameters
	DefaultParams (&params);
	ProcessParams (&params, &parHdlInd, parHdl);
	if (!silentMode) {
		ok = InputParams (&params, &parHdlInd);
		if (!ok)
			return NoError;

		// return parameters modified in the dialog
		ModifyParams (&params, &parHdlInd, parHdl);
	}

	// do the job
	err = CreateAText (&params);

	return err;
}	// CreateTextCommandHandler


// -----------------------------------------------------------------------------
// Start the binary link
// -----------------------------------------------------------------------------
GSErrCode BeginLinkCommandHandler (GSHandle params, GSPtr resultData, bool silentMode)
{
UNUSED_PARAMETER (params);
UNUSED_PARAMETER (silentMode);

	CallBackTbl_Client	*myResultData;

	myResultData = (CallBackTbl_Client *) resultData;
	BNZeroMemory (myResultData, sizeof (CallBackTbl_Client));

	myResultData->counterProc = ShowCounter;

	ACAPI_KeepInMemory (true);
	return NoError;
}	// BeginLinkCommandHandler


// -----------------------------------------------------------------------------
// Close the binary link
// -----------------------------------------------------------------------------
GSErrCode EndLinkCommandHandler (GSHandle params, GSPtr resultData, bool silentMode)
{
UNUSED_PARAMETER (params);
UNUSED_PARAMETER (resultData);
UNUSED_PARAMETER (silentMode);

	ACAPI_KeepInMemory (false);

	return NoError;
}	// EndLinkCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================



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
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_UserDef, MenuFlag_Default);

	// Register command services
	if (err == NoError)
		err = ACAPI_AddOnAddOnCommunication_RegisterSupportedService (CmdID_PutText, 1L);
	if (err == NoError)
		err = ACAPI_AddOnAddOnCommunication_RegisterSupportedService (CmdID_BegBinaryLink, 1L);
	if (err == NoError)
		err = ACAPI_AddOnAddOnCommunication_RegisterSupportedService (CmdID_EndBinaryLink, 1L);

	return err;
}		/* RegisterInterface */


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Communication_Client:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	err = ACAPI_AddOnIntegration_InstallModulCommandHandler (CmdID_PutText, 1L, CreateTextCommandHandler);
	if (err != NoError)
		DBPrintf ("Communication_Client:: Initialize() ACAPI_AddOnIntegration_InstallModulCommandHandler failed\n");

	err = ACAPI_AddOnIntegration_InstallModulCommandHandler (CmdID_BegBinaryLink, 1L, BeginLinkCommandHandler);
	if (err != NoError)
		DBPrintf ("Communication_Client:: Initialize() ACAPI_AddOnIntegration_InstallModulCommandHandler failed\n");

	err = ACAPI_AddOnIntegration_InstallModulCommandHandler (CmdID_EndBinaryLink, 1L, EndLinkCommandHandler);
	if (err != NoError)
		DBPrintf ("Communication_Client:: Initialize() ACAPI_AddOnIntegration_InstallModulCommandHandler failed\n");

	return err;
}		/* Initialize */


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}	// FreeData
