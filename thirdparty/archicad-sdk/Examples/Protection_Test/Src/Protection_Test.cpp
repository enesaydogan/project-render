// *****************************************************************************
// Source code for the Protection Test Add-On
// *****************************************************************************

// ---------------------------------- Includes ---------------------------------

#include "APIEnvir.h"

#include "GSPref.hpp"
#include "DGModule.hpp"
#include "ACAPinc.h"

#include "ProtectionTestResIDs.hpp"

#include <set>
#include <string>
#include "ACAPI/GSID.hpp"

// ! [Check-GSID-Authorization-Example]
static std::set<GS::UniString> GetAuthorizedUsers (void); // { ... }
static std::set<GS::UniString> GetAuthorizedOrganizations (void); // { ... }

bool IsUserAuthorizedToUseAddOn (void)
{
	auto gsid = ACAPI::CreateGSIDObject ();
	if (gsid.IsErr ())
		return false;

	auto userId = gsid->GetUserId ();
	auto userOrgs = gsid->GetOrganizationIds ();
	auto authorizedUsers = GetAuthorizedUsers ();
	auto authorizedOrgs = GetAuthorizedOrganizations ();

	if (userId.IsOk ())
		if (authorizedUsers.find (*userId) != authorizedUsers.end ())
			return true;

	if (userOrgs.IsOk ())
		for (const auto& userOrg : *userOrgs)
			if (authorizedOrgs.find (userOrg) != authorizedOrgs.end ())
				return true;

	// User not entitled to use Add-On (as e.g. they do not possess a license for your Add-On)
	return false;
}
// ! [Check-GSID-Authorization-Example]

// Return current user and their orgs as those "authorized" to use this test add-on
static std::set<GS::UniString> GetAuthorizedUsers (void)
{
	auto gsid = ACAPI::CreateGSIDObject ();
	if (gsid.IsErr ())
		return {};
	
	auto userId = gsid->GetUserId ();
	if (userId.IsErr ())
		return {};

	return { *userId };
}
static std::set<GS::UniString> GetAuthorizedOrganizations (void)
{
	auto gsid = ACAPI::CreateGSIDObject ();
	if (gsid.IsErr ())
		return {};

	auto orgIds = gsid->GetOrganizationIds ();
	if (orgIds.IsErr ())
		return {};

	return *orgIds;
}

// =============================================================================
//
// Main functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Command that requires the current license to have a Forward subscription
// -----------------------------------------------------------------------------

using CommandFunction = bool ();

static void DisplayCommandSuccesfulAlert ()
{
	DG::Alert (ACAPI_GetOwnResModule (), PROTECTIONTEST_ALERT_COMMAND_OK_RESID);
}


static void DisplayCommandFailedAlert ()
{
	DG::Alert (ACAPI_GetOwnResModule (), PROTECTIONTEST_ALERT_COMMAND_ERROR_RESID);
}


static void DoCommandChecked (CommandFunction* command)
{
	if (command ()) {
		DisplayCommandSuccesfulAlert ();
	} else {
		DisplayCommandFailedAlert ();
	}
}


static bool	Command_ForwardOnly (void)
{
	return ACAPI_Licensing_IsSSALevelAtLeast (API_SSALevels::Forward);
}


static bool	Command_SpecificPartnerOnly (void)
{
	return ACAPI_Licensing_GetPartnerId () == "GRAPHISOFT SE";
}


static void	Command_DisplayProtectionInfo (void)
{
	const GS::UniString isNetworkLicense = ACAPI_Licensing_GetProtectionMode () & APIPROT_NET_MASK ? "Yes" : "No";
	const GS::UniString serialNumber = GS::ValueToUniString (ACAPI_Licensing_GetSerialNumber ());
	const GS::UniString isUserAuthorized = IsUserAuthorizedToUseAddOn () ? "Yes" : "No";

	DG::Alert (ACAPI_GetOwnResModule (), PROTECTIONTEST_ALERT_PROTECTION_INFO_RESID, isNetworkLicense, serialNumber, isUserAuthorized);
}


// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case PROTECTIONTEST_MENU_STRINGSID:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					DoCommandChecked (Command_ForwardOnly);
					
					break;

				case 2:
					DoCommandChecked (Command_SpecificPartnerOnly);

					break;

				case 3:
					Command_DisplayProtectionInfo ();

					break;
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

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------


API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, PROTECTIONTEST_ABOUT_STRINGSID, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, PROTECTIONTEST_ABOUT_STRINGSID, 2, ACAPI_GetOwnResModule ());

	// This test Add-On has some functionality that requires a Forward subscription (see Command_ForwardOnly)
	// As such, we should declare this dependency in the environment structure
	envir->addOnInfo.requiredSSALevel = API_SSALevels::Forward;

	// Refuse to even be loaded if the program runs in demo mode. 
	// This could be based on any protection property, not just demo mode
	if (ACAPI_Licensing_GetProtectionMode () & APIPROT_DEMO_MASK)
		return APIAddon_DontRegister;

	return APIAddon_Normal;
}		// CheckEnvironment

// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (PROTECTIONTEST_MENU_STRINGSID, PROTECTIONTEST_PROMPT_STRINGSID, MenuCode_UserDef, MenuFlag_Default);
	if (err != NoError)
		DBPrintf ("Protection_Test:: RegisterInterface() ACAPI_MenuItem_RegisterMenu failed\n");

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (PROTECTIONTEST_MENU_STRINGSID, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Protection_Test:: Initialize () ACAPI_MenuItem_InstallMenuHandler failed\n");

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
