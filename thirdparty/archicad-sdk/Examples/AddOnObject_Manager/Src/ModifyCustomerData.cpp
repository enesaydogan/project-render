// *****************************************************************************
// Source code for the AddOnObject Manager Add-On
// *****************************************************************************

#define	_MODIFYCUSTOMERDATA_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"CustomerData.hpp"


bool ModifyAddOnData (IAddOnData& addonData, CloseReason& closeReason)
{
	addonData.GenerateRandomContent ();
	closeReason = CloseReason_Nothing;
	return true;
}


bool IsModifyButtonReservationDependent (void)
{
	return true;
}

