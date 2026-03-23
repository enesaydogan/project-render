// *****************************************************************************
// File:			Property_Test_Helper.hpp
// Description:		Property_Test add-on helper macros and functions
// Project:			APITools/Property_Test
// Namespace:		-
// Contact person:	CSAT
// *****************************************************************************

#if !defined (HELPERS_HPP)
#define	HELPERS_HPP

#include "Property_Test.hpp"
#include "APICommon.h"
#include "DG.h"
#include "StringConversion.hpp"

// -----------------------------------------------------------------------------
// Helper macros
// -----------------------------------------------------------------------------

#if defined (ASSERT)
	#undef ASSERT
#endif
#define ASSERT(expression) PropertyTestHelpers::DebugAssert((expression), #expression, __FILE__, __LINE__, __FUNCTION__)


#if defined (ASSERT_NO_ERROR)
	#undef ASSERT_NO_ERROR
#endif
#define ASSERT_NO_ERROR(expression) PropertyTestHelpers::DebugAssertNoError((expression), #expression, __FILE__, __LINE__, __FUNCTION__)


// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

namespace PropertyTestHelpers
{

API_Guid				RandomGuid ();

GS::UniString			GenearteUniqueName ();

API_PropertyGroup		CreateExamplePropertyGroup ();

GSErrCode				GetCommonExamplePropertyGroup (API_PropertyGroup& group);

API_PropertyDefinition	CreateExampleBoolPropertyDefinition (API_PropertyGroup group);

API_PropertyDefinition	CreateExampleIntPropertyDefinition (API_PropertyGroup group);

API_PropertyDefinition	CreateExampleStringListPropertyDefinition (API_PropertyGroup group);

API_PropertyDefinition	CreateExampleStringMultiEnumPropertyDefinition (API_PropertyGroup group);

API_PropertyDefinition	CreateExampleExpressionPropertyDefinition (API_PropertyGroup group);

GS::Array<API_Guid>		GetSelectedElements (bool assertIfNoSel = true);

void					CallOnSelectedElem (void (*function)(const API_Guid&), bool assertIfNoSel = true);

void					CallOnAllSelectedElems (void (*function)(const API_Guid&), bool assertIfNoSel = true);

void					DebugAssert	(bool success, GS::UniString expression, const char* file, UInt32 line, const char* function);

void					DebugAssertNoError  (GSErrCode error, GS::UniString expression, const char* file, UInt32 line, const char* function);

}

bool operator== (const API_Variant& lhs, const API_Variant& rhs);

bool operator== (const API_SingleVariant& lhs, const API_SingleVariant& rhs);

bool operator== (const API_ListVariant& lhs, const API_ListVariant& rhs);

bool operator== (const API_SingleEnumerationVariant& lhs, const API_SingleEnumerationVariant& rhs);

bool Equals (const API_PropertyDefaultValue& lhs, const API_PropertyDefaultValue& rhs, API_PropertyCollectionType collType);

bool Equals (const API_PropertyValue& lhs, const API_PropertyValue& rhs, API_PropertyCollectionType collType);

bool operator== (const API_PropertyGroup& lhs, const API_PropertyGroup& rhs);

bool operator== (const API_PropertyDefinition& lhs, const API_PropertyDefinition& rhs);

bool operator== (const API_Property& lhs, const API_Property& rhs);

template <typename T>
bool operator!= (const T& lhs, const T& rhs)
{
	return !(lhs == rhs);
}

#endif