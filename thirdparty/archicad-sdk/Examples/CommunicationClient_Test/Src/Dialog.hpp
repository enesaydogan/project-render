#if !defined DIALOG_HPP
#define DIALOG_HPP

#pragma once

// from GSRoot
#include "UniString.hpp"

class Dialog
{
private:
	GS::UniString			dialogButtonText;
	GS::UniString			dialogTitle;

public:
	Dialog (const GS::UniString& dialogTitle, const GS::UniString& dialogButtonText);

	void					ShowDialog (short alertType, const GS::UniString& largeText, const GS::UniString& smallText);
};

#endif
