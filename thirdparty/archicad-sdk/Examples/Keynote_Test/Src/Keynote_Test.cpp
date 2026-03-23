#include "Resources.hpp"

// from API
#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APICommon.h"

// from ACAPI
#include "ACAPI/Result.hpp"
#include "ACAPI/KeynoteManager.hpp"

namespace {

static void Report (const GS::UniString& message)
{
	WriteReport (message.ToCStr ().Get ());
}


static void Dump (const GS::UniString& message)
{
	Report (message);
}


static void DumpItem (const ACAPI::Keynote::KeynoteItem& item, UInt8 depth)
{
	GS::UniString pad;
	pad.Pad (depth, ' ');

	GS::UniString itemString = pad + "Item  - ";
	itemString += "ID: '" + APIGuid2GSGuid (item.GetId ()).ToUniString () + "', ";
	itemString += "Key: '" + item.GetKey () + "', ";
	itemString += "Title: '" + item.GetTitle () + "', ";
	itemString += "Description: '" + item.GetDescription () + "', ";
	itemString += "Reference: ' " + item.GetReference () + "'";
	Dump (itemString);
}


static void DumpFolder (const ACAPI::Keynote::KeynoteFolder& folder, UInt8 depth = 0)
{
	GS::UniString pad;
	pad.Pad (depth, ' ');

	GS::UniString folderString = pad + "Folder  - ";
	folderString += "ID: '" + APIGuid2GSGuid (folder.GetId ()).ToUniString () + "', ";
	folderString += "Key: '" + folder.GetKey () + "', ";
	folderString += "Title: '" + folder.GetTitle () + "', ";
	folderString += "Reference: '" + folder.GetReference () + "', ";
	Dump (folderString);
	ACAPI::Result<std::vector<ACAPI::Keynote::KeynoteFolder>> subFolders = folder.GetDirectSubFolders ();
	if (subFolders.IsErr ()) {
		Report ("Failed to get subfolders.");
		return;
	}
	for (auto& subFolder : subFolders.Unwrap ()) {
		DumpFolder (subFolder, depth + 1);
	}
	ACAPI::Result<std::vector<ACAPI::Keynote::KeynoteItem>> items = folder.GetDirectItems ();
	for (auto& item : items.Unwrap ()) {
		DumpItem (item, depth + 1);
	}
}


static ACAPI::Result<ACAPI::Keynote::KeynoteFolder> GetRootFolder ()
{
	// ! [Get-Root-Folder-Example]

	ACAPI::Result<ACAPI::Keynote::KeynoteManager> manager = ACAPI::Keynote::CreateKeynoteManager ();
	if (manager.IsErr ()) {
		Report (GS::UniString ("Failed to get Keynote Manager: ") + manager.UnwrapErr ().text.c_str ());
		return ACAPI::Result<ACAPI::Keynote::KeynoteFolder> {manager.UnwrapErr (), manager.Token ()};
	}

	const ACAPI::Result<ACAPI::Keynote::KeynoteFolder> rootFolder = manager->GetRootFolder ();
	if (rootFolder.IsErr ()) {
		Report (GS::UniString ("Failed to get keynote root folder: ") + rootFolder.UnwrapErr ().text.c_str ());
		return ACAPI::Result<ACAPI::Keynote::KeynoteFolder> {rootFolder.UnwrapErr (), rootFolder.Token ()};
	}

	// ! [Get-Root-Folder-Example]

	return rootFolder;
}


void DumpKeynoteTree ()
{
	ACAPI::Result<ACAPI::Keynote::KeynoteFolder> rootFolder = GetRootFolder ();
	DumpFolder (rootFolder.Unwrap ());
}


void ModifyFolder ()
{
	ACAPI::Keynote::KeynoteFolder rootFolder = GetRootFolder ().Unwrap ();
	ACAPI::Result<std::vector<ACAPI::Keynote::KeynoteFolder>> subFolders = rootFolder.GetDirectSubFolders ();
	if (subFolders.IsErr ()) {
		Report (GS::UniString ("Failed to get subfolders: ")  + subFolders.UnwrapErr ().text.c_str ());
		return;
	}
	if (subFolders.Unwrap ().size () < 1) {
		Report ("Failed to get first folder of root folder.");
		return;
	}
	// ! [Modify-Folder-Example]

	ACAPI::Keynote::KeynoteFolder firstFolder = subFolders.Unwrap ()[0];
	ACAPI_CallUndoableCommand ("Modify first keynote folder", [&firstFolder]() -> GSErrCode {
		if (firstFolder.SetKey ("KF-API").IsErr ()){
			return Error;
		}
		if (firstFolder.SetTitle ("Title set for Folder by API").IsErr ()){
			return Error;
		}
		if (firstFolder.SetReference ("Keynote folder modified by the test addon").IsErr ()) {
			return Error;
		}
		return NoError;
	});

	// ! [Modify-Folder-Example]
}


void ModifyItem ()
{
	ACAPI::Keynote::KeynoteFolder rootFolder = GetRootFolder ().Unwrap ();
	ACAPI::Result<std::vector<ACAPI::Keynote::KeynoteItem>> items = rootFolder.GetDirectItems ();
	if (items.IsErr ()) {
		Report (GS::UniString ("Failed to get items: ") + items.UnwrapErr ().text.c_str ());
		return;
	}
	if (items.Unwrap ().size () < 1) {
		Report (GS::UniString ("Failed to get first item of root folder."));
		return;
	}
	// ! [Modify-Item-Example]

	ACAPI::Keynote::KeynoteItem firstItem = items.Unwrap ()[0];
	ACAPI_CallUndoableCommand ("Modify first keynote item", [&firstItem]() -> GSErrCode {
		if (firstItem.SetKey ("K-API").IsErr ()) {
			return Error;
		}
		if (firstItem.SetTitle ("Title set for Item by API").IsErr ()) {
			return Error;
		}
		if (firstItem.SetDescription ("Keynote item modified by the test addon").IsErr ()){
			return Error;
		}
		if (firstItem.SetReference ("Keynote item modified by the test addon").IsErr ()) {
			return Error;
		}
		return NoError;
	});

	// ! [Modify-Item-Example]
}


void AddFolder ()
{	
	ACAPI::Keynote::KeynoteFolder rootFolder = GetRootFolder ().Unwrap ();
	
	// ! [Add-Folder-Example]

	ACAPI_CallUndoableCommand ("Add new keynote folder under root", [&rootFolder]() -> GSErrCode {
		for (UInt32 i = 0; i < 10; ++i) {
			const ACAPI::Keynote::FolderKey key ("KF-" + GS::ValueToUniString (i));
			const ACAPI::Keynote::Title title ("Title");
			ACAPI::Result<ACAPI::Keynote::KeynoteFolder> newFolder = rootFolder.AddSubFolder (key, title);
			if (newFolder.IsOk ()) {
				newFolder.Unwrap ().SetReference ("New folder Reference");
				return NoError;
			}
		}
		return Error;
	});

	// ! [Add-Folder-Example]
}


void AddItem ()
{
	ACAPI::Keynote::KeynoteFolder rootFolder = GetRootFolder ().Unwrap ();
	
	// ! [Add-Item-Example]

	ACAPI_CallUndoableCommand ("Add new keynote item under root", [&rootFolder]() -> GSErrCode {
		for (UInt32 i = 0; i < 10; ++i) {
			const ACAPI::Keynote::ItemKey key ("K-" + GS::ValueToUniString (i));
			ACAPI::Result<ACAPI::Keynote::KeynoteItem> newItem = rootFolder.AddItem (key);
			if (newItem.IsOk ()) {
				newItem.Unwrap ().SetTitle ("New item Title");
				newItem.Unwrap ().SetDescription ("New item Description");
				newItem.Unwrap ().SetReference ("New item Reference");
				return NoError;
			}
		}
		return Error;
	});

	// ! [Add-Item-Example]
}


void RemoveFolder ()
{
	ACAPI::Keynote::KeynoteFolder rootFolder = GetRootFolder ().Unwrap ();
	
	// ! [Remove-Folder-Example]

	ACAPI_CallUndoableCommand ("Remove keynote folder from under root", [&rootFolder]() -> GSErrCode {
		for (UInt32 i = 0; i < 10; ++i) {
			const ACAPI::Keynote::FolderKey key ("KF-" + GS::ValueToUniString (i));
			const ACAPI::Keynote::Title title ("Title");
			ACAPI::Result<void> removeResult = rootFolder.RemoveSubFolder (key, title);
			if (removeResult.IsOk ()) {
				return NoError;
			}
		}
		return Error;
	});

	// ! [Remove-Folder-Example]
}


void RemoveItem ()
{
	ACAPI::Keynote::KeynoteFolder rootFolder = GetRootFolder ().Unwrap ();

	// ! [Remove-Item-Example]
	
	ACAPI_CallUndoableCommand ("Remove keynote folder from under root", [&rootFolder]() -> GSErrCode {
		for (UInt32 i = 0; i < 10; ++i) {
			const ACAPI::Keynote::ItemKey key ("K-" + GS::ValueToUniString (i));
			ACAPI::Result<void> removeResult = rootFolder.RemoveItem (key);
			if (removeResult.IsOk ()) {
				return NoError;
			}
		}
		return Error;
	});

	// ! [Remove-Item-Example]
}


void PlaceKeynoteLabel ()
{
	// Get keynote item
	ACAPI::Result<ACAPI::Keynote::KeynoteManager> manager = ACAPI::Keynote::CreateKeynoteManager ();
	if (manager.IsErr ()) {
		Report (GS::UniString ("Failed to get Keynote Manager: ") + manager.UnwrapErr ().text.c_str ());
		return;
	}

	const ACAPI::Result<std::vector<ACAPI::Keynote::KeynoteItem>> items = manager.Unwrap ().GetRootFolder ().Unwrap ().GetDirectItems ();
	if (items.IsErr ()) {
		Report (GS::UniString ("Failed to get Keynote Items: ") + items.UnwrapErr ().text.c_str ());
		return;
	}
	if (items.Unwrap ().size () == 0) {
		Report (GS::UniString ("No keynote found under root."));
		return;
	}
	const ACAPI::Keynote::KeynoteItem keynoteItem = items.Unwrap ()[0];

	// Create label
	API_Coord c;
	if (!ClickAPoint ("Click label reference point", &c))
		return;

	API_Element			element = {};
	API_ElementMemo		memo = {};

	element.header.type = API_LabelID;

	GSErrCode err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		Report (GS::UniString ("Error when retrieving default label."));
		return;
	}

	element.label.labelClass	= APILblClass_Text;
	element.label.parent		= APINULLGuid;
	element.label.begC			= c;
	element.label.midC.x		= c.x + 1.0;
	element.label.midC.y		= c.y + 0.5;
	element.label.endC.x		= c.x + 3.0;
	element.label.endC.y		= c.y + 0.5;

	// ! [Get-Autotext-Label-Content-From-Keynote]

	GS::UniString textContent;
	textContent += manager.Unwrap ().GetAutoTextTokenFor (keynoteItem.GetId (), ACAPI::Keynote::KeynoteManager::AutoTextTokenSelector::Key).Unwrap () + "\n";
	textContent += manager.Unwrap ().GetAutoTextTokenFor (keynoteItem.GetId (), ACAPI::Keynote::KeynoteManager::AutoTextTokenSelector::Title).Unwrap () + "\n";
	textContent += manager.Unwrap ().GetAutoTextTokenFor (keynoteItem.GetId (), ACAPI::Keynote::KeynoteManager::AutoTextTokenSelector::Description).Unwrap () + "\n";
	textContent += manager.Unwrap ().GetAutoTextTokenFor (keynoteItem.GetId (), ACAPI::Keynote::KeynoteManager::AutoTextTokenSelector::Reference).Unwrap () + "\n";

	// ! [Get-Autotext-Label-Content-From-Keynote]

	memo.textContent = new GS::UniString { textContent };
	(*memo.paragraphs)[0].range = Strlen32 (textContent.ToCStr ().Get ());
	(*memo.paragraphs)[0].run[0].range = Strlen32 (textContent.ToCStr ().Get ());
	
	ACAPI_CallUndoableCommand ("Place keynote label", [&]() -> GSErrCode {
		err = ACAPI_Element_Create (&element, &memo);
		if (err != NoError)
			Report (GS::UniString ("Error when creating label"));
		return err;
	});

	ACAPI_DisposeElemMemoHdls (&memo);
}

}

// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case KEYNOTE_TEST_MENU_STRINGS: {
			switch (static_cast<MenuItem> (menuParams->menuItemRef.itemIndex)) {
				case MenuItem::DumpKeynoteTree:
					DumpKeynoteTree ();
					return NoError;
				case MenuItem::ModifyFolder:
					ModifyFolder ();
					return NoError;
				case MenuItem::ModifyItem:
					ModifyItem ();
					return NoError;
				case MenuItem::AddFolder:
					AddFolder ();
					return NoError;
				case MenuItem::AddItem:
					AddItem ();
					return NoError;
				case MenuItem::RemoveFolder:
					RemoveFolder ();
					return NoError;
				case MenuItem::RemoveItem:
					RemoveItem ();
					return NoError;
				case MenuItem::PlaceKeynoteLabel:
					PlaceKeynoteLabel ();
					return NoError;
				default:
					GSASSERT_FAILED ("Unhandled menu item index in Keynote_Test's MenuCommandHandler");
					return Error;
			}
			break;
		}
	}

	return NoError;
}

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
	RSGetIndString (&envir->addOnInfo.name,        KEYNOTE_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, KEYNOTE_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface ()
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (KEYNOTE_TEST_MENU_STRINGS, KEYNOTE_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default);
	DBASSERT (err == NoError);
	return err;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize ()
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (KEYNOTE_TEST_MENU_STRINGS, MenuCommandHandler);
	DBASSERT (err == NoError);
	ACAPI_KeepInMemory (true);
	return err;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData ()
{
	return NoError;
}
