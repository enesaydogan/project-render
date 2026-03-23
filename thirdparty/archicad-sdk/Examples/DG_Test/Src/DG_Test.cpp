// *****************************************************************************
// Source code for the DG Test Add-On
// *****************************************************************************

#define	_DG_TEST_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"BuiltInLibrary.hpp"
#include	"GSUnID.hpp"
#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"DGNativeContexts.hpp"
#include	"Graphics2D.h"
#include	"GXImage.hpp"

#include	"MemoryIChannel32.hpp"
#include	"MemoryOChannel32.hpp"
#include	"IV.hpp"
#include	"SetPlatformProtocol.hpp"
#include	"TreeEnumerator.hpp"

#include	"DGTestResIDs.hpp"
#include	"BuildingMaterialDialog.hpp"
#include	"AttributeListDialog.hpp"
#include	"LayerDialog.hpp"
#include	"BasicElementIconsRollDownDialog.hpp"
#include	"FontPopupDialog.hpp"
#include	"SplitButtonDialog.hpp"
#include	"OwnerDrawnListBoxPalette.hpp"

// ---------------------------------- Types ------------------------------------

#define	CURR_ADDON_VERS			0x0100		// The current version of the Add-On

#define Fill_GDLGID				32500		// The main Fill Settings dialog
#define Fill_FloorPlan_GDLGID	32501		// The Fill Settings/Floor Plan Attributes dialog

#define Browser_GDLGID			32510		// The Browser dialog
#define UserItem_GDLGID			32520		// The User Item example dialog
#define TreeView_GDLGID			32530		// The Tree view example dialog
#define Material_GDLGID			32540		// The Custom Material dialog
#define Progress_GDLGID			32550		// The Progress window
#define UCTest_GDLGID			32560		// The "Choose library parts" dialog
#define Profile_GDLGID			32570		// The Profile Controls dialog


// Fill_PenData control constants
#define ContourPen				0
#define FillPen					1
#define BackgroundPen			2

#define InvalPen				-1

// Fill_PenData macros
#define TRACKPENCOLOR(pen, col)									\
			itemLong = DGGetItemValLong (dialogID, pen);		\
			DGSetItemValLong (dialogID, col, itemLong)

#define SETPENCOLOR(pen, col, val)								\
			DGSetItemValLong (dialogID, pen, val);				\
			DGSetItemValLong (dialogID, col, val)


typedef struct {
	Int32				version;
	API_AttributeIndex	layer;
	API_AttributeIndex	hatchLineType;
	API_AttributeIndex	hatchFill;
	short		hatchPen;
	short		hatchbkgPen;
	short		hatchContPen;
	bool		hatchGlobal;
	bool		hatchWithArea;
	short		filler;
	GS::Owner<API_AttributePicker> attributePicker;
} MyPrefs;


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------

namespace DG
{
	// Non public exported functions
	PLATFORM_DLL_IMPORT bool	IsInstantDialogResourceLoadingEnabled ();
	PLATFORM_DLL_IMPORT void	SetInstantDialogResourceLoadingStatus (bool isEnabled);
}

// =============================================================================
//
// Utility functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Return the color of pen in question
// -----------------------------------------------------------------------------

static bool		SetFillColors (short penIndex,
							   bool isBackground,
							   UChar* colorR,
							   UChar* colorG,
							   UChar* colorB)
{
	GSErrCode		err;

	if (colorR == nullptr || colorG == nullptr || colorB == nullptr)
		return false;

	if (penIndex == InvalPen) {
		return false;
	} else if (penIndex == 0) {
		// For background, pen == 0 is valid (transparent)
		*colorR = (UChar) (isBackground ? 255 : 0);
		*colorG = *colorR;
		*colorB = *colorR;
	} else {
		API_Pen pen = {};
		pen.index  = penIndex;
		err = ACAPI_Attribute_GetPen (pen);

		if (err == NoError) {
			*colorR = (UChar) (((unsigned short) (pen.rgb.f_red * 65535))   >> 8);
			*colorG = (UChar) (((unsigned short) (pen.rgb.f_green * 65535)) >> 8);
			*colorB = (UChar) (((unsigned short) (pen.rgb.f_blue * 65535))  >> 8);
		} else {
			return false;
		}
	}
	return true;
}


// -----------------------------------------------------------------------------
// Sets the color of the fill popup
// -----------------------------------------------------------------------------

static void Fill_PenData (short dialogID,
						  short item,
						  short pen,
						  short which)
{
	UChar			colorR, colorG, colorB;

	switch (which) {
		case FillPen:
			if (SetFillColors (pen, false, &colorR, &colorG, &colorB))
				UC257SetColor (dialogID, item, FC257_FORECOLOR, colorR, colorG, colorB);
			break;

		case BackgroundPen:
			if (SetFillColors (pen, true, &colorR, &colorG, &colorB))
				UC257SetColor (dialogID, item, FC257_BACKCOLOR, colorR, colorG, colorB);
			break;

		case ContourPen:
			if (SetFillColors (pen, false, &colorR, &colorG, &colorB))
				UC257SetColor (dialogID, item, FC257_FRAMECOLOR, colorR, colorG, colorB);
			break;
	}

	return;
}		// Fill_PenData


// -----------------------------------------------------------------------------
// Callback function of the Fill Settings dialog
// Floor Plan Attributes tab page
// -----------------------------------------------------------------------------

static short DGCALLBACK FillFloorPlan_Handler (short			message,
											   short			dialogID,
											   short			item,
											   DGUserData		userData,
											   DGMessageData	/* msgData */)
{
	API_UCCallbackType	ucb;
	MyPrefs				*prefsData;
	Int32				itemLong;
	short				result;

// Dialog item IDs
#define HSLineTypeItm		  2
#define HSBorderIcn			  3
#define HSBorderPenItm		  4
#define HSBorderColorItm	  5
#define HSNotFramedItm		  6
#define HSFramedItm			  7
#define HSFillTypeItm		  8
#define HSFillPenItm		 10
#define HSFillColorItm		 11
#define HSFillBGPenItm		 13
#define HSFillBGColorItm	 14
#define HSShowAreaItm		 15
#define HSProjectOrigItm	 16
#define HSLocalOrigItm		 17

	prefsData = (MyPrefs *) userData;
	result    = 0;

	switch (message) {
	case DG_MSG_INIT:
				// initialize user controls
				BNZeroMemory (&ucb, sizeof (ucb));
				ucb.dialogID = dialogID;
				ucb.type	 = APIUserControlType_Pen;
				ucb.itemID	 = HSBorderColorItm;
				ACAPI_Dialog_SetUserControlCallback (&ucb);
				ucb.itemID	 = HSFillColorItm;
				ACAPI_Dialog_SetUserControlCallback (&ucb);
				ucb.type	 = APIUserControlType_PenWithNull;
				ucb.itemID	 = HSFillBGColorItm;
				ACAPI_Dialog_SetUserControlCallback (&ucb);
				ucb.type	 = APIUserControlType_DashedLine;
				ucb.itemID	 = HSLineTypeItm;
				ACAPI_Dialog_SetUserControlCallback (&ucb);
				ucb.type	 = APIUserControlType_PolyFill;
				ucb.itemID	 = HSFillTypeItm;
				ACAPI_Dialog_SetUserControlCallback (&ucb);

				DGSetItemValLong (dialogID, HSLineTypeItm, prefsData->hatchLineType.ToInt32_Deprecated ());
				DGSetItemValLong (dialogID, HSFillTypeItm, prefsData->hatchFill.ToInt32_Deprecated ());
				SETPENCOLOR (HSFillPenItm, HSFillColorItm, prefsData->hatchPen);
				SETPENCOLOR (HSFillBGPenItm, HSFillBGColorItm, prefsData->hatchbkgPen);

				Fill_PenData (dialogID, HSFillTypeItm, prefsData->hatchContPen, ContourPen);
				Fill_PenData (dialogID, HSFillTypeItm, prefsData->hatchPen, FillPen);
				Fill_PenData (dialogID, HSFillTypeItm, prefsData->hatchbkgPen, BackgroundPen);

				if (prefsData->hatchGlobal)
					DGSetItemValLong (dialogID, HSProjectOrigItm, 1);
				else
					DGSetItemValLong (dialogID, HSLocalOrigItm, 1);

				if (prefsData->hatchContPen != 0) {
					SETPENCOLOR (HSBorderPenItm, HSBorderColorItm, prefsData->hatchContPen);
					DGSetItemValLong (dialogID, HSFramedItm, 1);
				} else {
					SETPENCOLOR (HSBorderPenItm, HSBorderColorItm, prefsData->hatchPen);
					DGSetItemValLong (dialogID, HSNotFramedItm, 1);
					DGDisableItem (dialogID, HSBorderIcn);
					DGDisableItem (dialogID, HSBorderPenItm);
					DGDisableItem (dialogID, HSBorderColorItm);
				}
				DGSetItemValLong (dialogID, HSShowAreaItm, prefsData->hatchWithArea);
				break;

	case DG_MSG_CHANGE:
			switch (item) {
				case HSBorderPenItm:
					TRACKPENCOLOR (HSBorderPenItm, HSBorderColorItm);
					// break; - this break is intentionally omitted!
				case HSBorderColorItm:
					Fill_PenData (dialogID, HSFillTypeItm, (short) DGGetItemValLong (dialogID, HSBorderPenItm), ContourPen);
					break;

				case HSFillPenItm:
					TRACKPENCOLOR (HSFillPenItm, HSFillColorItm);
					// break; - this break is intentionally omitted!
				case HSFillColorItm:
					Fill_PenData (dialogID, HSFillTypeItm, (short) DGGetItemValLong (dialogID, HSFillPenItm), FillPen);
					break;

				case HSFillBGPenItm:
					TRACKPENCOLOR (HSFillBGPenItm, HSFillBGColorItm);
					// break; - this break is intentionally omitted!
				case HSFillBGColorItm:
					Fill_PenData (dialogID, HSFillTypeItm, (short) DGGetItemValLong (dialogID, HSFillBGPenItm), BackgroundPen);
					break;

				case HSFramedItm:
				case HSNotFramedItm:
					if (DGGetItemValLong (dialogID, HSFramedItm) == 1) {
						DGEnableItem (dialogID, HSBorderIcn);
						DGEnableItem (dialogID, HSBorderPenItm);
						DGEnableItem (dialogID, HSBorderColorItm);
					} else {
						DGDisableItem (dialogID, HSBorderIcn);
						DGDisableItem (dialogID, HSBorderPenItm);
						DGDisableItem (dialogID, HSBorderColorItm);
					}
					break;
				}
				break;

	case DG_MSG_TRACK:
			switch (item) {
				case HSBorderColorItm:
					TRACKPENCOLOR (HSBorderColorItm, HSBorderPenItm);
					break;

				case HSFillColorItm:
					TRACKPENCOLOR (HSFillColorItm, HSFillPenItm);
					break;

				case HSFillBGColorItm:
					TRACKPENCOLOR (HSFillBGColorItm, HSFillBGPenItm);
					break;
				}
				break;

	case DG_MSG_CLOSE:
			result = item;
			if (item == DG_OK) {
				if (DGModified (dialogID, HSLineTypeItm)) {
					prefsData->hatchLineType = ACAPI_CreateAttributeIndex (DGGetItemValLong (dialogID, HSLineTypeItm));
				}
				if (DGModified (dialogID, HSProjectOrigItm)) {
					prefsData->hatchGlobal = (DGGetItemValLong (dialogID, HSProjectOrigItm) != 0);
				}
				if (DGModified (dialogID, HSFillPenItm) || DGModified (dialogID, HSFillColorItm)) {
					prefsData->hatchPen = (short) DGGetItemValLong (dialogID, HSFillColorItm);
				}
				if (DGModified (dialogID, HSFillBGPenItm) || DGModified (dialogID, HSFillBGColorItm)) {
					prefsData->hatchbkgPen = (short) DGGetItemValLong (dialogID, HSFillBGColorItm);
				}
				if (DGModified (dialogID, HSBorderPenItm) || DGModified (dialogID, HSBorderColorItm) ||
					DGModified (dialogID, HSFramedItm)) {
					itemLong = DGGetItemValLong (dialogID, HSBorderColorItm);
					if (DGGetItemValLong (dialogID, HSFramedItm) == 1)
						prefsData->hatchContPen = (short) itemLong;
					else
						prefsData->hatchContPen = 0;
				}
				if (DGModified (dialogID, HSFillTypeItm)) {
					prefsData->hatchFill = ACAPI_CreateAttributeIndex (DGGetItemValLong (dialogID, HSFillTypeItm));
				}
				if (DGModified (dialogID, HSShowAreaItm)) {
					prefsData->hatchWithArea = (DGGetItemValLong (dialogID, HSShowAreaItm) != 0);
				}
			}
			break;
	}

	return (result);

#undef HSLineTypeItm
#undef HSBorderIcn
#undef HSBorderPenItm
#undef HSBorderColorItm
#undef HSNotFramedItm
#undef HSFramedItm
#undef HSFillTypeItm
#undef HSFillPenItm
#undef HSFillColorItm
#undef HSFillBGPenItm
#undef HSFillBGColorItm
#undef HSShowAreaItm
#undef HSProjectOrigItm
#undef HSLocalOrigItm
}		// FillFloorPlan_Handler


// -----------------------------------------------------------------------------
// Fill Settings dialog handler (callback)
// -----------------------------------------------------------------------------

static short DGCALLBACK Fill_Handler (short				message,
									  short				dialogID,
									  short				item,
									  DGUserData		userData,
									  DGMessageData		msgData)
{
	MyPrefs				*prefsData;
	short				result;

// Dialog item IDs
#define HSFloorPlanItm		4
#define AttrTabItm			5
#define LayerItm			7

	result    = 0;
	prefsData = (MyPrefs *) userData;

	switch (dialogID) {
		case Fill_FloorPlan_GDLGID:
				result = FillFloorPlan_Handler (message, Fill_FloorPlan_GDLGID, item, userData, msgData);
				break;

		default:	// Common part of the dialog
			switch (message) {
				case DG_MSG_INIT:
					{
						// initialize user controls
						API_AttributePickerParams app;
						app.type = APIUserControlType_Layer;
						app.dialogID = dialogID;
						app.itemID = LayerItm;
						app.pushCheckAppearance = API_AttributePickerParams::PushCheckAppearance::ArrowIconAndText;
						ACAPI_Dialog_CreateAttributePicker (app, prefsData->attributePicker);
						prefsData->attributePicker->SetSelectedAttributeIndex (prefsData->layer);
					}
					DGSetItemValLong (dialogID, HSFloorPlanItm, 1);
					DGSetItemValLong (dialogID, AttrTabItm, 1);
					FillFloorPlan_Handler (message, Fill_FloorPlan_GDLGID, item, userData, msgData);
					break;
				case DG_MSG_CHANGE:
					if (item == LayerItm) {
						if (prefsData->attributePicker->Invoke ())
							prefsData->layer = prefsData->attributePicker->GetSelectedAttributeIndex ();
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

				case DG_MSG_CLOSE:
					result = item;
					if (item == DG_OK) {
						FillFloorPlan_Handler (message, Fill_FloorPlan_GDLGID, item, userData, msgData);
					}
					prefsData->attributePicker.Delete ();
					break;
				}
			break;
	}

	return (result);

#undef HSFloorPlanItm
#undef AttrTabItm
#undef LayerItm
}		// Fill_Handler


// -----------------------------------------------------------------------------
// Open a Fill Settings like dialog
//	 - dialog item values are saved/restored into/from the preferences
// -----------------------------------------------------------------------------


static void IVAttributeIndex (GS::PlatformSign platform, API_AttributeIndex& pIndex) {
	Int32 attrIndex;
	IVLong (platform, &attrIndex);
	pIndex = ACAPI_CreateAttributeIndex (attrIndex);
}


static GSErrCode	Do_FillSettings (void)
{
	MyPrefs			prefsData;
	API_Element		elem;
	GSErrCode		err = NoError;
	bool			isOK;
	Int32			version;
	GSSize			nBytes;
	unsigned short	platformSign = GS::Act_Platform_Sign;

	ACAPI_Preferences_Platform (&version, &nBytes, nullptr, nullptr);
	if (version == CURR_ADDON_VERS) {
		ACAPI_Preferences_Platform (&version, &nBytes, (GSPtr) &prefsData, &platformSign);
		if (platformSign != GS::Act_Platform_Sign) {
			GS::PlatformSign	inplatform = (GS::PlatformSign) platformSign;
			IVLong (inplatform, &prefsData.version);
			IVAttributeIndex (inplatform, prefsData.layer);
			IVAttributeIndex (inplatform, prefsData.hatchLineType);
			IVAttributeIndex (inplatform, prefsData.hatchFill);
			IVShort (inplatform, &prefsData.hatchPen);
			IVShort (inplatform, &prefsData.hatchbkgPen);
			IVShort (inplatform, &prefsData.hatchContPen);
			IVBool  (inplatform, &prefsData.hatchGlobal);
			IVBool  (inplatform, &prefsData.hatchWithArea);
			IVShort (inplatform, &prefsData.filler);
		}
	} else {
		// no preferences were saved before or incorrect preferences version
		//	--> fill with defaults
		prefsData = {};
		// Fill with defaults from the original Fill Settings dialog
		elem = {};
		elem.header.type = API_HatchID;
		err = ACAPI_Element_GetDefaults (&elem, nullptr);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_GetDefaults", err);
			return err;
		}
		prefsData.version			= CURR_ADDON_VERS;
		prefsData.layer				= elem.hatch.head.layer;
		prefsData.hatchLineType		= elem.hatch.ltypeInd;

		API_AttributeIndex fill = ACAPI_CreateAttributeIndex (1);
		short fillPen = 1, fillBgPen = 0;
		if (elem.hatch.hatchType == API_FillHatch) {
			fill		= elem.hatch.fillInd;
			fillPen		= elem.hatch.fillPen.penIndex;
			fillBgPen	= elem.hatch.fillBGPen;
		} else if (elem.hatch.hatchType == API_BuildingMaterialHatch) {
			API_Attribute attribute;
			BNZeroMemory(&attribute, sizeof(API_Attribute));
			attribute.header.typeID = API_BuildingMaterialID;
			attribute.header.index = elem.hatch.buildingMaterial;
			if (ACAPI_Attribute_Get(&attribute) == NoError) {
				fill		= attribute.buildingMaterial.cutFill;
				fillPen		= attribute.buildingMaterial.cutFillPen;
				fillBgPen	= attribute.buildingMaterial.cutFillBackgroundPen;
			}
		}
		prefsData.hatchFill			= fill;
		prefsData.hatchPen			= fillPen;
		prefsData.hatchbkgPen		= fillBgPen;

		prefsData.hatchContPen		= elem.hatch.contPen.penIndex;
		prefsData.hatchGlobal		= (elem.hatch.hatchOrientation.type == API_HatchGlobal);
		prefsData.hatchWithArea		= elem.hatch.showArea;
	}

	// Get the dialog from our resource file
	isOK = (DGModalDialog (ACAPI_GetOwnResModule (), Fill_GDLGID, ACAPI_GetOwnResModule (), Fill_Handler, (DGUserData) &prefsData) == DG_OK);

	if (isOK) {
		// save last settings into the preferences
		ACAPI_SetPreferences (CURR_ADDON_VERS, sizeof (MyPrefs), (GSPtr) &prefsData);
	}

	return err;
}		// Do_FillSettings


// =============================================================================
//
// Main functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// SetListView -- simple filter function
// -----------------------------------------------------------------------------

static void SetListView (short dialId, short lvItem, GSHandle ***gsh)
{
	Int32			nLibParts, nsh;
	Int32			one = 1;

	DGListViewSetHelpStyle (dialId, lvItem, DG_LVHS_FORITEM);

	// Speed up drawing
	DGListViewDisableDraw (dialId, lvItem);
	DGListViewDeleteItem (dialId, lvItem, DG_ALL_ITEMS);
	// Set the default sizes here
	DGListViewSetItemSize (dialId, lvItem, 60, 80);
	DGListViewSetImageSize (dialId, lvItem, 40, 40);

	ACAPI_LibraryPart_GetNum (&nLibParts);
	*gsh = (GSHandle **) BMhAllClear (nLibParts * sizeof (GSHandle));
	if (*gsh == nullptr || BMError () != NoError)
		return;

	GS::UniString	phaseText ("Filling list view...");
	API_ProcessControlTypeID	pcType = API_MenuCommandEnabled;
	ACAPI_ProcessWindow_InitProcessWindow  (&phaseText, &nLibParts, &pcType);

	API_LibPart lp = {};
	// for all library parts
	for (lp.index = 1, nsh = 0; lp.index <= nLibParts; lp.index++) {
		ACAPI_ProcessWindow_SetNextProcessPhase (&phaseText, &one);
		if (ACAPI_LibraryPart_Get (&lp) == NoError) {
			if (lp.location != nullptr)
				delete lp.location;
			if (lp.typeID == APILib_ObjectID && !lp.isTemplate && lp.isPlaceable) {
				// it's an object

				GSErrCode			err = NoError;

				// Get the preview picture
				API_LibPartSection	lpSect = {};
				lpSect.sectType = API_SectInfoGIF;
				API_MimePicture		mimePic = {};
				err = ACAPI_LibraryPart_GetSection (lp.index, &lpSect, &mimePic.inputHdl, nullptr);
				if (err == NoError) {
					mimePic.inContainsMime = true;
					mimePic.outDepth	   = APIColorDepth_FromSourceImage;
					mimePic.mimeOut		   = *mimePic.inputHdl;		// use the original mime type

					err = ACAPI_Conversion_ConvertMimePicture (&mimePic);
				}
				if (err == NoError && mimePic.outputHdl != nullptr) {
					char	docuname[256];
					CHTruncate (GS::UniString (lp.docu_UName).ToCStr (), docuname, sizeof (docuname));
					DGListViewInsertItem (dialId, lvItem, DG_LIST_BOTTOM);
					DGListViewSetItemText (dialId, lvItem, DG_LIST_BOTTOM, docuname);
					// Store the index of the library part as custom data for every item
					DGListViewSetItemUserData (dialId, lvItem, DG_LIST_BOTTOM, lp.index);

					const void* dgPicture = GX::Image (mimePic.outputHdl, FTM::UnknownType).ToDGPicture ();
					DGListViewSetItemImage (dialId, lvItem, DG_LIST_BOTTOM, DG_LVIT_PICTURE, DG::Icon (dgPicture));

					(**gsh)[nsh++] = (GS::GSHandle)dgPicture;
				}

				BMhKill (&mimePic.outputHdl);
				BMhKill (&mimePic.inputHdl);
			}
		}
		if (ACAPI_ProcessWindow_IsProcessCanceled () == APIERR_CANCEL)
			break;
	}

	ACAPI_ProcessWindow_CloseProcessWindow ();

	DGListViewEnableDraw (dialId, lvItem);
	DGRedrawItem (dialId, lvItem);

	return;
}		// SetListView


//------------------------------------------------------
// GetFillColors
//------------------------------------------------------
bool		GetFillColors (API_Attribute attribute,
						   unsigned char* colorR,
						   unsigned char* colorG,
						   unsigned char* colorB)
{
	*colorR = (unsigned char) (attribute.material.surfaceRGB.f_red * 256);
	*colorG = (unsigned char) (attribute.material.surfaceRGB.f_green * 256);
	*colorB = (unsigned char) (attribute.material.surfaceRGB.f_blue * 256);
	return true;
}

// -----------------------------------------------------------------------------
// SetMaterialListView -- simple filter function
// -----------------------------------------------------------------------------

static void SetMaterialListView (short dialId, short lvItem, GSHandle ***gsh)
{
	GS::Array<API_Attribute>	attributes;
	Int32						one = 1;

	DGListViewSetHelpStyle (dialId, lvItem, DG_LVHS_FORITEM);

	// Speed up drawing
	DGListViewDisableDraw (dialId, lvItem);
	DGListViewDeleteItem (dialId, lvItem, DG_ALL_ITEMS);
	// Set the default sizes here
	DGListViewSetItemSize (dialId, lvItem, 60, 80);
	DGListViewSetImageSize (dialId, lvItem, 40, 40);

	ACAPI_Attribute_GetAttributesByType (API_MaterialID, attributes);
	*gsh = (GSHandle **) BMhAllClear (attributes.GetSize () * sizeof (GSHandle));
	if (*gsh == nullptr || BMError () != NoError)
		return;

	GS::UniString	phaseText ("Filling material list view...");
	ACAPI_ProcessWindow_InitProcessWindow (&phaseText, &one);
	Int32 nMaterials = attributes.GetSize ();
	ACAPI_ProcessWindow_SetNextProcessPhase (&phaseText, &nMaterials);

	// for all materials
	for (const API_Attribute& attr : attributes) {
		ACAPI_ProcessWindow_SetProcessValue (&one);

		DGListViewInsertItem (dialId, lvItem, DG_LIST_BOTTOM);
		DGListViewSetItemText (dialId, lvItem, DG_LIST_BOTTOM, attr.header.name);
		// Store the index of the material as custom data for every item
		DGListViewSetItemUserData (dialId, lvItem, DG_LIST_BOTTOM, attr.header.index.ToInt32_Deprecated ());

		if (attr.material.texture.fileLoc == nullptr) {
			GSPixMapHandle pmap = GXCreateGSPixMap (128, 128);
			if (pmap) {
				GX_ARGBColor aColor = {0, 0, 0, 0};
				GetFillColors (attr, &aColor.red, &aColor.green, &aColor.blue);
				GXFillGSPixMap (pmap, aColor);

				const void* dgPicture = GX::Image (pmap).ToDGPicture ();
				DGListViewSetItemImage (dialId, lvItem, DG_LIST_BOTTOM, DG_LVIT_PICTURE, DG::Icon (dgPicture));
				GXDeleteGSPixMap (pmap);
				(**gsh)[1] = (GS::GSHandle) dgPicture;
			}
		} else {
			const void* dgPicture = GX::Image (*attr.material.texture.fileLoc).ToDGPicture ();
			DGListViewSetItemImage (dialId, lvItem, DG_LIST_BOTTOM, DG_LVIT_PICTURE, DG::Icon (dgPicture));
			(**gsh)[1] = (GS::GSHandle) dgPicture;

			delete attr.material.texture.fileLoc;
		}
		if (ACAPI_ProcessWindow_IsProcessCanceled () == APIERR_CANCEL)
			break;
	}

	ACAPI_ProcessWindow_CloseProcessWindow ();

	DGListViewEnableDraw (dialId, lvItem);
	DGRedrawItem (dialId, lvItem);

	return;
}		// SetMaterialListView


// -----------------------------------------------------------------------------
// The dialog callback
// -----------------------------------------------------------------------------

static short DGCALLBACK Browser_CB (short			message,
								    short			dialId,
				 			 	    short			item,
				  				    DGUserData		userData,
								    DGMessageData	msgData)
{
	short result = 0;

	static	GSHandle	**gsh = nullptr;

#define	LV_SELECT	2
#define	LV_ITEM1	3

	switch (message) {
		case DG_MSG_INIT:
			DGSetItemValLong (dialId, LV_SELECT, 1);
			if ((Int32 *) userData != nullptr && *((Int32 *) userData) != 0)
				SetMaterialListView (dialId, LV_ITEM1, &gsh);
			else
				SetListView (dialId, LV_ITEM1, &gsh);
			break;

		case DG_MSG_CLICK:
			if (item == DG_OK || item == DG_CANCEL)
				result = item;
			break;

		case DG_MSG_CHANGE:
			// Change the style of the list view
			 if (item == LV_SELECT) {
				DGListViewDisableDraw (dialId, LV_ITEM1);
				switch (DGGetItemValLong (dialId, LV_SELECT)) {
					case 1:
						DGListViewSetViewMode (dialId, LV_ITEM1, DG_LVVM_BOTTOMTEXT);
						DGListViewSetItemSize (dialId, LV_ITEM1, 60, 80);
						DGListViewSetImageSize (dialId, LV_ITEM1, 40, 40);
						break;

					case 2:
						DGListViewSetViewMode (dialId, LV_ITEM1, DG_LVVM_BOTTOMTEXT);
						DGListViewSetItemSize (dialId, LV_ITEM1, 36, 36);
						DGListViewSetImageSize (dialId, LV_ITEM1, 16, 16);
						break;

					default:
						DGListViewSetViewMode (dialId, LV_ITEM1, DG_LVVM_SINGLECOLUMN);
						DGListViewSetItemSize (dialId, LV_ITEM1, 0, 18);
						DGListViewSetImageSize (dialId, LV_ITEM1, 16, 16);
						break;
				}
				DGListViewEnableDraw (dialId, LV_ITEM1);
				DGRedrawItem (dialId, LV_ITEM1);
			}
			break;

		case DG_MSG_GROW: {
				short	vgrow = DGGetVGrow (msgData);
				short	hgrow = DGGetHGrow (msgData);
				DGBeginMoveGrowItems (dialId);

				DGMoveItem (dialId, DG_OK, hgrow, vgrow);
				DGGrowItem (dialId, LV_ITEM1, hgrow, vgrow);

				DGEndMoveGrowItems (dialId);
			}
			break;

		case DG_MSG_ITEMHELP: {
				DGItemHelpMsgData	*itemHelp = (DGItemHelpMsgData	*) msgData;
				if (msgData != 0 && item == LV_ITEM1) {
					DBPrintf ("Help requested for list item %d\n", itemHelp->listItem);
					if (itemHelp->subMessage == DG_HSM_TOOLTIP && itemHelp->listItem > 0) {
						itemHelp->toolTipText = DGListViewGetItemText (dialId, LV_ITEM1, itemHelp->listItem);
						result = 1;
					}
				}
			}
			break;

		case DG_MSG_CLOSE:
			if (item == DG_OK) {
				Int32 sel = DGListViewGetSelected (dialId, LV_ITEM1, DG_LIST_TOP);
				if (sel != 0)
					*((Int32 *) userData) = TruncateTo32Bit (DGListViewGetItemUserData (dialId, LV_ITEM1, (short) sel));
			}
			{
				Int32 nsh = BMhGetSize ((GSHandle) gsh) / Sizeof32 (GSHandle);
				for (Int32 ii = 0; ii < nsh && (*gsh)[ii] != nullptr; ii++) {
#ifdef macintosh
					DG::ReleaseNativeImage ((*gsh)[ii]);
#else
					DeleteObject (reinterpret_cast<HGDIOBJ> ((*gsh)[ii]));
#endif
				}
				BMhKill ((GSHandle *) &gsh);
			}
			break;
	}

	return (result);

#undef LV_SELECT
#undef LV_ITEM1
}		// Browser_CB


// -----------------------------------------------------------------------------
// Shows how to use the list view control
// -----------------------------------------------------------------------------

static void		Do_LibraryBrowser (void)
{
	Int32		chosenInd = 0;
	bool		isOK;

	isOK = (DGModalDialog (ACAPI_GetOwnResModule (), Browser_GDLGID, ACAPI_GetOwnResModule (), Browser_CB, (DGUserData) &chosenInd) == DG_OK);

	if (isOK) {
		if (chosenInd == 0)
			WriteReport ("You didn't select anything.");
		else
			WriteReport ("Library index of the selected window: %d", chosenInd);
	}

	return;
}		// Do_LibraryBrowser

// -----------------------------------------------------------------------------
// Shows how to use the list view control
// -----------------------------------------------------------------------------

static void		Do_SurfaceBrowser (void)
{
	Int32		chosenInd = -1;
	bool		isOK;

	isOK = (DGModalDialog (ACAPI_GetOwnResModule (), Browser_GDLGID, ACAPI_GetOwnResModule (), Browser_CB, (DGUserData) &chosenInd) == DG_OK);

	if (isOK) {
		if (chosenInd == 0)
			WriteReport ("You didn't select anything.");
		else
			WriteReport ("Library index of the selected surface: %d", chosenInd);
	}

	return;
}		// Do_SurfaceBrowser

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Fill the user item with the specified color
// -----------------------------------------------------------------------------

void DGDrawColorFill80 (NewDisplay::NativeContext& dC, const DG::Rect &rect, API_RGBColor &rgb, short width, short height)

{
	const float left   = (float) (rect.GetWidth () - width) / 2.0f;
	const float right  = left + width;
	const float top    = (float) (rect.GetHeight () - height) / 2.0f;
	const float bottom = top + height;

	dC.FillRect (left, top, right, bottom,
				 (unsigned char) (rgb.f_red * 255.0),
				 (unsigned char) (rgb.f_green * 255.0),
				 (unsigned char) (rgb.f_blue * 255.0));
	dC.FrameRect (left, top, right, bottom);
}


// -----------------------------------------------------------------------------
// The dialog callback
// -----------------------------------------------------------------------------

static short DGCALLBACK UserItem_CB (short 			message,
								     short			/*dialId*/,
				 			 	     short			item,
				  				     DGUserData		/*userData*/,
								     DGMessageData	msgData)
{
	short result = 0;

#define	UI_ITEM		2

	switch (message) {
		case DG_MSG_INIT:
			break;

		case DG_MSG_CLICK:
			if (item == DG_OK || item == DG_CANCEL)
				result = item;
			break;

		case DG_MSG_UPDATE:
			if (item == UI_ITEM) {
				API_RGBColor	rgb = { 1, 0, 0 };
				const DGUserUpdateData* updateData = (DGUserUpdateData*) msgData;
				NewDisplay::UserItemUpdateNativeContext dC (updateData);
				const DG::Rect rect (0, 0, updateData->right - updateData->left, updateData->bottom - updateData->top);
				DGDrawColorFill80 (dC, rect, rgb, 96, 60);
			}
			break;

	}

	return (result);

#undef UI_ITEM
}		// UserItem_CB


// -----------------------------------------------------------------------------
// Shows how to draw into a user item control
// -----------------------------------------------------------------------------

static void		Do_DrawUserItem (void)
{
	DGModalDialog (ACAPI_GetOwnResModule (), UserItem_GDLGID, ACAPI_GetOwnResModule (), UserItem_CB, 0);

	return;
}		// Do_DrawUserItem

#ifdef __APPLE__
#pragma mark -
#endif

// --- Types -------------------------------------------------------------------

struct TreeViewData {
	GSResModule		ownRes;
};

const short LAY_SHOW = 31801;
const short LAY_HID  = 31802;
const short LAY_OPEN = 31803;
const short LAY_CLOS = 31804;

// --- FolderEnumerator class declaration --------------------------------------

class FolderEnumerator: public IO::TreeEnumerator
{
private:
	const TreeViewData	*tvd;
	short	dialId;
	short	itemId;
	Int32	parentItem;

protected:
	virtual bool	FileFound	  (const IO::Name& fileName) override;
	virtual void	FolderEntered (const IO::Name& folderName, const IO::Folder& current) override;
	virtual void	FolderExited  (const IO::Name& folderName, const IO::Folder& current) override;

public:
	FolderEnumerator (IO::Folder& root, short dialId, short itemId, Int32 parentItem, const TreeViewData* tvd);
	virtual GSErrCode Do (void) override;
};


// --- FolderEnumerator class definition ---------------------------------------

FolderEnumerator::FolderEnumerator (IO::Folder& root, short dialId, short itemId, Int32 parentItem, const TreeViewData* tvd):
	IO::TreeEnumerator (root),
	dialId (dialId),
	itemId (itemId),
	parentItem (parentItem),
	tvd (tvd)
{
}


GSErrCode	FolderEnumerator::Do (void)
{
	parentItem = DGTreeViewInsertItem (dialId, itemId, parentItem, DG_TVI_BOTTOM);

	IO::Location loc;
	GetParent ().GetLocation (&loc);

	IO::Name name;
	loc.GetLastLocalName (&name);
	DGTreeViewSetItemText (dialId, itemId, parentItem, name.ToString ());

	DGTreeViewSetItemIcon (dialId, itemId, parentItem, DG_TVIT_NORMAL, DG::Icon (tvd->ownRes, LAY_CLOS));

	return (TreeEnumerator::Do ());
}


bool	FolderEnumerator::FileFound (const IO::Name& name)
{
	Int32 fileItem = DGTreeViewInsertItem (dialId, itemId, parentItem, DG_TVI_BOTTOM);
	DGTreeViewSetItemText (dialId, itemId, fileItem, name.ToString ());
	DGTreeViewSetItemIcon (dialId, itemId, fileItem, DG_TVIT_NORMAL, DG::Icon (tvd->ownRes, LAY_SHOW));

	return (false);
}


void	FolderEnumerator::FolderEntered (const IO::Name& name, const IO::Folder&)
{
	parentItem = DGTreeViewInsertItem (dialId, itemId, parentItem, DG_TVI_TOP);
	DGTreeViewSetItemText (dialId, itemId, parentItem, name.ToString ());
	DGTreeViewSetItemIcon (dialId, itemId, parentItem, DG_TVIT_NORMAL, DG::Icon (tvd->ownRes, LAY_OPEN));

	return;
}


void	FolderEnumerator::FolderExited (const IO::Name&, const IO::Folder&)
{
	parentItem = DGTreeViewGetItem (dialId, itemId, parentItem, DG_TVG_PARENT);

	return;
}


static bool	TreeViewSetFolderContent (short dialId, short itemId, const TreeViewData* tvd)
{
	IO::Location	loc;
	IO::Location	ioloc;
	API_SpecFolderID	specID = API_ApplicationFolderID;

	ACAPI_ProjectSettings_GetSpecFolder (&specID, &loc);
	ioloc = loc;

	IO::Folder folder (ioloc);
	FolderEnumerator folderEnumerator (folder, dialId, itemId, DG_TVI_ROOT, tvd);

	DGTreeViewDisableDraw (dialId, itemId);
	DGTreeViewDeleteItem (dialId, itemId, DG_ALL_ITEMS);

	try {
		folderEnumerator.Do ();
	}
	catch (...) {
	}
	DGTreeViewEnableDraw (dialId, itemId);
	DGRedrawItem (dialId, itemId);

	return (true);
}


// -----------------------------------------------------------------------------
// Function:	LoadTreeViewDialogIcons
//
// Description:	Load platform specific treeview icons
// -----------------------------------------------------------------------------

static void		LoadTreeViewDialogIcons (TreeViewData* treeViewData)
{
	treeViewData->ownRes	 = ACAPI_GetOwnResModule ();

	return;
}

// -----------------------------------------------------------------------------
// Function:	TreeViewDlgCallBack
//
// Description:	Callback function of the treeview modal dialog
// -----------------------------------------------------------------------------

static short DGCALLBACK		TreeViewDlgCallBack (short message, short dialId, short itemId,
												 DGUserData /*userData*/, DGMessageData /*msgData*/)
{
#define FOLDER_CONTENT	3
#define CHANGE_ICON		4
#define TREEVIEW		5

	Int32 treeItem;

	static TreeViewData		tvd = { 0 };

	switch (message) {
		case DG_MSG_INIT:
			LoadTreeViewDialogIcons (&tvd);
			break;

		case DG_MSG_CLOSE:
			//DeleteTreeViewDialogIcons (&tvd);
			break;

		case DG_MSG_CLICK:
			switch (itemId) {
				case DG_OK:
				case DG_CANCEL:
					return (itemId);

				case FOLDER_CONTENT:
					if (TreeViewSetFolderContent (dialId, TREEVIEW, &tvd)) {
						treeItem = DGTreeViewGetItem (dialId, TREEVIEW, 0, DG_TVG_FIRST);
						if (treeItem != DG_TVI_NONE) {
							DGTreeViewDisableDraw (dialId, TREEVIEW);
							DGTreeViewExpandItem (dialId, TREEVIEW, treeItem);
							treeItem = DGTreeViewGetItem (dialId, TREEVIEW,
								treeItem, DG_TVG_CHILD);
							if (treeItem != DG_TVI_NONE) {
								DGTreeViewExpandItem (dialId, TREEVIEW, treeItem);
								if (DGTreeViewIsItemExpanded (dialId, TREEVIEW,
									treeItem) == 1)
								{
									DGTreeViewCollapseItem (dialId, TREEVIEW, treeItem);
								}
								treeItem = DGTreeViewGetItem (dialId, TREEVIEW,
									treeItem, DG_TVG_LASTLISTED);
							}
							DGTreeViewEnableDraw (dialId, TREEVIEW);
							DGRedrawItem (dialId, TREEVIEW);
						}
					}
					break;

				case CHANGE_ICON:
					treeItem = DGTreeViewGetSelected (dialId, TREEVIEW, DG_TVI_ROOT);
					if (treeItem != DG_TVI_NONE)
						DGTreeViewSetItemIcon (dialId, TREEVIEW, treeItem, DG_TVIT_NORMAL, DG::Icon (tvd.ownRes, LAY_HID));
					break;

				default:
					break;
			}
			break;


		default:
			break;
	}

	return (0);
#undef FOLDER_CONTENT
#undef CHANGE_ICON
#undef TREEVIEW
}

// -----------------------------------------------------------------------------
// Shows how to set treeview items
// -----------------------------------------------------------------------------

static void		Do_TreeView (void)
{
	DGModalDialog (ACAPI_GetOwnResModule (), TreeView_GDLGID, ACAPI_GetOwnResModule (), TreeViewDlgCallBack, 0);

	return;
}		// Do_TreeView


// -----------------------------------------------------------------------------
// Function:	OnlyOneAttr
//
// Description:	Allocate index handle for one attribute item.
// -----------------------------------------------------------------------------

static void		OnlyOneAttr (const API_AttributeIndex& index, GS::Array<API_AttributeIndex>& indices)
{
	if (index.IsPositive ()) {
		indices = GS::Array<API_AttributeIndex> { index };
	} else {
		indices.Clear ();
	}
	return;
}			/* OnlyOneAttr */

// -----------------------------------------------------------------------------
// Function:	AllNeededAttrs
//
// Description:	Allocate index handle for all needed attribute item.
//				The Popup will contain these attributes (materials).
// -----------------------------------------------------------------------------

static GSErrCode		AllNeededAttrs (GS::Array<API_AttributeIndex>& indices, UInt32 custIndex)
{
	GS::Array<API_Attribute> attributes;
	GSErrCode	attributeError = ACAPI_Attribute_GetAttributesByType (API_MaterialID, attributes);
	if (attributeError != NoError)
		return attributeError;

	if (custIndex > attributes.GetSize ())
		custIndex = attributes.GetSize ();

	indices.SetSize (custIndex);

	for (UInt32 attributeIndex = 0; attributeIndex < custIndex; attributeIndex++)
		indices[attributeIndex] = attributes[attributeIndex].header.index;

	for (API_Attribute& attr : attributes) {
		delete attr.material.texture.fileLoc;
		attr.material.texture.fileLoc = nullptr;
	}

	return attributeError;
}

// -----------------------------------------------------------------------------
// Function:	GetMaterListSize
//
// Description:	Calculates palette data size for the material type control.
// -----------------------------------------------------------------------------

static Int32	GetMaterListSize (UC257CallBackRec* callBackRec, bool general)
{
	Int32							nCells;
	GS::Array<API_AttributeIndex>	indices;

	if (callBackRec->action != ACT_GETITEMSIZE && callBackRec->action != ACT_GETLISTSIZE)
		return (0);

	if (callBackRec->action == ACT_GETITEMSIZE)
		OnlyOneAttr (ACAPI_CreateAttributeIndex (callBackRec->value), indices);
	else
		AllNeededAttrs (indices, * (UInt32 *) callBackRec->userData);

	nCells = indices.GetSize ();

	if (general)
		nCells++;

	callBackRec->dataSize = UC257DataSize (nCells);

	return (callBackRec->dataSize);
}

// -----------------------------------------------------------------------------
// Function:	RGBToColor
//
// Description:	Convert API_RGBColor data to CellRGB data.
// -----------------------------------------------------------------------------

static void		RGBToColor (const API_RGBColor* api_rgb, CellRGB* color)
{
	color->red   = static_cast<unsigned char> (api_rgb->f_red *  256);
	color->green = static_cast<unsigned char> (api_rgb->f_green * 256);
	color->blue  = static_cast<unsigned char> (api_rgb->f_blue * 256);
}		/* RGBToColor */

// -----------------------------------------------------------------------------
// Function:	GetMaterInfo
//
// Description:	Fills the palette cell data for the material type control.
// -----------------------------------------------------------------------------

static GSErrCode	GetMaterInfo (const API_AttributeIndex& index, UC257CellData* cell)
{
	char			fillName[API_AttrNameLen];
	bool			existFillName;
	GS::uchar_t*	texName;
	GS::uchar_t*	s;
	API_Attribute	attr;
	GSErrCode		err = NoError;

	BNZeroMemory (&attr, sizeof (attr));
	attr.header.typeID = API_MaterialID;
	attr.header.index = index;

	if (index == APIInvalidAttributeIndex) {
		//CHCopyC ("GENERAL", cell->text);
		GS::ucscpy (cell->text, L("GENERAL"));

		cell->flags = CF257_VALID | CF257_SHOWTEXT;
		return (NoError);
	}

	texName = nullptr;
	existFillName = false;

	cell->value		= index.ToInt32_Deprecated ();
	cell->uniqueID	= index.ToInt32_Deprecated ();
	cell->flags		= CF257_VALID | CF257_SHOWTEXT | CF257_SHOWCOLOR;

	err = ACAPI_Attribute_Get (&attr);
	delete attr.material.texture.fileLoc;
	attr.material.texture.fileLoc = nullptr;
	if (err != NoError)
		return err;

	RGBToColor (&attr.material.surfaceRGB, &cell->foreGroundColor[0]);

	API_Attribute fill;
	BNZeroMemory (&fill, sizeof (API_Attribute));
	fill.header.typeID =  API_FilltypeID;
	fill.header.index = attr.material.ifill;

	err = ACAPI_Attribute_Get (&fill);
	if (err == NoError && fill.filltype.subType != APIFill_Empty) {
		BNCopyMemory (cell->pattern[0], &fill.filltype.bitPat, 8);
		strcpy (fillName, fill.header.name);
		existFillName = true;
		cell->flags |= CF257_SHOWPATTERN;
	}

	GS::uchar_t	tmpTexName[API_UniLongNameLen];
	tmpTexName[0] = 0;
	if (attr.material.texture.status & APITxtr_LinkMat) {
		texName = tmpTexName;
		GS::UTruncate (attr.material.texture.texName, tmpTexName, API_UniLongNameLen);
		cell->flags |= CF257_SHOWICON;
	}

	s = cell->text;
	CHANSI2Unicode (attr.header.name, CStringLen, s, Sizeof32 (attr.header.name));
	s += GS::ucslen32 (s) + 1;

	if (existFillName)
		CHANSI2Unicode (fillName, CStringLen, s, Sizeof32 (fillName));
	else
		GS::ucscpy (s, L(""));
	s += GS::ucslen32 (s) + 1;

	if (texName != nullptr)
		GS::ucscpy (s, texName);
	else
		GS::ucscpy (s, L(""));

	return (NoError);
}			/* GetMaterInfo */

// -----------------------------------------------------------------------------
// Function:	GetMaterList
//
// Description:	Generates the popup list for the material control palette.
// -----------------------------------------------------------------------------

static void*	GetMaterList (UC257CallBackRec* callBackRec, bool general)
{
	Int32							nCells;
	GS::Array<API_AttributeIndex>	indices;
	UC257HeadData*					header;
	UC257CellData*					cell;

	if (callBackRec->action != ACT_GETITEM && callBackRec->action != ACT_GETLIST)
		return (nullptr);

	if (callBackRec->action == ACT_GETITEM)
		OnlyOneAttr (ACAPI_CreateAttributeIndex (callBackRec->value), indices);
	else
		AllNeededAttrs (indices, * (UInt32 *) callBackRec->userData);

	nCells = indices.GetSize ();

	if (general)
		nCells++;

	if (callBackRec->dataSize < UC257DataSize (nCells)) {
		return (nullptr);
	}

	header = (UC257HeadData*) callBackRec->data;
	header->minWidth = 128;
	header->nColumns = 0;
	header->nCells   = 0;

	GS::ucscpy (header->missingName, L("MISSING"));

	cell = &header->cell[0];

	if (general) {
		GetMaterInfo  (APIInvalidAttributeIndex, cell++);
		header->nCells++;
	}

	for (const API_AttributeIndex& attrInd : indices) {
		if (GetMaterInfo (attrInd, cell) == NoError) {
			cell++;
			header->nCells++;
		}
	}

	return ((void*) header);
}			/* GetMaterList */

// -----------------------------------------------------------------------------
// Callback function of the Material Popup
// -----------------------------------------------------------------------------

static void CCALL	MaterialControlCallBack (UC257CallBackRec* callBackRec)
{
	if (callBackRec->action == ACT_GETLISTSIZE || callBackRec->action == ACT_GETITEMSIZE)
		GetMaterListSize (callBackRec, false);
	else if (callBackRec->action == ACT_GETLIST || callBackRec->action == ACT_GETITEM)
		GetMaterList (callBackRec, false);

	return;
}

// -----------------------------------------------------------------------------
// Custom Material dialog handler (callback)
// -----------------------------------------------------------------------------

static short DGCALLBACK Material_Handler (short				message,
										  short				dialogID,
										  short				item,
										  DGUserData		userData,
										  DGMessageData		/*msgData*/)
{
	short	result = 0;

// Dialog item IDs
#define MaterialItm		3

	switch (message) {
		case DG_MSG_INIT:
			// initialize user controls
			UC257SetUserData (dialogID, MaterialItm, (void *) userData);		// contains numItems
			UC257SetCallBack (dialogID, MaterialItm, MaterialControlCallBack);
			DGSetItemValLong (dialogID, MaterialItm, 1);
			break;

		case DG_MSG_CLICK:
			switch (item) {
			case DG_OK:
			case DG_CANCEL:
				result = item;
				break;
			}
			break;

		case DG_MSG_CLOSE:
			result = item;
			break;
		}

	return (result);

#undef MaterialItm
}		// Material_Handler

// -----------------------------------------------------------------------------
// Material popup contains custom materials
// -----------------------------------------------------------------------------

static void		Do_CustomMaterial (void)
{
	Int32		numItems = 20;		// display the first 20 items; will be passed as
									//	user data
	// Get the dialog from our resource file
	DGModalDialog (ACAPI_GetOwnResModule (), Material_GDLGID, ACAPI_GetOwnResModule (), Material_Handler, (DGUserData) &numItems);

	return;
}		// Do_CustomMaterial


// -----------------------------------------------------------------------------
// Modal progress dialog
// -----------------------------------------------------------------------------
struct CBData;

typedef GSErrCode LibPartPlacerProc (CBData *cbd);

typedef struct CBData {
	Int32				cnt;
	Int32				numLPs;
	Int32				goodLPs;
	double				xPos;
	API_Element			defElem;
	LibPartPlacerProc	*proc;
#ifdef WINDOWS
	HCURSOR				hCursor;
#endif
} CBData;

// -----------------------------------------------------------------------------
// Progress dialog handler (internal callback)
// -----------------------------------------------------------------------------

static GSErrCode LibPartPlacer (CBData *cbd)

{
	double				aa = 0, bb = 0;
	Int32				addParNum;
	API_Element			elem;
	API_ElementMemo		memo;
	GSErrCode			err = Error;

	BNZeroMemory (&elem, sizeof (elem));
	BNZeroMemory (&memo, sizeof (memo));
	if (ACAPI_LibraryPart_GetParams (cbd->cnt, &aa, &bb, &addParNum, &memo.params) == NoError) {
		elem = cbd->defElem;
		elem.object.libInd = cbd->cnt;
		elem.object.pos.x  = cbd->xPos;
		elem.object.pos.y  = 0;
		elem.object.xRatio = aa;
		elem.object.yRatio = bb;
		if (ACAPI_Element_Create (&elem, &memo) == NoError) {
			cbd->xPos += aa + 0.2;
			err = NoError;
		}
	}
	ACAPI_DisposeElemMemoHdls (&memo);

	return err;

}	// LibPartPlacer

// -----------------------------------------------------------------------------
// Progress dialog handler (callback)
// -----------------------------------------------------------------------------

static short DGCALLBACK Progress_Handler (short				message,
										  short				dialogID,
										  short				item,
										  DGUserData		userData,
										  DGMessageData		/*msgData*/)
{
// Dialog item IDs
#define CancelBtn		1
#define ProcTextItm		2

	short	result = 0;
	CBData	*cbd = (CBData *) userData;

	if (cbd == nullptr)
		return result;


	switch (message) {
		case DG_MSG_INIT:
			DGEnableMessage (dialogID, DG_ALL_ITEMS, DG_MSG_NULL);
			break;

		case DG_MSG_NULL:
			if (cbd->cnt < cbd->numLPs) {
				++cbd->cnt;
				if ((cbd->cnt % 20) == 0) {
					char	progText[128];

					sprintf (progText, "Placed %ld libparts", (GS::LongForStdio) cbd->cnt);
					DGSetItemText (dialogID, ProcTextItm, progText);
				}

#ifdef WINDOWS
				if (cbd->cnt > 20) {
					if (cbd->hCursor == nullptr)
						cbd->hCursor = SetCursor (LoadCursorW (nullptr, MAKEINTRESOURCEW (IDC_WAIT)));
					else
						SetCursor (LoadCursorW (nullptr, MAKEINTRESOURCEW (IDC_WAIT)));
				}
#endif

				if (cbd->proc != nullptr && (*cbd->proc) (cbd) == NoError)
					++cbd->goodLPs;

			} else {
				result = 1;
			}
			break;

		case DG_MSG_CLICK:
			switch (item) {
			case CancelBtn:
			case DG_CLOSEBOX:
				result = item;
				break;
			}
			break;

		case DG_MSG_CLOSE:
#ifdef WINDOWS
			if (cbd->hCursor)
				SetCursor (cbd->hCursor);
#endif
			result = item;
			break;
		}

	return (result);

#undef ProcTextItm
#undef CancelBtn
}		// Progress_Handler


// -----------------------------------------------------------------------------
// A simple progress window implementation
// -----------------------------------------------------------------------------

static void		Do_ProgressWindow (void)
{
	CBData		cbd;
	char		progText[128];

	BNZeroMemory (&cbd, sizeof (CBData));
	cbd.defElem.header.type = API_ObjectID;
	if (ACAPI_Element_GetDefaults (&cbd.defElem, nullptr) != NoError)
		return;
	if (ACAPI_LibraryPart_GetNum (&cbd.numLPs) != NoError || cbd.numLPs == 0)
		return;
	cbd.proc = LibPartPlacer;

	ACAPI_CallUndoableCommand ("Place library parts",
		[&] () -> GSErrCode {
			// Get the dialog from our resource file
			DGModalDialog (ACAPI_GetOwnResModule (), Progress_GDLGID, ACAPI_GetOwnResModule (), Progress_Handler, (DGUserData) &cbd);
			return NoError;
		});

	if (cbd.goodLPs > 0) {
		sprintf (progText, "Placed %ld library part%s", (GS::LongForStdio) cbd.goodLPs, cbd.goodLPs > 1 ? "s" : " ");
		DGAlert (DG_INFORMATION, "Process report", progText, GS::UniString(), "OK");
	}

	return;
}		// Do_ProgressWindow


// -----------------------------------------------------------------------------
// UC test dialog handler (callback)
// -----------------------------------------------------------------------------
struct API_LibPartEx : public API_LibPart {
	GSHandle	picData;
};


typedef struct UCBData {
	GS::Array<API_LibPartEx>	lps;
	Int32	chosen;
} UCBData;

// -----------------------------------------------------------------------------
// Draws the preview picture into the user item
// -----------------------------------------------------------------------------

static void DrawMyPict (DGUserUpdateData	*upd,
						GSHandle			picData)
{
	if (upd == 0 || upd->drawContext == 0 || picData == 0)
		return;

	const float width = (float)(upd->right - upd->left);
	const float height = (float)(upd->bottom - upd->top);

	NewDisplay::UserItemUpdateNativeContext context (upd);
	NewDisplay::NativeImage image = GX::Image (picData, FTM::UnknownType).ToNativeImage ();

	const float xScale = (float)width / (float)image.GetWidth ();
	const float yScale = (float)height / (float)image.GetHeight ();

	context.DrawImage (image, xScale, yScale, 0.0, 0.0, 0.0, false);
}	// DrawMyPict

// -----------------------------------------------------------------------------
// UC test dialog handler (callback)
// -----------------------------------------------------------------------------

static short DGCALLBACK UCTest_Handler (short			message,
										short			dialogID,
										short			item,
										DGUserData		userData,
										DGMessageData	msgData)
{
// Dialog item IDs
#define LPPopup			2
#define PreviewPict		3

	short	result = 0;
	UCBData	*cbd = (UCBData *) userData;

	if (cbd == nullptr)
		return result;


	switch (message) {
		case DG_MSG_INIT:
			UCPopupSetButtonType (dialogID, LPPopup, BT_POPUP_TEXTONLY);
			for (ULong ii = 1; ii <= cbd->lps.GetSize (); ii++) {
				GS::UniString	menuStr (cbd->lps[ii-1].docu_UName);
				UCPopupAppendItem (dialogID, LPPopup);
				UCPopupSetItemText (dialogID, LPPopup, ii, menuStr);
				if (ii == 1) {
					UCPopupSetButtonText (dialogID, LPPopup, menuStr);
					cbd->chosen = 0;
				}
			}
			break;

		case DG_MSG_CHANGE:
			switch (item) {
			case LPPopup:
				cbd->chosen = UCPopupGetSelectedItem (dialogID, LPPopup) - 1;

				GS::UniString	menuStr (cbd->lps[cbd->chosen].docu_UName);
				UCPopupSetButtonText (dialogID, LPPopup, menuStr);
				DGUserRedrawItem (dialogID, PreviewPict, true);
				break;
			}
			break;

		case DG_MSG_CLICK:
			switch (item) {
			case DG_OK:
			case DG_CLOSEBOX:
				result = item;
				break;
			}
			break;

		case DG_MSG_UPDATE:
			switch (item) {
				case PreviewPict:
					DrawMyPict ((DGUserUpdateData *) msgData, cbd->lps[cbd->chosen].picData);
				break;
			}
			break;

		case DG_MSG_CLOSE:
			result = item;
			break;
		}

	return (result);

#undef PreviewPict
#undef LPPopup
}		// UCTest_Handler


// -----------------------------------------------------------------------------
// A simple progress window implementation
// -----------------------------------------------------------------------------

static void		Do_UCTest (void)
{
	UCBData			cbd;
	API_LibPartEx	lp, lp1;

	cbd.chosen = 0;
	BNZeroMemory (&lp, sizeof (lp));

	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::LabelLibPartID);

	CHCopyC (unID.ToUniString ().ToCStr (), lp.parentUnID);	// Label
	lp1 = lp;

	while (ACAPI_LibraryPart_Search (&lp, false) == NoError) {
		API_LibPartSection	lpSect;
		API_MimePicture		mimePic;
		GSErrCode			err = NoError;

		// Get the preview picture
		BNZeroMemory (&lpSect, sizeof (API_LibPartSection));
		lpSect.sectType = API_SectInfoGIF;
		BNZeroMemory (&mimePic, sizeof (mimePic));
		err = ACAPI_LibraryPart_GetSection (lp.index, &lpSect, &mimePic.inputHdl, nullptr);
		if (err == NoError) {
			mimePic.inContainsMime = true;
			mimePic.outDepth	   = APIColorDepth_FromSourceImage;
			mimePic.mimeOut		   = *mimePic.inputHdl;		// use the original mime type

			err = ACAPI_Conversion_ConvertMimePicture (&mimePic);
		}
		if (err == NoError && mimePic.outputHdl != nullptr) {
			lp.picData = mimePic.outputHdl;
		} else {
			BMhKill (&mimePic.outputHdl);
		}
		BMhKill (&mimePic.inputHdl);
		cbd.lps.Push (lp);

		lp1.index = lp.index;
		lp = lp1;
	}

	// Get the dialog from our resource file
	DGModalDialog (ACAPI_GetOwnResModule (), UCTest_GDLGID, ACAPI_GetOwnResModule (), UCTest_Handler, (DGUserData) &cbd);

	if (cbd.chosen > 0) {
		char	docuname[256];
		CHTruncate (GS::UniString (cbd.lps[cbd.chosen].docu_UName).ToCStr (), docuname, sizeof (docuname));
		DGAlert (DG_INFORMATION, "Chosen library part", docuname, GS::UniString(), "OK");
	}

	for (ULong ii = 0; ii < cbd.lps.GetSize (); ii++) {
		delete cbd.lps[ii].location;
		BMhKill (&cbd.lps[ii].picData);
	}

	return;
}		// Do_UCTest


// -----------------------------------------------------------------------------
// Profile Control dialog handler (callback)
// -----------------------------------------------------------------------------

static short DGCALLBACK Profile_Handler ( short				message,
										  short 			dialogID,
										  short				item,
										  DGUserData		/*userData*/,
										  DGMessageData		/*msgData*/)
{
	// Profile dialog item IDs
	enum {
		WallProfileItm = 9,
		BeamProfileItm = 10,
		ColumnProfileItm = 11,
		HandrailProfileItm = 12,
		OtherGDLObjectProfileItm = 13,
		AllProfileItm = 14
	};

	static GS::Owner<API_AttributePicker> wallProfilePicker;
	static GS::Owner<API_AttributePicker> beamProfilePicker;
	static GS::Owner<API_AttributePicker> columnProfilePicker;
	static GS::Owner<API_AttributePicker> handrailProfilePicker;
	static GS::Owner<API_AttributePicker> otherGDLObjectProfilePicker;
	static GS::Owner<API_AttributePicker> allProfilePicker;

	short result = 0;
	switch (message) {
		case DG_MSG_INIT:
		{
			API_AttributePickerParams app;
			app.pushCheckAppearance = API_AttributePickerParams::PushCheckAppearance::ArrowIconAndText;
			app.emptySelectionBehaviour = API_AttributePickerParams::EmptySelectionBehaviour::SelectNothing;

			{
				app.dialogID = dialogID;
				app.itemID = WallProfileItm;
				app.type = APIUserControlType_WallProfile;
				DBVERIFY (ACAPI_Dialog_CreateAttributePicker (app, wallProfilePicker) == NoError);
			}

			{
				app.dialogID = dialogID;
				app.itemID = BeamProfileItm;
				app.type = APIUserControlType_BeamProfile;
				DBVERIFY (ACAPI_Dialog_CreateAttributePicker (app, beamProfilePicker) == NoError);
			}

			{
				app.dialogID = dialogID;
				app.itemID = ColumnProfileItm;
				app.type = APIUserControlType_ColumnProfile;
				DBVERIFY (ACAPI_Dialog_CreateAttributePicker (app, columnProfilePicker) == NoError);
			}

			{
				app.dialogID = dialogID;
				app.itemID = HandrailProfileItm;
				app.type = APIUserControlType_HandrailProfile;
				DBVERIFY (ACAPI_Dialog_CreateAttributePicker (app, handrailProfilePicker) == NoError);
			}

			{
				app.dialogID = dialogID;
				app.itemID = OtherGDLObjectProfileItm;
				app.type = APIUserControlType_OtherGDLObjectProfile;
				DBVERIFY (ACAPI_Dialog_CreateAttributePicker (app, otherGDLObjectProfilePicker) == NoError);
			}

			{
				app.dialogID = dialogID;
				app.itemID = AllProfileItm;
				app.type = APIUserControlType_AllProfile;
				DBVERIFY (ACAPI_Dialog_CreateAttributePicker (app, allProfilePicker) == NoError);
			}
		} break;

		case DG_MSG_CLICK:
			result = item;
			break;

		case DG_MSG_CHANGE:
			switch (item) {
				case WallProfileItm:
					if (DBVERIFY (wallProfilePicker != nullptr) && wallProfilePicker->Invoke ()) {
						wallProfilePicker->Refresh ();
					}
					break;
				case BeamProfileItm:
					if (DBVERIFY (beamProfilePicker != nullptr) && beamProfilePicker->Invoke ()) {
						beamProfilePicker->Refresh ();
					}
					break;
				case ColumnProfileItm:
					if (DBVERIFY (columnProfilePicker != nullptr) && columnProfilePicker->Invoke ()) {
						columnProfilePicker->Refresh ();
					}
					break;
				case HandrailProfileItm:
					if (DBVERIFY (handrailProfilePicker != nullptr) && handrailProfilePicker->Invoke ()) {
						handrailProfilePicker->Refresh ();
					}
					break;
				case OtherGDLObjectProfileItm:
					if (DBVERIFY (otherGDLObjectProfilePicker != nullptr) && otherGDLObjectProfilePicker->Invoke ()) {
						otherGDLObjectProfilePicker->Refresh ();
					}
					break;
				case AllProfileItm:
					if (DBVERIFY (allProfilePicker != nullptr) && allProfilePicker->Invoke ()) {
						allProfilePicker->Refresh ();
					}
					break;
				default:
					break;
			} break;

		case DG_MSG_CLOSE:
			result = item;
			break;
		}

	return (result);
}		// Profile_Handler

// -----------------------------------------------------------------------------
// Profile Controls
// -----------------------------------------------------------------------------

static void		Do_ProfileControls (void)
{
	// Get the dialog from our resource file
	DGModalDialog (ACAPI_GetOwnResModule (), Profile_GDLGID, ACAPI_GetOwnResModule (), Profile_Handler, 0);
}		// Do_ProfileControls


// -----------------------------------------------------------------------------
// Function to show the BuildingMaterial Dialog
// -----------------------------------------------------------------------------

static void		Show_BuildingMaterialDialog (void)
{
	BuildingMaterialDialog		dialog (ACAPI_GetOwnResModule (), BUILDING_MATERIAL_DIALOG_RESID);
	if (DBERROR (dialog.GetId () == 0))
		return;

	BuildingMaterialObserver	observer (&dialog);
	dialog.Invoke ();
}


// -----------------------------------------------------------------------------
// Function to show the AttributeList Dialog
// -----------------------------------------------------------------------------

static void		Show_AttributeListDialog (void)
{
	AttributeListDialog		dialog;
	if (DBERROR (dialog.GetId () == 0))
		return;

	dialog.Invoke ();
}


static GSErrCode LoadModuleData (GS::HashTable<GS::UniString, ConvertStatus>& statusTable, TabId& lastSortedTab, bool& lastDirectionUp)
{
	GSErrCode		err = NoError;
	API_ModulData	info;

	BNZeroMemory (&info, sizeof (info));
	err = ACAPI_ModulData_Get (&info);
	if (err != NoError)
		return err;

	if (info.dataHdl == nullptr || BMhGetSize (info.dataHdl) == 0)
		return NoError;

	if (info.dataVersion != 6)
		return err;

	GS::MemoryIChannel32 memChannel (*info.dataHdl, BMGetHandleSize (info.dataHdl));

	IO::SetPlatformIProtocol (memChannel, (GS::PlatformSign) info.platformSign);
	memChannel.Read ((int&) lastSortedTab);
	memChannel.Read (lastDirectionUp);
	try {
		statusTable.Read (memChannel);
	} catch (...) {
		statusTable.Clear ();
	}

	err = memChannel.GetInputStatus ();

	BMhKill (&info.dataHdl);

	return err;
}

static GSErrCode StoreModuleData (const GS::HashTable<GS::UniString, ConvertStatus>& statusTable, const TabId& lastSortedTab, const bool& lastDirectionUp)
{
	GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
	GSErrCode errCode = memChannel.Write ((int&) lastSortedTab);
	if (errCode == NoError) {
		errCode = memChannel.Write (lastDirectionUp);
		if (errCode == NoError) {
			errCode = statusTable.Write (memChannel);
			if (errCode == NoError) {
				/* store module data */
				USize		nBytes = memChannel.GetDataSize ();			// bytes in IO buffers not yet flushed are also included
				const char*	pData  = memChannel.GetDestination ();		// flushes IO buffers

				DBASSERT (pData != nullptr);

				API_ModulData	info;
				BNZeroMemory (&info, sizeof (info));
				info.dataVersion  = 6;
				info.platformSign = GS::Act_Platform_Sign;

				info.dataHdl = BMAllocateHandle (nBytes, ALLOCATE_CLEAR, 0);
				if (info.dataHdl == nullptr)
					return ErrMemoryFull;

				BNCopyMemory (*info.dataHdl, pData, nBytes);
				errCode = ACAPI_ModulData_Store (&info);

				BMhFree (info.dataHdl);
			}
		}
	}

	return errCode;
}

// -----------------------------------------------------------------------------
// Function to show the Layer Dialog
// -----------------------------------------------------------------------------


static void		Show_LayerDialog (void)
{
	GS::HashTable<GS::UniString, ConvertStatus> 	statusTable;
	TabId											lastSortedTab;
	bool 											lastDirectionUp;
	if (LoadModuleData (statusTable, lastSortedTab, lastDirectionUp) != NoError) {
		statusTable.Clear ();
		lastSortedTab = LayerNameTab;
		lastDirectionUp = true;
	}

	LayerDialog		dialog (ACAPI_GetOwnResModule (), statusTable, lastSortedTab, lastDirectionUp);
	LayerObserver	observer (&dialog);

	if (dialog.Invoke ())
		StoreModuleData (statusTable, lastSortedTab, lastDirectionUp);

	return;
}


// -----------------------------------------------------------------------------
// Function to show the Basic Element Icons RollDownDialog
// -----------------------------------------------------------------------------

static void		Show_BasicElementIconsRollDownDialog (void)
{
	BasicElementIconsRollDownDialog		dialog (nullptr);
	if (DBERROR (dialog.GetId () == 0))
		return;

	dialog.Invoke ();
}


// -----------------------------------------------------------------------------
// Function to show the FontPopUp Dialog
// -----------------------------------------------------------------------------

static void		Show_FontPopUpDialog (void)
{
	FontPopupDialog dialog;
	if (DBERROR (dialog.GetId () == 0))
		return;
	dialog.Invoke ();
}


// -----------------------------------------------------------------------------
// Function to show the SplitButton Dialog
// -----------------------------------------------------------------------------

static void		Show_SplitButtonDialog (void)
{
	SplitButtonDialog dialog;
	if (DBERROR (dialog.GetId () == 0))
		return;

	dialog.Invoke ();
}


// -----------------------------------------------------------------------------
// Function to show the OwnerDrawnListBox Modeless Dialog
// -----------------------------------------------------------------------------

static void		Show_OwnerDrawnListBoxDialog (void)
{
	OwnerDrawnListBoxPalette::GetInstance ().Show ();
}


// -----------------------------------------------------------------------------
// Function to delete item from the OwnerDrawnListBox
// -----------------------------------------------------------------------------

static void		Delete_LastItem_From_OwnerDrawnListBox (void)
{
	OwnerDrawnListBoxPalette::GetInstance ().GetListBox ().DeleteItem (DG::ListBox::BottomItem);
}


static void		ToggleInstantDialogResourceLoadingStatus (void)
{
	bool isEnabled = DG::IsInstantDialogResourceLoadingEnabled ();
	if (isEnabled) {
		DG::SetInstantDialogResourceLoadingStatus (false);
	} else {
		DG::SetInstantDialogResourceLoadingStatus (true);
	}

	// Toggle menu check
	API_MenuItemRef	itemRef;
	BNZeroMemory (&itemRef, sizeof (API_MenuItemRef));
	itemRef.menuResID = DGTEST_MENU_STRINGSID;
	itemRef.itemIndex = 19;
	GSFlags itemFlags = 0;
	GSErrCode err = ACAPI_MenuItem_GetMenuItemFlags (&itemRef, &itemFlags);
	if (err == NoError) {
		if (isEnabled) {
			itemFlags &= ~API_MenuItemChecked;
		} else {
			itemFlags |= API_MenuItemChecked;
		}
		ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &itemFlags);
	}
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
		case DGTEST_MENU_STRINGSID:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:		Do_FillSettings ();							break;
				case 2:		Do_LibraryBrowser ();						break;
				case 3:		Do_DrawUserItem ();							break;
				case 4:		Do_TreeView ();								break;
				case 5:		Do_CustomMaterial ();						break;
				case 6:		Do_ProgressWindow ();						break;
				case 7:		Do_UCTest ();								break;
				case 8:		Do_ProfileControls ();						break;
				case 9:		Do_SurfaceBrowser ();						break;
				case 10:	Show_BuildingMaterialDialog ();				break;
				case 11:	Show_AttributeListDialog ();				break;
				case 12:	Show_LayerDialog ();						break;
				case 13:	Show_BasicElementIconsRollDownDialog ();	break;
				case 14:	Show_FontPopUpDialog();						break;
				case 15:	Show_SplitButtonDialog ();					break;
				case 17:	Show_OwnerDrawnListBoxDialog ();			break;
				case 18:	Delete_LastItem_From_OwnerDrawnListBox ();	break;
				case 19:	ToggleInstantDialogResourceLoadingStatus ();break;
				default:												break;
			}
			break;
		default:
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
	RSGetIndString (&envir->addOnInfo.name, DGTEST_ABOUT_STRINGSID, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, DGTEST_ABOUT_STRINGSID, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (DGTEST_MENU_STRINGSID, DGTEST_PROMPT_STRINGSID, MenuCode_UserDef, MenuFlag_Default);
	if (err != NoError)
		DBPrintf ("DG_Test:: RegisterInterface() ACAPI_Register_Menu failed\n");

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (DGTEST_MENU_STRINGSID, MenuCommandHandler);

	// If the palette is constructed and registered, the add-on will be not unloaded from the memory while ARCHICAD runs.
	ACAPI_RegisterModelessWindow (OwnerDrawnListBoxPalette::ODLBPaletteRefId (),
								  OwnerDrawnListBoxPalette::PaletteAPIControlCallBack,
								  API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
								  API_PalEnabled_InteriorElevation + API_PalEnabled_3D +
								  API_PalEnabled_Detail + API_PalEnabled_Worksheet + API_PalEnabled_Layout +
								  API_PalEnabled_DocumentFrom3D, GSGuid2APIGuid (OwnerDrawnListBoxPalette::ODLBPaletteGuid ()));

	if (err != NoError)
		DBPrintf ("DG_Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	ACAPI_UnregisterModelessWindow (OwnerDrawnListBoxPalette::ODLBPaletteRefId ());
	return NoError;
}		// FreeData
