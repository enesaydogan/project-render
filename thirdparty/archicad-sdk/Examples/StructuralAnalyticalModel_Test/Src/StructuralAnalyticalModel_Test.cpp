// *****************************************************************************
// Contact person:	HR
// *****************************************************************************

#include "StructuralAnalyticalModel_Test.hpp"
#include "Resources.hpp"

// from DG
#include "DG.h"

// from GSRoot
#include "StringConversion.hpp"

// from API
#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ACAPI_Analytical_Adapter.hpp"


static GS::Array<API_AnalyticalCurveMember> GetCurveMembers () 
{
	GS::Array<API_AnalyticalCurveMember> api_curveMembers = {
		{																							// Column
			API_AnalyticalMemberType::Column,														// type
			GSGuid2APIGuid (GS::Guid ("{0D220634-5B83-467F-A603-EE6F357CBB93}")),					// guid
			{ API_CrossSection_Rectangle, 0.3, 0.4, 0.0,
			 APIInvalidAttributeIndex, { ACAPI_CreateAttributeIndex (1) }},						// crossSection
			{																						// curve
				API_AnalyticalCurve_LineType,														// type
				{ 1, { 0, 0, 0 } },																	// begNode ( nodeId, coord )
				{ 2, { 0, 0, 2 } },																	// endNode ( nodeId, coord )
			},
			{},																						// internalNodes
			0.1,																					// rotationAngle
			0.2,																					// ey
			{ 0, 1, 0 },																			// eyVector
			0.3,																					// ez
			{ 0, 0, 0 },																			// ezVector
		},

		{																							// Beam
			API_AnalyticalMemberType::Beam,															// type
			GSGuid2APIGuid (GS::Guid ("{FD22A9BF-9A42-4EE3-BC0E-54E09A831F69}")),					// guid
			{ API_CrossSection_Circle, 0.5, 0.5, 0.0,
			APIInvalidAttributeIndex, { ACAPI_CreateAttributeIndex (2) } },						// crossSection
			{																						// curve
				API_AnalyticalCurve_LineType,														// type
				{ 1, { 2, 2, 2 } },																	// begNode ( nodeId, coord )
				{ 2, { 4, 4, 2 } },																	// endNode ( nodeId, coord )
			},
			{},																						// internalNodes
			0.0,																					// rotationAngle
			0.0,																					// ey
			{ 0, 1, 0 },																			// eyVector
			0.0,																					// ez
			{ 0, 0, 0 },																			// ezVector
		},

		{																							// Column
			API_AnalyticalMemberType::Column,														// type
			GSGuid2APIGuid (GS::Guid ("{B03DFE5A-3793-4971-BD52-948B55AF6BB2}")),					// guid
			{ API_CrossSection_Rectangle, 0.3, 0.4, 0.0,
			APIInvalidAttributeIndex, { ACAPI_CreateAttributeIndex (1) } },						// crossSection
			{																						// curve
				API_AnalyticalCurve_LineType,														// type
				{ 1, { -1, -1, 0 } },																// begNode ( nodeId, coord )
				{ 2, { -2, -2, 2 } },																// endNode ( nodeId, coord )
			},
			{},																						// internalNodes
			0.1,																					// rotationAngle
			0.2,																					// ey
			{ 0, 1, 0 },																			// eyVector
			0.3,																					// ez
			{ 0, 0, 0 },																			// ezVector
		},

		{																							// Beam
			API_AnalyticalMemberType::Beam,															// type
			GSGuid2APIGuid (GS::Guid ("{79FBDEA0-9888-4BF6-9A64-FA4CB88921D5}")),					// guid
			{ API_CrossSection_Circle, 0.5, 0.5, 0.0,
			APIInvalidAttributeIndex, { ACAPI_CreateAttributeIndex (2) } },						// crossSection
			{																						// curve
				API_AnalyticalCurve_LineType,														// type
				{ 1, { 4, 2, 2 } },																	// begNode ( nodeId, coord )
				{ 2, { 2, 4, 2 } },																	// endNode ( nodeId, coord )
			},
			{},																						// internalNodes
			0.0,																					// rotationAngle
			0.0,																					// ey
			{ 0, 1, 0 },																			// eyVector
			0.0,																					// ez
			{ 0, 0, 0 },																			// ezVector
		},
	};

	return api_curveMembers;
}


static GS::Array<API_AnalyticalSurfaceMember> GetSurfaceMembers ()
{
	GS::Array<API_AnalyticalSurfaceMember> surfaceMembers = {
		{																			// Wall
			API_AnalyticalMemberType::Wall,											// type
			GSGuid2APIGuid (GS::Guid ("{E3E169EA-48F9-4822-9A0D-42423E4E5D6A}")),	// guid
			{																		//surface
				API_AnalyticalSurface_PlaneType,									// surfaceType
				{ { 1, -1, 0}, { 3, 0, 0} },										// planeSurface
				{ }																	// cylinderSurface
			},
			{															// contour
				{
					API_AnalyticalCurve_LineType,						// type
					{ 1, { 3, 0, 0 } },									// begNode ( nodeId, coord )
					{ 2, { 3, 0, 3 } },									// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,						// type
					{ 2, { 3, 0, 3 } },									// begNode ( nodeId, coord )
					{ 3, { 6, 0, 3 } },									// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,						// type
					{ 3, { 6, 0, 3 } },									// begNode ( nodeId, coord )
					{ 4, { 6, 0, 0 } },									// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,						// type
					{ 4, { 6, 0, 0 } },									// begNode ( nodeId, coord )
					{ 1, { 3, 0, 0 } },									// endNode ( nodeId, coord )
				},
			},
			{},														// holeContours
			{},														// internalNodes
			0.2,													// thickness
			{ 1, 0, 0 },											// lcsXVector
			0.1,													// ez
			{ 0, 0, 0 },											// ezVector
			ACAPI_CreateAttributeIndex (1),							// material idx
		},
		{																			// Slab
			API_AnalyticalMemberType::Slab,											// type
			GSGuid2APIGuid (GS::Guid ("{951C871D-C866-4F4B-A8B5-112F6D8919BA}")),	// guid
			{																		//surface
				API_AnalyticalSurface_PlaneType,									// surfaceType
				{ { -3, 0, 0 }, { 0, 0, 1 } },										// planeSurface
				{ }																	// cylinderSurface
			},
			{														// contour
				{
					API_AnalyticalCurve_LineType,					// type
					{ 1, { -3, 0, 0 } },							// begNode ( nodeId, coord )
					{ 2, { -6, 0, 0 } },							// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,					// type
					{ 2, { -6, 0, 0 } },							// begNode ( nodeId, coord )
					{ 3, { -6, 4, 0 } },							// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,					// type
					{ 3, { -6, 4, 0 } },							// begNode ( nodeId, coord )
					{ 4, { -3, 4, 0 } },							// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,					// type
					{ 4, { -3, 4, 0 } },							// begNode ( nodeId, coord )
					{ 1, { -3, 0, 0 } },							// endNode ( nodeId, coord )
				},
			},
			{},														// holeContours
			{},														// internalNodes
			0.3,													// thickness
			{ 1, 0, 0 },											// lcsXVector
			0.2,													// ez
			{ 0, 0, 0 },											// ezVector
			ACAPI_CreateAttributeIndex (1),							// material idx
		},
		{																			// Roof
			API_AnalyticalMemberType::Roof,											// type
			GSGuid2APIGuid (GS::Guid ("{100A8DD4-546A-4010-AFEA-CEA153929AA3}")),	// guid
			{																		//surface
				API_AnalyticalSurface_PlaneType,									// surfaceType
				{ { 0, -4, 0 }, { -1, 0, 1 } },										// planeSurface
				{ }																	// cylinderSurface
			},
			{														// contour
				{
					API_AnalyticalCurve_LineType,					// type
					{ 1, { 0, -4, 0 } },							// begNode ( nodeId, coord )
					{ 2, { 0, -8, 0 } },							// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,					// type
					{ 2, { 0, -8, 0 } },							// begNode ( nodeId, coord )
					{ 3, { 4, -8, 5 } },							// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,					// type
					{ 3, { 4, -8, 5 } },							// begNode ( nodeId, coord )
					{ 4, { 4, -4, 5 } },							// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,					// type
					{ 4, { 4, -4, 5 } },							// begNode ( nodeId, coord )
					{ 2, { 0, -4, 0 } },							// endNode ( nodeId, coord )
				},
			},
			{},														// holeContours
			{},														// internalNodes
			0.4,													// thickness
			{ 0.62469504755442418, 0.0, 0.78086880944303039 },		// lcsXVector
			0.3,													// ez
			{ 0, 0, 0 },											// ezVector
			ACAPI_CreateAttributeIndex (3),							// material idx
		}
	};

	return surfaceMembers;
}

static API_AnalyticalReleaseType GetAnalyticalReleaseType (const API_AnalyticalMemberPartRef& analyticalMemberPartRef)
{
	API_AnalyticalReleaseType release = {
		GSGuid2APIGuid (GS::Guid::GenerateGuid),									// guid
		{																			// releaseDescriptions
			{
				API_FreeType, 0, API_RigidType, 0, API_CustomType, 0.1,					// ux, uy, uz
				API_CustomType, 0.2, API_FreeType, 0, API_RigidType, 0,					// fix, fiy, fiz
				analyticalMemberPartRef,											// amPartRef
			}
		},
	};

	return release;
}


static API_AnalyticalLinkType GetAnalytical0DLink (const API_Guid& elem1Guid, const API_Guid& elem2Guid)
{
	API_AnalyticalMemberPartRef firstElemPartRef = {};
	firstElemPartRef.elemGuid = elem1Guid;
	firstElemPartRef.partRefType = API_AMPartRef_Edge;

	API_AnalyticalMemberPartRef secondElemPartRef = {};
	secondElemPartRef.elemGuid = elem2Guid;
	secondElemPartRef.partRefType = API_AMPartRef_Edge;

	return {
			{																						// linkSettings
				API_Link1D,																				// geometry
				API_Spring,																				// type
				{ API_HingeConnection, API_HingeConnection },											// rigidSettings
				{																						// springSettings
					API_Global,																				// direction
					{ API_FreeType, 0, API_RigidType, 0, API_CustomType, 0.1, true, true, true, 0.1, 0.2, 0.3 },		// translation
					{ API_CustomType, 0.1, API_FreeType, 0, API_RigidType, 0, true, true, true, 0.4, 0.5, 0.6 },		// rotation
				},
			},
			firstElemPartRef,				// startElementPartRef
			secondElemPartRef,					// endElementPartRef
	};
}


static API_AnalyticalLinkType GetAnalytical1DLink (const API_AnalyticalMemberPartRef& firstElemPartRef, const API_AnalyticalMemberPartRef& secondElemPartRef)
{
	return {
			{																						// linkSettings
				API_Link1D,																				// geometry
				API_Spring,																				// type
				{ API_HingeConnection, API_HingeConnection },											// rigidSettings
				{																						// springSettings
					API_Local,																				// direction
					{ API_CustomType, 0.2, API_FreeType, 0, API_RigidType, 0, true, true, true, 0.2, 0.3, 0.4 },		// translation
					{ API_RigidType, 0, API_CustomType, 0.2, API_FreeType, 0, true, true, true, 0.5, 0.6, 0.7 },		// rotation
				},
			},
			firstElemPartRef,				// startElementPartRef
			secondElemPartRef,				// endElementPartRef
	};
}


static API_AnalyticalLinkType GetAnalytical2DLink (const API_AnalyticalMemberPartRef& firstElemPartRef, const API_AnalyticalMemberPartRef& secondElemPartRef)
{
	return {
			{																						// linkSettings
				API_Link2D,																				// geometry
				API_Spring,																				// type
				{ API_HingeConnection, API_HingeConnection },											// rigidSettings
				{																						// springSettings
					API_Global,																				// direction
					{ API_RigidType, 0, API_CustomType, 0.3, API_FreeType, 0, true, true, true, 0.3, 0.4, 0.5 },		// translation
					{ API_FreeType, 0, API_RigidType, 0, API_CustomType, 0.3, true, true, true, 0.6, 0.7, 0.8 },		// rotation
				},
			},
			firstElemPartRef,					// startElementPartRef
			secondElemPartRef,					// endElementPartRef
	};
}


static API_AnalyticalSupportType GetAnalyticalSupport (const API_AnalyticalMemberPartRef& elemPartRef)
{
	return {
			{																		// supportSettings
				API_SimplePoint,														// supportType
				{																		// supportValues
					API_FreeType, 0, API_RigidType, 0, API_CustomType, 0.1,
					API_CustomType, 0.2, API_FreeType, 0, API_RigidType, 0,
				},
				API_Global,																// coordinateSystem
			},
			elemPartRef,	// supportedMemberPartRef
	};
}


static API_AnalyticalLoadGroup GetAnalyticalLoadGroup (const GS::UniString& name)
{
	return {
		name,																			// LoadGroup Name
		API_AnalyticalLoadGroupType::API_AnalyticalLoadGroup_PermanentLoad,				// LoadGroupType
		API_AnalyticalLoadGroupRelation::API_AnalyticalLoadGroup_StandardRelation,		// Relation
		"LoadType",																		// LoadType
		GSGuid2APIGuid (GS::Guid (GS::Guid::GenerateGuid)),								// guid
	};
}


static API_AnalyticalLoadCase GetAnalyticalLoadCase (const GS::UniString& name, const API_Guid& loadGroupId)
{
	return {
		name,																			// LoadCase Name
		"Description",																	// Description
		API_AnalyticalLoadCaseActionType::API_AnalyticalLoadCase_VariableAction,		// ActionType
		loadGroupId,																	// LoadGroup guid
		API_AnalyticalLoadCaseLoadType::API_AnalyticalLoadCase_StandardLoad,			// LoadType
		API_AnalyticalLoadCaseDuration::API_AnalyticalLoadCase_LongDuration,			// Duration
		GSGuid2APIGuid (GS::Guid (GS::Guid::GenerateGuid)),								// guid
	};
}


static API_AnalyticalLoadCombination GetAnalyticalLoadCombination (const GS::UniString& name, 
																   const API_Guid& loadCaseId1, 
																   const API_Guid& loadCaseId2, 
																   API_AnalyticalLoadCombinationCategory category,
																   GS::Optional<API_AnalyticalLoadCombinationNationalStandard> nationalStandard,
																   GS::Optional<API_LoadCombinationType> type,
																   double multiplier1 = 1.0,															   
																   double multiplier2 = 1.0,															   
																   GS::Optional<double> factor1 = GS::NoValue,
																   GS::Optional<double> factor2 = GS::NoValue)
{
	DBVERIFY ((category == API_AnalyticalLoadCombination_ANS) == nationalStandard.HasValue ());
	DBVERIFY ((category != API_AnalyticalLoadCombination_ANS) == factor1.HasValue ());
	DBVERIFY ((category != API_AnalyticalLoadCombination_ANS) == factor2.HasValue ());
	if (type.HasValue ()) {
		DBVERIFY (category != API_AnalyticalLoadCombination_ANS && category != API_AnalyticalLoadCombination_ALS);
	}

	return {
		name,																// LoadCombination Name
		"Description",														// Description
		category,															// Category
		nationalStandard,													// National Standard
		type,																// Type
		{																	// Load Case Factors
			{ loadCaseId1, factor1, multiplier1},
			{ loadCaseId2, factor2, multiplier2}
		},																	
		GSGuid2APIGuid (GS::Guid (GS::Guid::GenerateGuid))					// Guid
	};
}


static GSErrCode  CreateLoadGroupsAndCasesAndCombinations ()
{
	API_AnalyticalLoadGroup loadGroup1 = GetAnalyticalLoadGroup ("LG1");

	GSErrCode err = ACAPI_Analytical_CreateAnalyticalLoadGroup (loadGroup1);
	if (err != NoError)
		return err;

	API_AnalyticalLoadGroup loadGroup2 = GetAnalyticalLoadGroup ("LG2");

	err = ACAPI_Analytical_CreateAnalyticalLoadGroup (loadGroup2);
	if (err != NoError)
		return err;

	API_AnalyticalLoadCase loadCase1 = GetAnalyticalLoadCase ("LC1", loadGroup1.guid);
	API_AnalyticalLoadCase loadCase2 = GetAnalyticalLoadCase ("LC2", loadGroup1.guid);
	API_AnalyticalLoadCase loadCase3 = GetAnalyticalLoadCase ("LC3", loadGroup1.guid);

	err = ACAPI_Analytical_CreateAnalyticalLoadCase (loadCase1);
	if (err != NoError)
		return err;

	err = ACAPI_Analytical_CreateAnalyticalLoadCase (loadCase2);
	if (err != NoError)
		return err;

	err = ACAPI_Analytical_CreateAnalyticalLoadCase (loadCase3);
	if (err != NoError)
		return err;

	API_AnalyticalLoadCase loadCase4 = GetAnalyticalLoadCase ("LC4", loadGroup2.guid);
	err = ACAPI_Analytical_CreateAnalyticalLoadCase (loadCase4);
	if (err != NoError)
		return err;

	API_AnalyticalLoadCombination loadCombination1 = GetAnalyticalLoadCombination ("COM1", loadCase1.loadCaseGuid, loadCase2.loadCaseGuid, 
																				    API_AnalyticalLoadCombination_ANS, 
																					API_AnalyticalLoadNationalStandard_IBCASDseismic,
																					GS::NoValue);

	err = ACAPI_Analytical_CreateAnalyticalLoadCombination (loadCombination1);
	if (err != NoError)
		return err;

	API_AnalyticalLoadCombination loadCombination2 = GetAnalyticalLoadCombination ("COM2", loadCase1.loadCaseGuid, loadCase4.loadCaseGuid,
																					API_AnalyticalLoadCombination_NotDefined,
																					GS::NoValue, GS::NoValue, 0.2, 2.1, 0.4, 0.7);

	err = ACAPI_Analytical_CreateAnalyticalLoadCombination (loadCombination2);
	if (err != NoError)
		return err;

	API_AnalyticalLoadFactor loadFactor1 { loadCase3.loadCaseGuid, GS::NoValue, 0.8 };
	API_AnalyticalLoadFactor loadFactor2 { loadCase3.loadCaseGuid, 1.0, 0.0 };

	err = ACAPI_Analytical_AddLoadCaseToLoadCombination (loadFactor1, loadCombination1.guid);
	if (err != NoError)
		return err;

	err = ACAPI_Analytical_AddLoadCaseToLoadCombination (loadFactor2, loadCombination2.guid);
	if (err != NoError)
		return err;

	return err;
}


static API_AnalyticalPointLoadType GetAnalyticalPointLoad (const API_AnalyticalMemberPartRef& elemPartRef, const API_Guid& loadCaseId)
{
	return {
		{														// LoadSettings
			loadCaseId,
			API_AnalyticalLoad_LoadForceType::API_Force,
			API_AnalyticalCoordSysDirection::API_Global,
			1.2, 3.4, 5.6,
		},
		elemPartRef,											// loadedMemberPartRef
	};
}


static API_AnalyticalEdgeLoadType GetAnalyticalEdgeLoad (const API_AnalyticalMemberPartRef& elemPartRef, const API_Guid& loadCaseId)
{
	return {
		{														// LoadSettings
			loadCaseId,
			API_AnalyticalLoad_LoadForceType::API_Force,
			API_AnalyticalLoad_LoadDistributionType::API_Uniform,
			API_AnalyticalCoordSysDirection::API_Global,
			{ 1.2, 3.4, 5.6 }, { 1.0, 0.0, 0.0 }
		},
		elemPartRef,											// loadedMemberPartRef
	};
}


static API_AnalyticalSurfaceLoadType GetAnalyticalSurfaceLoad (const API_AnalyticalMemberPartRef& elemPartRef, const API_Guid& loadCaseId)
{
	return {
		{														// LoadSettings
			loadCaseId,
			API_AnalyticalCoordSysDirection::API_Global,
			1.2, 3.4, 5.6
		},
		elemPartRef,											// loadedMemberPartRef
	};
}


static GS::Array<API_AnalyticalCurve> GetSurfaceRegion () 
{
	return {
				{
					API_AnalyticalCurve_LineType,						// type
					{ 1, { 4, 0, 1 } },									// begNode ( nodeId, coord )
					{ 2, { 4, 0, 2 } },									// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,						// type
					{ 2, { 4, 0, 2 } },									// begNode ( nodeId, coord )
					{ 3, { 5, 0, 2 } },									// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,						// type
					{ 3, { 5, 0, 2 } },									// begNode ( nodeId, coord )
					{ 4, { 5, 0, 1 } },									// endNode ( nodeId, coord )
				},
				{
					API_AnalyticalCurve_LineType,						// type
					{ 4, { 5, 0, 1 } },									// begNode ( nodeId, coord )
					{ 1, { 4, 0, 1 } },									// endNode ( nodeId, coord )
				}, 
		   };
}


static GS::Array<API_AnalyticalCurve> GetSurfaceEdges ()
{
	return	{
			{
				API_AnalyticalCurve_LineType,						// type
				{ 1, { 4, 0, 1 } },									// begNode ( nodeId, coord )
				{ 2, { 5, 0, 2 } },									// endNode ( nodeId, coord )
			},
			{
				API_AnalyticalCurve_LineType,						// type
				{ 1, { 4, 0, 2 } },									// begNode ( nodeId, coord )
				{ 2, { 5, 0, 1 } },									// endNode ( nodeId, coord )
			},
			{
				API_AnalyticalCurve_LineType,						// type
				{ 1, { 4, 0, 1 } },									// begNode ( nodeId, coord )
				{ 2, { 13, 0, 1 } },									// endNode ( nodeId, coord )
			},
			{
				API_AnalyticalCurve_LineType,						// type
				{ 1, { 4, 0, 1 } },									// begNode ( nodeId, coord )
				{ 2, { -5, 0, 1 } },								// endNode ( nodeId, coord )
			}
	};
}



static GSErrCode CreateCurveMembers () 
{
	GSErrCode err = NoError;

	GS::Array<API_AnalyticalCurveMember> curveMembers = GetCurveMembers ();

	// Curve Member Creation
	for (API_AnalyticalCurveMember& item : curveMembers) {
		err = ACAPI_Analytical_CreateElementFromCurveMember (item, GS::EmptyUniString);
		if (err != NoError)
			return err;
	}

	// Set releases for surface members
	for (const auto& curveMember : curveMembers) {

		API_AnalyticalMemberPartRef memberPartRef = {};
		memberPartRef.elemGuid = curveMember.elemGuid;
		memberPartRef.partRefType = API_AMPartRef_BegVertex;
		memberPartRef.partId.edgeOrVertexIdx = 0;
		memberPartRef.partId.contourIdx = 0;

		API_AnalyticalReleaseType releaseType = GetAnalyticalReleaseType (memberPartRef);

		memberPartRef.partRefType = API_AMPartRef_EndVertex;
		releaseType.releaseDescriptions.Push (GetAnalyticalReleaseType (memberPartRef).releaseDescriptions.GetFirst ());

		err = ACAPI_Analytical_SetAnalyticalRelease (curveMember.elemGuid, releaseType);
		if (err != NoError)
			return err;
	}

	return err;
}


static GSErrCode CreateSurfaceMembers (API_ImportIdMappingType&	idMapping)
{
	GSErrCode err = NoError;
	GS::Array<API_AnalyticalSurfaceMember> surfaceMembers = GetSurfaceMembers ();
	
	for (API_AnalyticalSurfaceMember& surfaceMember : surfaceMembers) {
		err = ACAPI_Analytical_CreateElementFromSurfaceMember (surfaceMember, "", &idMapping);
		if (err != NoError)
			return err;
	}

	for (const auto& edgeMappingEntry : idMapping.edges) {
		API_AnalyticalReleaseType releaseType = GetAnalyticalReleaseType (edgeMappingEntry.value);
		err = ACAPI_Analytical_SetAnalyticalRelease (edgeMappingEntry.key.memberGuid, releaseType);
		if (err != NoError)
			return err;
	}

	return err;
}


static GSErrCode CreateAnalyticalLinks (const API_ImportIdMappingType& idMapping)
{
	GSErrCode err = NoError;

	GS::Array<API_AnalyticalCurveMember> curveMembers = GetCurveMembers ();
	GS::Array<API_AnalyticalSurfaceMember> surfaceMembers = GetSurfaceMembers ();

	// Create 0D analytical link
	API_Element link0DElement = {};
	API_AnalyticalLinkType analytical0DLink = GetAnalytical0DLink (curveMembers[1].elemGuid, curveMembers[3].elemGuid);

	ACAPI_Analytical_SetElementFromLink (analytical0DLink, link0DElement);
	link0DElement.header.type = API_AnalyticalLinkID;
	link0DElement.header.layer = APIApplicationLayerAttributeIndex;
	link0DElement.header.renovationStatus = API_ExistingStatus;

	err = ACAPI_Element_Create (&link0DElement, nullptr);
	if (err != NoError)
		return err;

	// Create 1D analytical link
	API_AnalyticalMemberPartRef beamPartRef = {};
	beamPartRef.elemGuid = curveMembers[0].elemGuid;
	beamPartRef.partRefType = API_AMPartRef_BegVertex;
	beamPartRef.partId.edgeOrVertexIdx = 0;
	beamPartRef.partId.contourIdx = 0;

	API_AnalyticalMemberPartRef columnPartRef = {};
	columnPartRef.elemGuid = curveMembers[1].elemGuid;
	columnPartRef.partRefType = API_AMPartRef_BegVertex;
	columnPartRef.partId.edgeOrVertexIdx = 0;
	columnPartRef.partId.contourIdx = 0;

	API_Element link1DElement;
	API_AnalyticalLinkType analytical1DLink = GetAnalytical1DLink (beamPartRef, columnPartRef);

	ACAPI_Analytical_SetElementFromLink (analytical1DLink, link1DElement);
	link1DElement.header.type = API_AnalyticalLinkID;
	link1DElement.header.layer = APIApplicationLayerAttributeIndex;
	link1DElement.header.renovationStatus = API_ExistingStatus;
	link1DElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally


	err = ACAPI_Element_Create (&link1DElement, nullptr);
	if (err != NoError)
		return err;

	// Create 2D analytical link
	API_AnalyticalMemberPartRef beamPartRef2 = {};
	beamPartRef2.elemGuid = curveMembers[0].elemGuid;
	beamPartRef2.partRefType = API_AMPartRef_EndVertex;
	beamPartRef2.partId.edgeOrVertexIdx = 0;
	beamPartRef2.partId.contourIdx = 0;

	API_Element link2DElement;
	API_AnalyticalElemPartId elemPartId = {
		surfaceMembers[1].elemGuid,			//memberGuid
		0,									//contourIndex	-0 for outline | 1,2,3... for holes
		1									//edgeOrVertexIndex
	};

	API_AnalyticalLinkType analytical2DLink = GetAnalytical2DLink (beamPartRef2, idMapping.edges[elemPartId]);

	ACAPI_Analytical_SetElementFromLink (analytical2DLink, link2DElement);
	link2DElement.header.type = API_AnalyticalLinkID;
	link2DElement.header.layer = APIApplicationLayerAttributeIndex;
	link2DElement.header.renovationStatus = API_ExistingStatus;
	link2DElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally


	err = ACAPI_Element_Create (&link2DElement, nullptr);
	if (err != NoError)
		return err;

	return err;
}


static GSErrCode CreateAnalyticalSupports (const API_ImportIdMappingType& idMapping)
{
	GSErrCode err = NoError;

	GS::Array<API_AnalyticalCurveMember> curveMembers = GetCurveMembers ();
	GS::Array<API_AnalyticalSurfaceMember> surfaceMembers = GetSurfaceMembers ();

	// Line Support
	API_Element supportElement;
	API_AnalyticalMemberPartRef columnPartRef4 = {};
	columnPartRef4.elemGuid = curveMembers[3].elemGuid;
	columnPartRef4.partRefType = API_AMPartRef_Edge;
	columnPartRef4.partId.edgeOrVertexIdx = 0;
	columnPartRef4.partId.contourIdx = 0;

	API_AnalyticalSupportType analyticalSupportType = GetAnalyticalSupport (columnPartRef4);

	ACAPI_Analytical_SetElementFromSupport (analyticalSupportType, supportElement);
	supportElement.header.type = API_AnalyticalSupportID;
	supportElement.header.layer = APIApplicationLayerAttributeIndex;
	supportElement.header.renovationStatus = API_ExistingStatus;
	supportElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

	err = ACAPI_Element_Create (&supportElement, nullptr);
	if (err != NoError)
		return err;

	// Point Support
	API_AnalyticalMemberPartRef columnPartRef5 = {};
	columnPartRef5.elemGuid = curveMembers[3].elemGuid;
	columnPartRef5.partRefType = API_AMPartRef_BegVertex;
	columnPartRef5.partId.edgeOrVertexIdx = 0;
	columnPartRef5.partId.contourIdx = 0;
	analyticalSupportType = GetAnalyticalSupport (columnPartRef5);

	ACAPI_Analytical_SetElementFromSupport (analyticalSupportType, supportElement);
	supportElement.header.type = API_AnalyticalSupportID;
	supportElement.header.layer = APIApplicationLayerAttributeIndex;
	supportElement.header.renovationStatus = API_ExistingStatus;
	supportElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

	err = ACAPI_Element_Create (&supportElement, nullptr);
	if (err != NoError)
		return err;

	// Surface Support
	API_AnalyticalElemPartId elemPartId2 = {
		surfaceMembers[1].elemGuid,			//memberGuid
		0,									//contourIndex	-0 for outline | 1,2,3... for holes
		1									//edgeOrVertexIndex
	};
	API_AnalyticalMemberPartRef slabPartRef = idMapping.edges[elemPartId2];
	slabPartRef.partRefType = API_AMPartRef_Surface;
	analyticalSupportType = GetAnalyticalSupport (slabPartRef);

	ACAPI_Analytical_SetElementFromSupport (analyticalSupportType, supportElement);
	supportElement.header.type = API_AnalyticalSupportID;
	supportElement.header.layer = APIApplicationLayerAttributeIndex;
	supportElement.header.renovationStatus = API_ExistingStatus;
	supportElement.header.renovationFilterGuid = APINULLGuid;

	err = ACAPI_Element_Create (&supportElement, nullptr);
	if (err != NoError)
		return err;

	return NoError;
}


static GSErrCode CreateAnalyticalLoads ()
{
	GSErrCode err = NoError;

	GS::Array<API_AnalyticalCurveMember> curveMembers = GetCurveMembers ();

	API_AnalyticalMemberPartRef columnPartRef5 = {};
	columnPartRef5.elemGuid = curveMembers[3].elemGuid;
	columnPartRef5.partRefType = API_AMPartRef_BegVertex;
	columnPartRef5.partId.edgeOrVertexIdx = 0;
	columnPartRef5.partId.contourIdx = 0;

	GS::Array<API_Guid> loadCases;
	err = ACAPI_Analytical_GetAllAnalyticalLoadCases (loadCases);
	if (err != NoError || loadCases.IsEmpty ()) {
		return err;
	}

	API_AnalyticalPointLoadType pointLoadType = GetAnalyticalPointLoad (columnPartRef5, loadCases.GetFirst ());

	API_Element pointLoadElement;
	ACAPI_Analytical_SetElementFromPointLoad (pointLoadType, pointLoadElement);
	pointLoadElement.header.type = API_AnalyticalPointLoadID;
	pointLoadElement.header.layer = APIApplicationLayerAttributeIndex;
	pointLoadElement.header.renovationStatus = API_ExistingStatus;
	pointLoadElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

	err = ACAPI_Element_Create (&pointLoadElement, nullptr);
	if (err != NoError)
		return err;

	// First Edge Load
	API_AnalyticalMemberPartRef firstBeamPartRef = {};
	firstBeamPartRef.elemGuid = curveMembers[3].elemGuid;
	firstBeamPartRef.partRefType = API_AMPartRef_Edge;
	firstBeamPartRef.partId.edgeOrVertexIdx = 0;
	firstBeamPartRef.partId.contourIdx = 0;

	API_AnalyticalEdgeLoadType edgeLoadType = GetAnalyticalEdgeLoad (firstBeamPartRef, loadCases.GetFirst ());

	API_Element firstEdgeLoadElement;

	ACAPI_Analytical_SetElementFromEdgeLoad (edgeLoadType, firstEdgeLoadElement);
	firstEdgeLoadElement.header.type = API_AnalyticalEdgeLoadID;
	firstEdgeLoadElement.header.layer = APIApplicationLayerAttributeIndex;
	firstEdgeLoadElement.header.renovationStatus = API_ExistingStatus;
	firstEdgeLoadElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

	err = ACAPI_Element_Create (&firstEdgeLoadElement, nullptr);
	if (err != NoError)
		return err;

	// Second Edge Load with custom ratios
	API_AnalyticalMemberPartRef secondBeamPartRef = {};
	secondBeamPartRef.elemGuid = curveMembers[1].elemGuid;
	secondBeamPartRef.partRefType = API_AMPartRef_RatioEdge;
	secondBeamPartRef.partId.edgeOrVertexIdx = 0;
	secondBeamPartRef.partId.contourIdx = 0;
	secondBeamPartRef.ratioEdgeData.ratioStart = 0.2;
	secondBeamPartRef.ratioEdgeData.ratioEnd = 0.7;

	API_AnalyticalEdgeLoadType beamEdgeLoadType = GetAnalyticalEdgeLoad (secondBeamPartRef, loadCases.GetFirst ());

	API_Element secondEdgeLoadElement;
	ACAPI_Analytical_SetElementFromEdgeLoad (beamEdgeLoadType, secondEdgeLoadElement);

	secondEdgeLoadElement.header.type = API_AnalyticalEdgeLoadID;
	secondEdgeLoadElement.header.layer = APIApplicationLayerAttributeIndex;
	secondEdgeLoadElement.header.renovationStatus = API_ExistingStatus;
	secondEdgeLoadElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

 	err = ACAPI_Element_Create (&secondEdgeLoadElement, nullptr);
 	if (err != NoError)
 		return err;

	GS::Array<API_AnalyticalSurfaceMember> surfaceMembers = GetSurfaceMembers ();

	API_AnalyticalEdgeLoadType firstEdgeLoad;
	ACAPI_Analytical_GetEdgeLoadFromElement (firstEdgeLoadElement, firstEdgeLoad);
	firstEdgeLoad.loadedMemberPartRef.partRefType = API_AMPartRef_CustomEdge;
	firstEdgeLoad.loadedMemberPartRef.elemGuid = surfaceMembers[0].elemGuid;
	ACAPI_Analytical_SetElementFromEdgeLoad (firstEdgeLoad, firstEdgeLoadElement);

	API_EdgeLoadCustomData edgeLoadCustomData;

	GS::Array<API_AnalyticalCurve> curves = GetSurfaceEdges ();
	for (const API_AnalyticalCurve& curve : curves) {

		edgeLoadCustomData.curve = { curve };

		API_ElementMemo edgeLoadMemo = {};
		err = ACAPI_Analytical_SetMemoFromEdgeLoadCustomData (edgeLoadCustomData, edgeLoadMemo);

		err = ACAPI_Element_Create (&firstEdgeLoadElement, &edgeLoadMemo);

		ACAPI_DisposeElemMemoHdls (&edgeLoadMemo);

		if (err != NoError)
			return err;
	}

	// SurfaceLoad
	API_AnalyticalMemberPartRef surfaceMemberPartRef = {};
	surfaceMemberPartRef.elemGuid = surfaceMembers[0].elemGuid;
	surfaceMemberPartRef.partRefType = API_AMPartRef_Surface;
	surfaceMemberPartRef.partId.edgeOrVertexIdx = 0;
	surfaceMemberPartRef.partId.contourIdx = 1;
	surfaceMemberPartRef.partId.edgeFragmentIndex = 0;

	API_AnalyticalSurfaceLoadType surfaceLoadType = GetAnalyticalSurfaceLoad (surfaceMemberPartRef, loadCases.GetFirst ());

	API_Element surfaceLoadElement;
	ACAPI_Analytical_SetElementFromSurfaceLoad (surfaceLoadType, surfaceLoadElement);

	surfaceLoadElement.header.type = API_AnalyticalSurfaceLoadID;
	surfaceLoadElement.header.layer = APIApplicationLayerAttributeIndex;
	surfaceLoadElement.header.renovationStatus = API_ExistingStatus;
	surfaceLoadElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

 	err = ACAPI_Element_Create (&surfaceLoadElement, nullptr);
 	if (err != NoError)
 		return err;
	
	// SurfaceLoad
	surfaceMemberPartRef.partRefType = API_AMPartRef_CustomSurface;

	API_AnalyticalSurfaceLoadType customSurfaceLoadType = GetAnalyticalSurfaceLoad (surfaceMemberPartRef, loadCases.GetFirst ());

	API_Element customSurfaceLoadElement;
	ACAPI_Analytical_SetElementFromSurfaceLoad (customSurfaceLoadType, customSurfaceLoadElement);

	customSurfaceLoadElement.header.type = API_AnalyticalSurfaceLoadID;
	customSurfaceLoadElement.header.layer = APIApplicationLayerAttributeIndex;
	customSurfaceLoadElement.header.renovationStatus = API_ExistingStatus;
	customSurfaceLoadElement.header.renovationFilterGuid = APINULLGuid;		// APINULLGuid is handled internally

	API_SurfaceLoadCustomData surfaceLoadCustomData;
	surfaceLoadCustomData.contour = GetSurfaceRegion ();

	API_ElementMemo surfaceLoadMemo = {};
	ACAPI_Analytical_SetMemoFromSurfaceLoadCustomData (surfaceLoadCustomData, surfaceLoadMemo);

 	err = ACAPI_Element_Create (&customSurfaceLoadElement, &surfaceLoadMemo);

	ACAPI_DisposeElemMemoHdls (&surfaceLoadMemo);

 	if (err != NoError)
 		return err;

	return err;
}

static GSErrCode SetAnalyticalModelVisibilitySettings ()
{
	API_AM_VisibilitySettings visibilitySettings = {};
	GSErrCode err = ACAPI_Analytical_GetVisibilitySettings (visibilitySettings);
	if (err != NoError)
		return err;

	visibilitySettings.visibilityMode = API_AM_VisibilityMode::AnalyticalModelOnly;
	visibilitySettings.isAnalyticalModelVisible = true;
	visibilitySettings.areNodesVisible = true;
	visibilitySettings.areCrossSectionsVisible = true;

	return ACAPI_Analytical_SetVisibilitySettings (visibilitySettings);
}

static GSErrCode CreatePlan ()
{
	GSErrCode err = NoError;
	
	err = CreateCurveMembers ();
	if (err != NoError)
		return err;

	API_ImportIdMappingType	idMapping;
	err = CreateSurfaceMembers (idMapping);
	if (err != NoError)
		return err;

	err = CreateAnalyticalLinks (idMapping);
	if (err != NoError)
		return err;

	err = CreateAnalyticalSupports (idMapping);
	if (err != NoError)
		return err;

	err = CreateLoadGroupsAndCasesAndCombinations ();
	if (err != NoError)
		return err;

	err = CreateAnalyticalLoads ();
	if (err != NoError)
		return err;

	return SetAnalyticalModelVisibilitySettings ();
}


static GSErrCode CreateStructuralAnalyticalModelTestPlan (void)
{
	return ACAPI_CallUndoableCommand ("Command", [&]() -> GSErrCode {
		return CreatePlan ();
	});
}


static void WriteReportForElements (const GS::Array<API_Guid>& elementGuids, const std::function<GS::UniString (const API_Element&)>& reportGeneratorForElem)
{
	for (const API_Guid& elemGuid : elementGuids) {
		API_Element elem = {};
		elem.header.guid = elemGuid;

		ACAPI_Element_Get (&elem);

		ACAPI_WriteReport (reportGeneratorForElem (elem), false);
	}
}


static bool	IsLinkInIntersection (const API_AnalyticalLinkType& linkType) 
{
	if (linkType.linkSettings.geometry != API_Link1D)
		return false;

	return linkType.startElementPartRef.partRefType == API_AMPartRef_Edge && linkType.endElementPartRef.partRefType == API_AMPartRef_Edge;
}


static void WriteLocalCoordinateSystemInIntersection (const API_AnalyticalLinkType& linkType)
{
	API_CoordinateSystem coordinateSystem;

	ACAPI_Analytical_GetLCSOfAnalyticalMemberInIntersection (linkType.startElementPartRef, linkType.endElementPartRef, coordinateSystem);

	ACAPI_WriteReport ("LCS in intersection : x = (%f, %f, %f), y = (%f, %f, %f)", false,
						coordinateSystem.xVector.x, coordinateSystem.xVector.y, coordinateSystem.xVector.z,
						coordinateSystem.yVector.x, coordinateSystem.yVector.y, coordinateSystem.yVector.z);
}

static GSErrCode ListStructuralAnalyticalElements () 
{
	// Get Analytical Model
	API_Guid amGuid;
	GSErrCode err = ACAPI_Analytical_GetCurrentAnalyticalModel (amGuid);
	if (err != NoError)
		return err;

	GS::Array<API_AnalyticalReleaseType> api_releaseElements;

	// Get Curve Elements
	GS::Array<API_Guid> curveElements;
	err = ACAPI_Analytical_GetCurveElements (amGuid, curveElements);
	if (err != NoError)
		return err;

	for (auto& curveElement : curveElements) {

		API_AnalyticalCurveMember api_curveMember;
		err = ACAPI_Analytical_GetCurveMember (amGuid, curveElement, api_curveMember);
		if (err != NoError)
			return err;

		ACAPI_WriteReport ("Analytical Curve Member - " + APIGuidToString (curveElement), false);

		API_AnalyticalReleaseType api_releaseElement;
		err = ACAPI_Analytical_GetAnalyticalRelease (amGuid, curveElement, api_releaseElement);
		if (err != NoError)
			return err;

		if (api_releaseElement.releaseDescriptions.GetSize () > 0)
			api_releaseElements.Push (api_releaseElement);

	}

	GS::Array<API_Guid> linkElements;
	err = ACAPI_Element_GetElemList (API_AnalyticalLinkID, &linkElements);
	if (err != NoError)
		return err;

	ACAPI_WriteReport ("-----------------------------", false);

	WriteReportForElements (linkElements, [](const API_Element& api_element) {
		API_AnalyticalLinkType link;
		ACAPI_Analytical_GetLinkFromElement (api_element, link);
		if (IsLinkInIntersection (link)) {
			WriteLocalCoordinateSystemInIntersection (link);
		}

		GS::UniString elemTypeName;
		ACAPI_Element_GetElemTypeName (api_element.header.type, elemTypeName);

		return GS::UniString (elemTypeName + " - " + APIGuidToString (link.startElementPartRef.elemGuid));
	});

	GS::Array<API_Guid> supportElements;
	err = ACAPI_Element_GetElemList (API_AnalyticalSupportID, &supportElements);
	if (err != NoError)
		return err;

	ACAPI_WriteReport ("-----------------------------", false);

	WriteReportForElements (supportElements, [](const API_Element& api_element) {
		API_AnalyticalSupportType support;
		ACAPI_Analytical_GetSupportFromElement (api_element, support);

		GS::UniString elemTypeName;
		ACAPI_Element_GetElemTypeName (api_element.header.type, elemTypeName);

		return GS::UniString (elemTypeName + " - " + APIGuidToString (support.supportedMemberPartRef.elemGuid));
	});

	ACAPI_WriteReport ("-----------------------------", false);

	GS::Array<API_Guid> api_loadGroups;
	err = ACAPI_Analytical_GetAnalyticalLoadGroups (api_loadGroups);
	for (const API_Guid& loadGroup : api_loadGroups) {
		ACAPI_WriteReport ("Analytical Load Group - " + APIGuidToString (loadGroup), false);
	}

	ACAPI_WriteReport ("-----------------------------", false);

	GS::Array<API_Guid> api_loadCases;
	err = ACAPI_Analytical_GetAllAnalyticalLoadCases (api_loadCases);
	for (const API_Guid& loadCase : api_loadCases) {
		ACAPI_WriteReport ("Analytical Load Case - " + APIGuidToString (loadCase), false);
	}

	ACAPI_WriteReport ("-----------------------------", false);

	GS::Array<API_Guid> api_loadCombinations;
	err = ACAPI_Analytical_GetAllAnalyticalLoadCases (api_loadCombinations);
	for (const API_Guid& loadCombination : api_loadCombinations) {
		ACAPI_WriteReport ("Analytical Load Combination - " + APIGuidToString (loadCombination), false);
	}

	ACAPI_WriteReport ("-----------------------------", false);

	GS::Array<API_Guid> pointLoadElements;
	err = ACAPI_Element_GetElemList (API_AnalyticalPointLoadID, &pointLoadElements);
	if (err != NoError)
		return err;

	WriteReportForElements (pointLoadElements, [] (const API_Element& api_element) {
		API_AnalyticalPointLoadType load;
		ACAPI_Analytical_GetPointLoadFromElement (api_element, load);

		GS::UniString elemTypeName;
		ACAPI_Element_GetElemTypeName (api_element.header.type, elemTypeName);

		return GS::UniString (elemTypeName + " - " + APIGuidToString (api_element.header.guid));
	});

	ACAPI_WriteReport ("-----------------------------", false);

	GS::Array<API_Guid> edgeLoadElements;
	err = ACAPI_Element_GetElemList (API_AnalyticalEdgeLoadID, &edgeLoadElements);
	if (err != NoError)
		return err;

	WriteReportForElements (edgeLoadElements, [](const API_Element& api_element) {
		API_AnalyticalEdgeLoadType load;
		ACAPI_Analytical_GetEdgeLoadFromElement (api_element, load);

		GS::UniString elemTypeName;
		ACAPI_Element_GetElemTypeName (api_element.header.type, elemTypeName);

		return GS::UniString (elemTypeName + " - " + APIGuidToString (api_element.header.guid));
	});

	
	ACAPI_WriteReport ("-----------------------------", false);

	GS::Array<API_Guid> surfaceLoadElements;
	err = ACAPI_Element_GetElemList (API_AnalyticalSurfaceLoadID, &surfaceLoadElements);
	if (err != NoError)
		return err;

	WriteReportForElements (surfaceLoadElements, [](const API_Element& api_element) {
		API_AnalyticalSurfaceLoadType load;
		ACAPI_Analytical_GetSurfaceLoadFromElement (api_element, load);

		GS::UniString elemTypeName;
		ACAPI_Element_GetElemTypeName (api_element.header.type, elemTypeName);

		return GS::UniString (elemTypeName + " - " + APIGuidToString (api_element.header.guid));
	});

	for (const API_Guid& guid : surfaceLoadElements) {
		API_ElementMemo elemMemo = {};

		ACAPI_Element_GetMemo (guid, &elemMemo, APIMemoMask_SurfaceLoadCustomData);
		ACAPI_DisposeElemMemoHdls (&elemMemo);
	}

	return err;
}


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
	case STRUCTURAL_ANALYTICAL_MODEL_TEST_MENU_STRINGS:
	{
		switch (menuParams->menuItemRef.itemIndex) {
		case 1: 
			return CreateStructuralAnalyticalModelTestPlan ();
		case 2:
			return ListStructuralAnalyticalElements ();
		}
		break;
	}
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
	RSGetIndString (&envir->addOnInfo.name,        STRUCTURAL_ANALYTICAL_MODEL_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, STRUCTURAL_ANALYTICAL_MODEL_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (STRUCTURAL_ANALYTICAL_MODEL_TEST_MENU_STRINGS, STRUCTURAL_ANALYTICAL_MODEL_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default);

	DBASSERT (err == NoError);

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (STRUCTURAL_ANALYTICAL_MODEL_TEST_MENU_STRINGS, MenuCommandHandler);

	DBASSERT (err == NoError);

	return err;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}
