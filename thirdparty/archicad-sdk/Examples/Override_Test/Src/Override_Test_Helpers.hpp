// *****************************************************************************
// File:			Override_Test_Helper.hpp
// Description:		Override_Test add-on helper macros and functions
// Project:			APITools/Override_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

#ifndef HELPERS_HPP
#define	HELPERS_HPP

#include "Override_Test.hpp"

// -----------------------------------------------------------------------------
// Helper macros
// -----------------------------------------------------------------------------

#if defined (ASSERT)
	#undef ASSERT
#endif
#define ASSERT(expression) OverrideTestHelpers::DebugAssert((expression), #expression, __FILE__, __LINE__, __FUNCTION__)


#if defined (ASSERT_NO_ERROR)
	#undef ASSERT_NO_ERROR
#endif
#define ASSERT_NO_ERROR(expression) OverrideTestHelpers::DebugAssertNoError((expression), #expression, __FILE__, __LINE__, __FUNCTION__)


// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

namespace OverrideTestHelpers
{

API_Guid		RandomGuid ();

void			DebugAssert	(bool success, GS::UniString expression, const char* file, UInt32 line, const char* function);

void			DebugAssertNoError (GSErrCode error, GS::UniString expression, const char* file, UInt32 line, const char* function);

}

bool operator==(const API_OverriddenFillType& a, const API_OverriddenFillType& b);

bool operator==(const API_OverriddenAttribute& a, const API_OverriddenAttribute& b);

bool operator==(const API_OverrideRuleStyle& a, const API_OverrideRuleStyle& b);

bool operator==(const API_OverrideRule& a, const API_OverrideRule& b);

bool operator==(const API_OverrideCombination& a, const API_OverrideCombination& b);

bool operator==(const API_OverriddenSurfaceType& a, const API_OverriddenSurfaceType& b);


#endif