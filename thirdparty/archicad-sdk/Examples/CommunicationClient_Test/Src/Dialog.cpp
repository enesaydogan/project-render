#include "Dialog.hpp"

// from DG
#include "DG.h"


Dialog::Dialog (const GS::UniString& dialogTitle, const GS::UniString& dialogButtonText) :
	dialogTitle (dialogTitle),
	dialogButtonText (dialogButtonText)
{
}


void Dialog::ShowDialog (short alertType, const GS::UniString& largeText, const GS::UniString& smallText)
{
	DGAlert (alertType, dialogTitle, largeText, smallText, dialogButtonText);
}
