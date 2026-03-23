// *****************************************************************************
// File:			Classification_Test_Helper.hpp
// Description:		Classification_Test add-on helper macros and functions
// Project:			APITools/Classification_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

#if !defined (HELPERS_HPP)
#define	HELPERS_HPP

#include "Classification_Test.hpp"

// -----------------------------------------------------------------------------
// Helper macros
// -----------------------------------------------------------------------------

#if defined (ASSERT)
	#undef ASSERT
#endif
#define ASSERT(expression) ClassificationTestHelpers::DebugAssert((expression), #expression, __FILE__, __LINE__, __FUNCTION__)


#if defined (ASSERT_NO_ERROR)
	#undef ASSERT_NO_ERROR
#endif
#define ASSERT_NO_ERROR(expression) ClassificationTestHelpers::DebugAssertNoError((expression), #expression, __FILE__, __LINE__, __FUNCTION__)


// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

namespace ClassificationTestHelpers
{

API_Guid				RandomGuid ();

GS::UniString			GenearteUniqueName ();

void					CallOnSelectedElem (void (*function)(const API_Guid&), bool assertIfNoSel = true, bool onlyEditable = true);

void					DebugAssert	(bool success, GS::UniString expression, const char* file, UInt32 line, const char* function);

void					DebugAssertNoError  (GSErrCode error, GS::UniString expression, const char* file, UInt32 line, const char* function);

GSDateRecord			GetCurrentDate ();

}

bool operator== (const API_ClassificationItem& lhs, const API_ClassificationItem& rhs);

bool operator== (const API_ClassificationSystem& lhs, const API_ClassificationSystem& rhs);

template <typename T>
bool operator!= (const T& lhs, const T& rhs)
{
	return !(lhs == rhs);
}

#endif