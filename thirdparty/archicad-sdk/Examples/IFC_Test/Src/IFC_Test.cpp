#include "APIEnvir.h"
#include "ACAPinc.h"

#include "Resources.hpp"

#include "IFCAPI_Test.hpp"


enum APITestMenu {
	GetAssignmentTreeMenuItem = 1,
	GetContainingGroupsOfSelectionMenuItem,
	GetStoriesMenuItem,
	GetAllObjectsLocalPropertiesMenuItem,
	GetAllObjectsPreviewPropertiesMenuItem,
	FindElementsByIFCGlobalIdMenuItem
};


GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case IFCAPI_TEST_MENU_STRINGS:
			{
				switch (menuParams->menuItemRef.itemIndex) {
					case GetAssignmentTreeMenuItem: GetAssignmentTree (); break;
					case GetContainingGroupsOfSelectionMenuItem: GetContainingGroupsOfSelection (); break;
					case GetStoriesMenuItem: GetStories (); break;
					case GetAllObjectsLocalPropertiesMenuItem: GetAllObjectsLocalProperties (); break;
					case GetAllObjectsPreviewPropertiesMenuItem: GetAllObjectsPreviewProperties (); break;
					case FindElementsByIFCGlobalIdMenuItem: FindElementsByIFCGlobalId (); break;
				}
				break;
			}
		default:
			DBBREAK_STR ("Unhandled menu item!"); break;
	}

	ACAPI_KeepInMemory (true);

	return NoError;
}


API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	if (envir->serverInfo.serverApplication != APIAppl_ArchiCADID)
		return APIAddon_DontRegister;

	RSGetIndString (&envir->addOnInfo.name, IFC_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, IFC_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (IFCAPI_TEST_MENU_STRINGS, IFCAPI_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_InsertIntoSame);

	DBASSERT (err == NoError);

	return err;
}


GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (IFCAPI_TEST_MENU_STRINGS, MenuCommandHandler);

	DBASSERT (err == NoError);

	return err;
}


GSErrCode FreeData (void)
{
	return NoError;
}
