// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn LineTypeIndexes class header
//					for managing the indexes of Line type elements
// *****************************************************************************

#ifndef LINETYPEINDEXES_HPP
#define LINETYPEINDEXES_HPP

#include "APIdefs.h"

#pragma once

#include "APIdefs_AttributeIndex.hpp"

class LineTypeIndexes
{
	API_AttributeIndex basicLineType;
	API_AttributeIndex hiddenLineType;

	void GetPreferences ();

	static LineTypeIndexes& GetNotConstInstance ();

	LineTypeIndexes ();

public:
	static const LineTypeIndexes& GetInstance ();

	const API_AttributeIndex& GetBasicLineType () const { return basicLineType; }
	const API_AttributeIndex& GetHiddenLineType () const { return hiddenLineType; }

	static void SetLineTypes (const API_AttributeIndex& basicLineType, const API_AttributeIndex& hiddenLineType);
};

#endif