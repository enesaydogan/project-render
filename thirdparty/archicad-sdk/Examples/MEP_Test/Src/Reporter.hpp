#include "UniString.hpp"

#include "ACAPI/MEPEnums.hpp"
#include "ACAPI/UniqueID.hpp"

// ARCHICADAPI
#include "APIdefs_Base.h"


class Reporter {
	GS::UniString reportString;
	short tabCount;

	GS::UniString Indent (short tabCount);
public:
	Reporter ();
	Reporter (short tabCount);
	~Reporter ();

	template<class T>
	void Add (const GS::UniString&, const T&, bool closeWithNewLine = true);

	void Add (const GS::UniString&, const GS::UniString&, bool closeWithNewLine = true);

	void Add (const GS::UniString&, double, bool closeWithNewLine = true);

	void Add (const GS::UniString&, Int32, bool closeWithNewLine = true);

	void Add (const GS::UniString&, bool closeWithNewLine = true);

	void Add (const API_Coord3D&, bool closeWithNewLine = true);

	void AddNewLine ();

	void SetTabCount (short tabCount);

	void Write ();
};


GS::UniString ToString (const ACAPI::UniqueID&);
GS::UniString ToString (ACAPI::MEP::ConnectorShape);
GS::UniString ToString (ACAPI::MEP::PreferredTransitionPlacement);
GS::UniString ToString (ACAPI::MEP::Domain);
GS::UniString ToString (ACAPI::MEP::FlowDirection);
GS::UniString ToString (ACAPI::MEP::ConnectionSex);
GS::UniString ToString (ACAPI::MEP::ConnectionStyle);
GS::UniString ToString (const API_Coord3D&);
GS::UniString ToString (const API_AttributeIndex&);
GS::UniString ToString (uint32_t);


template<class T>
void Reporter::Add (const GS::UniString& format, const T& subject, bool closeWithNewLine)
{
	reportString += GS::UniString::Printf (Indent (tabCount) + format + ": %T", ToString (subject).ToPrintf ());
	if (closeWithNewLine) AddNewLine ();
}