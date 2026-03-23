#include "PlaceElements.hpp"
#include "Reporter.hpp"

// API
#include "ACAPinc.h"
#include "API_Guid.hpp"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"

#include "ACAPI/MEPPort.hpp"
#include "ACAPI/MEPPipingPort.hpp"

#include "ACAPI/MEPRoutingElement.hpp"
#include "ACAPI/MEPRoutingElementDefault.hpp"
#include "ACAPI/MEPRoutingNode.hpp"
#include "ACAPI/MEPRoutingSegment.hpp"
#include "ACAPI/MEPRoutingNodeDefault.hpp"
#include "ACAPI/MEPRoutingSegmentDefault.hpp"
#include "ACAPI/MEPBendDefault.hpp"

#include "ACAPI/MEPTakeOffDefault.hpp"
#include "ACAPI/MEPTerminal.hpp"
#include "ACAPI/MEPTerminalDefault.hpp"
#include "ACAPI/MEPFlexibleSegment.hpp"
#include "ACAPI/MEPFlexibleSegmentDefault.hpp"
#include "ACAPI/MEPFitting.hpp"
#include "ACAPI/MEPFittingDefault.hpp"
#include "ACAPI/MEPAccessory.hpp"
#include "ACAPI/MEPAccessoryDefault.hpp"
#include "ACAPI/MEPEquipmentDefault.hpp"

#include "ACAPI/MEPPipeReferenceSet.hpp"
#include "ACAPI/MEPPipeSegmentPreferenceTable.hpp"
#include "ACAPI/MEPPipeElbowPreferenceTable.hpp"
#include "ACAPI/MEPPipeBranchPreferenceTable.hpp"

// Geometry
#include "AngleData.h"
#include "Vector3D.hpp"


using namespace ACAPI::MEP;
using RectangularCrossSectionDataMap = std::map<UInt32, RoutingSegmentRectangularCrossSectionData>;
using CircularCrossSectionDataMap = std::map<UInt32, RoutingSegmentCircularCrossSectionData>;
using CrossSectionDataMapPair = std::pair<RectangularCrossSectionDataMap, CircularCrossSectionDataMap>;
using RoutePlacementData = std::pair<std::vector<API_Coord3D>, CrossSectionDataMapPair>;

namespace MEPExample {


namespace {


GSErrCode PlaceTerminals ()
{
// ! [Terminal-Placement-Example]
	{
		auto ductTerminalDefault = CreateTerminalDefault (Domain::Ventilation);
		if (ductTerminalDefault.IsErr ())
			return ductTerminalDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> terminalId = ductTerminalDefault->Place ({ 0.0, 10.0, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (terminalId.IsErr ())
			return terminalId.UnwrapErr ().kind;

		ACAPI_WriteReport ("Ventilation Terminal placed successfully", false);
	}

	{
		auto pipeTerminalDefault = CreateTerminalDefault (Domain::Piping);
		if (pipeTerminalDefault.IsErr ())
			return pipeTerminalDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> terminalId = pipeTerminalDefault->Place ({ 10.0, 10.0, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (terminalId.IsErr ())
			return terminalId.UnwrapErr ().kind;

		ACAPI_WriteReport ("Piping Terminal placed successfully", false);
	}
// ! [Terminal-Placement-Example]

	return NoError;
}


GSErrCode PlaceAccessories ()
{
// ! [Accessory-Placement-Example]
	{
		auto ductAccessoryDefault = CreateAccessoryDefault (Domain::Ventilation);
		if (ductAccessoryDefault.IsErr ())
			return ductAccessoryDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> accessoryId = ductAccessoryDefault->Place ({ 0.0, 13.0, 0.0 }, { { -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (accessoryId.IsErr ())
			return accessoryId.UnwrapErr ().kind;

		ACAPI_WriteReport ("Ventilation Accessory placed successfully", false);
	}

	{
		auto pipeAccessoryDefault = CreateAccessoryDefault (Domain::Piping);
		if (pipeAccessoryDefault.IsErr ())
			return pipeAccessoryDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> accessoryId = pipeAccessoryDefault->Place ({ 10.0, 13.0, 0.0 }, { { -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (accessoryId.IsErr ())
			return accessoryId.UnwrapErr ().kind;

		ACAPI_WriteReport ("Piping Accessory placed successfully", false);
	}
// ! [Accessory-Placement-Example]

	return NoError;
}


GSErrCode PlaceEquipment ()
{
// ! [Equipment-Placement-Example]
	auto equipmentDefault = CreateEquipmentDefault ();
	if (equipmentDefault.IsErr ())
		return equipmentDefault.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> equipmentId = equipmentDefault->Place ({ 20.0, 11.5, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
	if (equipmentId.IsErr ())
		return equipmentId.UnwrapErr ().kind;

	ACAPI_WriteReport ("Equipment placed successfully", false);
// ! [Equipment-Placement-Example]

	return NoError;
}


GSErrCode PlaceFittings ()
{
// ! [Fitting-Placement-Example]
	{
		auto ductFittingDefault = CreateFittingDefault (Domain::Ventilation);
		if (ductFittingDefault.IsErr ())
			return ductFittingDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> fitting = ductFittingDefault->Place ({ 0.0, 20.0, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (fitting.IsErr ())
			return fitting.UnwrapErr ().kind;

		ACAPI_WriteReport ("Ventilation Fitting placed successfully", false);
	}

	{
		auto pipeFittingDefault = CreateFittingDefault (Domain::Piping);
		if (pipeFittingDefault.IsErr ())
			return pipeFittingDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> fitting = pipeFittingDefault->Place ({ 10.0, 20.0, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (fitting.IsErr ())
			return fitting.UnwrapErr ().kind;

		ACAPI_WriteReport ("Piping Fitting placed successfully", false);
	}

	{
		auto cableCarrierFittingDefault = CreateFittingDefault (Domain::CableCarrier);
		if (cableCarrierFittingDefault.IsErr ())
			return cableCarrierFittingDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> fitting = cableCarrierFittingDefault->Place ({ 20.0, 20.0, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (fitting.IsErr ())
			return fitting.UnwrapErr ().kind;

		ACAPI_WriteReport ("Cable Carrier Fitting placed successfully", false);
	}
// ! [Fitting-Placement-Example]

	return NoError;
}


GSErrCode PlaceFlexibleSegment ()
{
	{
// ! [FlexibleSegment-Placement-Example]
		auto ductFlexibleSegmentDefault = CreateFlexibleSegmentDefault (Domain::Ventilation);
		if (ductFlexibleSegmentDefault.IsErr ())
			return ductFlexibleSegmentDefault.UnwrapErr ().kind;

		std::vector<API_Coord3D> controlPoints = {
			{ 0.0, 30.0, 0.0 },
			{ 2.0, 31.0, 0.0 },
			{ 4.0, 29.0, 0.0 },
			{ 6.0, 30.0, 0.0 }
		};

		ACAPI::Result<UniqueID> flexibleSegment = ductFlexibleSegmentDefault->Place (controlPoints, { { -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (flexibleSegment.IsErr ())
			return flexibleSegment.UnwrapErr ().kind;

		ACAPI_WriteReport ("Ventilation FlexibleSegment placed successfully", false);
	}
	{
		auto pipeFlexibleSegmentDefault = CreateFlexibleSegmentDefault (Domain::Piping);
		if (pipeFlexibleSegmentDefault.IsErr ())
			return pipeFlexibleSegmentDefault.UnwrapErr ().kind;

		std::vector<API_Coord3D> controlPoints = {
			{ 20.0, 30.0, 0.0 },
			{ 22.0, 31.0, 0.0 },
			{ 24.0, 29.0, 0.0 },
			{ 26.0, 30.0, 0.0 }
		};

		ACAPI::Result<UniqueID> flexibleSegment = pipeFlexibleSegmentDefault->Place (controlPoints, { { -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, GenerateAPIGuid ());
		if (flexibleSegment.IsErr ())
			return flexibleSegment.UnwrapErr ().kind;

		ACAPI_WriteReport ("Piping FlexibleSegment placed successfully", false);
	}
// ! [FlexibleSegment-Placement-Example]

	return NoError;
}


ACAPI::Result<std::vector<UniqueID>> PlaceRoutes (const std::vector<RoutePlacementData>& placementData, Domain domain)
{
// ! [RoutingElement-Placement-Example]
	std::vector<UniqueID> routeIds;
	routeIds.reserve (placementData.size ());

	ACAPI::Result<RoutingElementDefault> routingElementDefault = CreateRoutingElementDefault (domain);
	if (routingElementDefault.IsErr ())
		return { routingElementDefault.UnwrapErr (), ACAPI_GetToken () };

	for (const auto& [referenceCoords, crossSectionDataMaps] : placementData) {

		// Optionally, you could provide a GUID for the placed route with a third parameter
		ACAPI::Result<UniqueID> routingElemId = routingElementDefault->Place (referenceCoords, crossSectionDataMaps.first, crossSectionDataMaps.second);
		if (routingElemId.IsErr ())
			return { routingElemId.UnwrapErr (), ACAPI_GetToken () };

		routeIds.emplace_back (*routingElemId);
	}

	return ACAPI::Ok (std::move (routeIds));
// ! [RoutingElement-Placement-Example]
}



ACAPI::Result<std::vector<UniqueID>> PlaceRoutesWithGivenRoutingElementDefault (const RoutingElementDefault& routingElementDefault, const std::vector<RoutePlacementData>& placementData)
{
	std::vector<UniqueID> routeIds;
	routeIds.reserve (placementData.size ());

	for (const auto& [referenceCoords, crossSectionDataMaps] : placementData) {

		// Optionally, you could provide a GUID for the placed route with a third parameter
		ACAPI::Result<UniqueID> routingElemId = routingElementDefault.Place (referenceCoords, crossSectionDataMaps.first, crossSectionDataMaps.second);
		if (routingElemId.IsErr ())
			return { routingElemId.UnwrapErr (), ACAPI_GetToken () };

		routeIds.emplace_back (*routingElemId);
	}

	return ACAPI::Ok (std::move (routeIds));
}

// ! [RoutingElement-Connect-Example]
ACAPI::Result<std::vector<RouteConnectionResult>> ConnectRoutes (const std::vector<UniqueID>& routeIds)
{
	if (routeIds.empty ())
		return { ACAPI::Error (ErrParam, "Route vector is empty."), ACAPI_GetToken () };

	ACAPI::Result<RoutingElement> routingElem = RoutingElement::Get (routeIds[0]);
	if (routingElem.IsErr ())
		return { routingElem.UnwrapErr (), ACAPI_GetToken () };

	std::vector<RouteConnectionResult> routeConnectionResults;

	for (size_t i = 1; i < routeIds.size (); ++i) {
		ACAPI::Result<void> modifyResult = routingElem->Modify ([&](RoutingElement::Modifier& modifier) -> GSErrCode {
			ACAPI::Result<RouteConnectionResult> connectionResult = modifier.ConnectLogically (routeIds[i]);
			if (connectionResult.IsErr ())
				return connectionResult.UnwrapErr ().kind;

			routeConnectionResults.push_back (*connectionResult);
			return NoError;
		}, "Connect routes.");

		if (modifyResult.IsErr ())
			return { modifyResult.UnwrapErr (), ACAPI_GetToken () };
	}

	return ACAPI::Ok (routeConnectionResults);
}
// ! [RoutingElement-Connect-Example]


GSErrCode PlaceRoutesAndConnect (const std::vector<RoutePlacementData>& placementData, Domain domain)
{
	if (placementData.size () < 2)
		return Error;

	// ! [RoutingElement-PlaceAndConnectWrapped-Example]
	ACAPI::Result<void> commandResult = ACAPI::CallUndoableCommand ([&]() -> GSErrCode {
		ACAPI::Result<std::vector<UniqueID>> routeIds = PlaceRoutes (placementData, domain);

		if (routeIds.IsErr ())
			return routeIds.UnwrapErr ().kind;

		ACAPI::Result<std::vector<RouteConnectionResult>> connectionResult = ConnectRoutes (*routeIds);

		if (connectionResult.IsErr ())
			return connectionResult.UnwrapErr ().kind;

		return NoError;
	}, ACAPI_GetToken (), "Place and connect routes.");
	// ! [RoutingElement-PlaceAndConnectWrapped-Example]

	if (commandResult.IsErr ())
		return commandResult.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceTakeOff ()
{
// ! [Take-Off-Placement-Example]
	// First, we create a simple route to place the Take-Off on. For more details, see RoutingElement-Placement-Example.
	ACAPI::Result<RoutingElementDefault> routingElementDefault = CreateRoutingElementDefault (Domain::Ventilation);
	if (routingElementDefault.IsErr ()) {
		return routingElementDefault.UnwrapErr ().kind;
	}

	// Place a route consisting of a single, 10-meter-long segment facing north
	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData;

	ACAPI::Result<UniqueID> routingElemId = routingElementDefault->Place ({ {10.0, 30.0, 0.0}, {10.0, 25.0, 0.0} }, rectangularCrossSectionData, circularCrossSectionData);
	if (routingElemId.IsErr ()) {
		return routingElemId.UnwrapErr ().kind;
	}

	// Retrieve the routing element so we can access its segments
	const ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get (*routingElemId);
	if (routingElement.IsErr ()) {
		return routingElement.UnwrapErr ().kind;
	}

	// Retrieve the IDs of the segments of the routing element
	const std::vector<UniqueID> segmentIds = routingElement->GetRoutingSegmentIds ();
	if (segmentIds.empty ()) {
		return Error;
	}

	// Prepare the Take-off default
	ACAPI::Result<TakeOffDefault> takeOffDefault = CreateTakeOffDefault (Domain::Ventilation);
	if (takeOffDefault.IsErr ()) {
		return takeOffDefault.UnwrapErr ().kind;
	}

	// Place a Take-off near the beginning of the first route segment, which will face left relative to the segment direction
	ACAPI::Result<UniqueID> takeOffLeftId = takeOffDefault->Place (segmentIds[0], 1.25);
	if (takeOffLeftId.IsErr ()) {
		return takeOffLeftId.UnwrapErr ().kind;
	}

	// Place a Take-off near the end of the first route segment, which will face up
	ACAPI::Result<UniqueID> takeOffUpId = takeOffDefault->Place (segmentIds[0], 2.5, PI*0.5);
	if (takeOffUpId.IsErr ()) {
		return takeOffUpId.UnwrapErr ().kind;
	}

	// Place a Take-off near the end of the first route segment, which will face right relative to the segment direction
	ACAPI::Result<UniqueID> takeOffRightId = takeOffDefault->Place (segmentIds[0], 3.75, PI);
	if (takeOffRightId.IsErr ()) {
		return takeOffRightId.UnwrapErr ().kind;
	}

// ! [Take-Off-Placement-Example]
	return NoError;
}


GSErrCode PlaceRoutesWithGivenRoutingElementDefaultAndConnect (const RoutingElementDefault& routingElementDefault, const std::vector<RoutePlacementData>& placementData)
{
	if (placementData.size () < 2)
		return Error;

	ACAPI::Result<void> commandResult = ACAPI::CallUndoableCommand ([&]() -> GSErrCode {
		ACAPI::Result<std::vector<UniqueID>> routeIds = PlaceRoutesWithGivenRoutingElementDefault (routingElementDefault, placementData);

		if (routeIds.IsErr ())
			return routeIds.UnwrapErr ().kind;

		ACAPI::Result<std::vector<RouteConnectionResult>> connectionResult = ConnectRoutes (*routeIds);

		if (connectionResult.IsErr ())
			return connectionResult.UnwrapErr ().kind;

		return NoError;
	}, ACAPI_GetToken (), "Place and connect routes.");

	if (commandResult.IsErr ())
		return commandResult.UnwrapErr ().kind;

	return NoError;
}


} // namespace


GSErrCode PlaceMEPElements ()
{
	ERRCHK_NO_ASSERT (PlaceTerminals ());

	ERRCHK_NO_ASSERT (PlaceAccessories ());

	ERRCHK_NO_ASSERT (PlaceEquipment ());

	ERRCHK_NO_ASSERT (PlaceFittings ());

	ERRCHK_NO_ASSERT (PlaceFlexibleSegment ());

	ERRCHK_NO_ASSERT (PlaceTakeOff ());

	return NoError;
}


GSErrCode CopyPlaceSelectedTerminals ()
{
	GS::Array<API_Neig>	selNeigs;
	API_SelectionInfo 	selectionInfo = {};

	ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);

	for (const API_Neig& neig : selNeigs) {
		API_Element	elem = {};
		elem.header.guid = neig.guid;
		ACAPI_Element_Get (&elem);

		if (elem.header.type.typeID == API_ExternalElemID && IsTerminal (elem.header.type.classID)) {
			ACAPI::Result<Terminal> terminal = Terminal::Get (Adapter::UniqueID (neig.guid));
			TerminalDefault terminalDefault = terminal->PickUpDefault ();
			ACAPI::Result<UniqueID> newTerminalId = terminalDefault.Place ({ 10.0, 10.0, 0.0 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } });

			if (newTerminalId.IsErr ())
				return newTerminalId.UnwrapErr ().kind;

			Reporter reporter;
			reporter.Add ("New terminal placed successfully!");
			reporter.Add ("ID of the new terminal:", *newTerminalId);
			reporter.Write ();
		}
	}

	return NoError;
}


GSErrCode PlaceTwoContinuousRoutes ()
{
// ! [RoutingElement-PlacementData-Example]
	std::vector<API_Coord3D> referenceCoords1;
	referenceCoords1.push_back ({ 0.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 5.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 5.0, 5.0, 2.0 });
	referenceCoords1.push_back ({ 10.0, 5.0, 2.0 });
	referenceCoords1.push_back ({ 10.0, 10.0, 2.0 });

	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData1;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData1;

	std::vector<API_Coord3D> referenceCoords2;
	referenceCoords2.push_back ({ 10.0, 10.0, 2.0 });
	referenceCoords2.push_back ({ 15.0, 10.0, 2.0 });
	referenceCoords2.push_back ({ 15.0, 15.0, 2.0 });

	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData2;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData2;
	
	circularCrossSectionData2.emplace (0, ACAPI::MEP::RoutingSegmentCircularCrossSectionData (20));
	circularCrossSectionData2.emplace (1, ACAPI::MEP::RoutingSegmentCircularCrossSectionData (20));

	return PlaceRoutesAndConnect ({ { referenceCoords1, std::make_pair (rectangularCrossSectionData1, circularCrossSectionData1) }, { referenceCoords2, std::make_pair (rectangularCrossSectionData2, circularCrossSectionData2) } }, Domain::Piping);
// ! [RoutingElement-PlacementData-Example]
}


GSErrCode PlaceTwoRoutesInTShape ()
{
	std::vector<API_Coord3D> referenceCoords1;
	referenceCoords1.push_back ({ 0.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 5.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 10.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 15.0, 5.0, 2.0 });

	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData1;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData1;

	ACAPI::Result<RoutingElementDefault> routingElementDefault = CreateRoutingElementDefault (Domain::Ventilation);
	if (routingElementDefault.IsErr ())
		return routingElementDefault.UnwrapErr ().kind;

	RoutingSegmentDefault routingSegmentDefault = routingElementDefault->GetRoutingSegmentDefault ();

	/* Here it is in your hand to decide which segment's settings should be taken as the segmentDefault.
	If your route has more than one segment, and those segments differ in cross section size/shape, anchor position or offset from reference line,
	you cannot avoid getting the yellow triangle (because your segmentDefault's parameters cannot match both of your different segment settings). */
	routingSegmentDefault.Modify ([&] (RoutingSegmentDefault::Modifier& modifier) {
		ACAPI::MEP::RoutingSegmentRectangularCrossSectionData crossSectionData (0.8, 0.5);
		modifier.SetCrossSectionWidth (crossSectionData.width);
		modifier.SetCrossSectionHeight (crossSectionData.height);
		modifier.SetCrossSectionShape (ACAPI::MEP::ConnectorShape::Rectangular);
		modifier.SetAnchor (APIAnc_MM);
		modifier.SetReferenceLineOffsetX (0.0);
		modifier.SetReferenceLineOffsetY (0.0);
	});

	routingElementDefault->Modify ([&] (RoutingElementDefault::Modifier& modifier) {
		modifier.SetRoutingSegmentDefault (routingSegmentDefault);
	});

	std::vector<API_Coord3D> referenceCoords2;
	referenceCoords2.push_back ({ 2.5, 0.0, 2.0 });
	referenceCoords2.push_back ({ 2.5, -5.0, 2.0 });

	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData2;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData2;

	rectangularCrossSectionData2.emplace (0, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.9, 0.7));

	return PlaceRoutesWithGivenRoutingElementDefaultAndConnect (*routingElementDefault, { { referenceCoords1, std::make_pair (rectangularCrossSectionData1, circularCrossSectionData1) }, { referenceCoords2, std::make_pair (rectangularCrossSectionData2, circularCrossSectionData2) } });
}


GSErrCode PlaceThreeRoutesInTShape ()
{
	std::vector<API_Coord3D> referenceCoords1;
	referenceCoords1.push_back ({ 0.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 5.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 10.0, 0.0, 2.0 });
	referenceCoords1.push_back ({ 12.5, 5.0, 2.0 });
	referenceCoords1.push_back ({ 15.0, 5.0, 2.0 });

	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData1;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData1;

	rectangularCrossSectionData1.emplace (0, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.8, 0.4));
	rectangularCrossSectionData1.emplace (1, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.8, 0.4));
	rectangularCrossSectionData1.emplace (2, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.8, 0.4));
	rectangularCrossSectionData1.emplace (3, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.8, 0.4));

	std::vector<API_Coord3D> referenceCoords2;
	referenceCoords2.push_back ({ 15.0, 10.0, 2.0 });
	referenceCoords2.push_back ({ 15.0, 5.0, 2.0 });
	
	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData2;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData2;

	rectangularCrossSectionData2.emplace (0, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.9, 0.5));

	std::vector<API_Coord3D> referenceCoords3;
	referenceCoords3.push_back ({ 15.0, 5.0, 2.0 });
	referenceCoords3.push_back ({ 20.0, 5.0, 2.0 });

	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData3;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData3;

	rectangularCrossSectionData3.emplace (0, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.9, 0.5));

	return PlaceRoutesAndConnect ({ { referenceCoords1, std::make_pair (rectangularCrossSectionData1, circularCrossSectionData1) }, { referenceCoords2, std::make_pair (rectangularCrossSectionData2, circularCrossSectionData2) }, { referenceCoords3, std::make_pair (rectangularCrossSectionData3, circularCrossSectionData3) } }, Domain::CableCarrier);
}


GSErrCode PlaceConnectedElements ()
{
	ACAPI::Result<void> commandResultDuct = ACAPI::CallUndoableCommand ([&]() -> GSErrCode {
		auto flexibleSegmentDefault = CreateFlexibleSegmentDefault (Domain::Ventilation);
		if (flexibleSegmentDefault.IsErr ())
			return flexibleSegmentDefault.UnwrapErr ().kind;

		std::vector<API_Coord3D> controlPoints = {
			{ -10.0, -10.0, 0.0 },
			{ -5.0, -8.0, 0.0 },
			{ 0.0, -12.0, 0.0 },
			{ 5.0, -10.0, 0.0 }
		};

		ACAPI::Result<UniqueID> flexibleSegmentId = flexibleSegmentDefault->Place (controlPoints, { { -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } });
		if (flexibleSegmentId.IsErr ())
			return flexibleSegmentId.UnwrapErr ().kind;

// ! [RoutingElement-PlaceAndConnectRoutingElementToElement-Example]
		ACAPI::Result<FlexibleSegment> flexibleSegment = FlexibleSegment::Get (*flexibleSegmentId);
		if (flexibleSegment.IsErr ())
			return flexibleSegment.UnwrapErr ().kind;

		ACAPI::Result<Port> flexibleSegmentPort = Port::Get (flexibleSegment->GetPortIDs ()[0]);
		if (flexibleSegmentPort.IsErr ())
			return flexibleSegmentPort.UnwrapErr ().kind;

		ACAPI::Result<Port> flexibleSegmentPort2 = Port::Get (flexibleSegment->GetPortIDs ()[1]);
		flexibleSegmentPort->Modify ([&](Port::Modifier& modifier) {
			modifier.SetShape (ConnectorShape::Circular);
			modifier.SetWidth (0.35);

			flexibleSegmentPort2->Modify ([&](Port::Modifier& modifier) {
				modifier.SetShape (ConnectorShape::Circular);
				modifier.SetWidth (0.35);
			}, "Set FlexibleSegment Port2 size.");
		}, "Set FlexibleSegment Port size.");

		ACAPI::Result<TerminalDefault> terminalDefault = CreateTerminalDefault (Domain::Ventilation);
		if (terminalDefault.IsErr ())
			return terminalDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> terminalId = terminalDefault->Place (flexibleSegment->GetPortIDs ()[0]);
		if (terminalId.IsErr ())
			return terminalId.UnwrapErr ().kind;

		std::vector<API_Coord3D> referenceCoords;
		referenceCoords.push_back ({ 5.0, -10.0, 0.0 });
		referenceCoords.push_back ({ 8.0, -10.0, 0.0 });
		referenceCoords.push_back ({ 8.0, -10.0, 10.0 });

		std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData;
		std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData;

		rectangularCrossSectionData.emplace (0, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.3, 0.5));
		rectangularCrossSectionData.emplace (1, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData (0.8, 0.5));

		ACAPI::Result<std::vector<UniqueID>> routingElementId = PlaceRoutes ({ { referenceCoords, std::make_pair (rectangularCrossSectionData, circularCrossSectionData) } }, Domain::Ventilation);
		if (routingElementId.IsErr ())
			return routingElementId.UnwrapErr ().kind;

		ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get ((*routingElementId)[0]);
		if (routingElement.IsErr ())
			return routingElement.UnwrapErr ().kind;

		ACAPI::Result connectResult = routingElement->Modify ([&](RoutingElement::Modifier& modifier) -> GSErrCode {
			modifier.SetMEPSystem (flexibleSegmentPort2->GetMEPSystem ());
			ACAPI::Result<RouteConnectionResult> connectionResult = modifier.ConnectLogically (*flexibleSegmentId);
			if (connectionResult.IsErr ())
				return connectionResult.UnwrapErr ().kind;

			return NoError;
		}, "Connect RoutingElement to FlexibleSegment.");
// ! [RoutingElement-PlaceAndConnectRoutingElementToElement-Example]
		return connectResult.IsErr () ? connectResult.UnwrapErr ().kind : NoError;
	}, ACAPI_GetToken (), "Place and connect MEPElements and RoutingElement.");

	if (commandResultDuct.IsErr ())
		return commandResultDuct.UnwrapErr ().kind;


	ACAPI::Result<void> commandResultPipe = ACAPI::CallUndoableCommand ([&]() -> GSErrCode {
		auto terminalDefault = CreateTerminalDefault (Domain::Piping);
		if (terminalDefault.IsErr ())
			return terminalDefault.UnwrapErr ().kind;

		ACAPI::Result<UniqueID> terminalId = terminalDefault->Place ({ -10.0, -4.0, -0.225 }, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } });
		if (terminalId.IsErr ())
			return terminalId.UnwrapErr ().kind;

		ACAPI::Result<Terminal> terminal = Terminal::Get (*terminalId);
		if (terminal.IsErr ())
			return terminal.UnwrapErr ().kind;

		ACAPI::Result<Port> terminalPort = Port::Get (terminal->GetPortIDs ()[0]);
		if (terminalPort.IsErr ())
			return terminalPort.UnwrapErr ().kind;

		terminalPort->Modify ([&](Port::Modifier& modifier) {
			modifier.SetShape (ConnectorShape::Circular);
			modifier.SetWidth (0.09);
		}, "Set Terminal Port size.");

		auto flexibleSegmentDefault = CreateFlexibleSegmentDefault (Domain::Piping);
		if (flexibleSegmentDefault.IsErr ())
			return flexibleSegmentDefault.UnwrapErr ().kind;

		std::vector<API_Coord3D> controlPoints = {
			terminalPort->GetPosition (),
			{ -5.0, -2.0, 0.0 },
			{ 0.0, -6.0, 0.0 },
			{ 5.0, -4.0, 0.0 }
		};

		ACAPI::MEP::Orientation terminalPortOrientation = terminalPort->GetOrientation ();

		ACAPI::MEP::Orientation flexibleSegemntStartOrientation = terminalPortOrientation;
		flexibleSegemntStartOrientation.direction.x *= -1.0;
		flexibleSegemntStartOrientation.direction.y *= -1.0;
		flexibleSegemntStartOrientation.direction.z *= -1.0;

		ACAPI::Result<UniqueID> flexibleSegmentId = flexibleSegmentDefault->Place (controlPoints, flexibleSegemntStartOrientation, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } });
		if (flexibleSegmentId.IsErr ())
			return flexibleSegmentId.UnwrapErr ().kind;

		ACAPI::Result<FlexibleSegment> flexibleSegment = FlexibleSegment::Get (*flexibleSegmentId);
		if (flexibleSegment.IsErr ())
			return flexibleSegment.UnwrapErr ().kind;

		terminalPort->Modify ([&](Port::Modifier& modifier) {
			modifier.SetMEPSystem (Port::Get (flexibleSegment->GetPortIDs ()[0])->GetMEPSystem ());
		}, "Set Terminal Port MEPSystem.");

		ACAPI::Result<Port> flexibleSegmentPort = Port::Get (flexibleSegment->GetPortIDs ()[0]);
		if (flexibleSegmentPort.IsErr ())
			return flexibleSegmentPort.UnwrapErr ().kind;

		flexibleSegmentPort->Modify ([&](Port::Modifier& modifier) {
			modifier.SetShape (ConnectorShape::Circular);
			modifier.SetWidth (0.09);

			ACAPI::Result<Port> flexibleSegmentPort2 = Port::Get (flexibleSegment->GetPortIDs ()[1]);
			flexibleSegmentPort2->Modify ([&](Port::Modifier& modifier) {
				modifier.SetShape (ConnectorShape::Circular);
				modifier.SetWidth (0.09);
			}, "Set FlexibleSegment Port2 size.");
		}, "Set FlexibleSegment Port size.");


		std::vector<API_Coord3D> referenceCoords;
		referenceCoords.push_back ({ 5.0, -4.0, 0.0 });
		referenceCoords.push_back ({ 8.0, -4.0, 0.0 });
		referenceCoords.push_back ({ 8.0, -4.0, 10.0 });
		
		std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData;
		std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData;

		circularCrossSectionData.emplace (0, ACAPI::MEP::RoutingSegmentCircularCrossSectionData (20));
		circularCrossSectionData.emplace (1, ACAPI::MEP::RoutingSegmentCircularCrossSectionData (20));

		ACAPI::Result<std::vector<UniqueID>> routingElementId = PlaceRoutes ({ { referenceCoords, std::make_pair (rectangularCrossSectionData, circularCrossSectionData) } }, Domain::Piping);
		if (routingElementId.IsErr ())
			return routingElementId.UnwrapErr ().kind;

		ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get ((*routingElementId)[0]);
		if (routingElement.IsErr ())
			return routingElement.UnwrapErr ().kind;

		ACAPI::Result<void> connectResult = routingElement->Modify ([&](RoutingElement::Modifier& modifier) -> GSErrCode {
			ACAPI::Result<RouteConnectionResult> connectionResult = modifier.ConnectLogically (*flexibleSegmentId);
			if (connectionResult.IsErr ())
				return connectionResult.UnwrapErr ().kind;

			return NoError;
		}, "Connect RoutingElement to FlexibleSegment.");
		
		return connectResult.IsErr () ? connectResult.UnwrapErr ().kind : NoError;
	}, ACAPI_GetToken (), "Place and connect MEPElements and RoutingElement.");

	if (commandResultPipe.IsErr ())
		return commandResultPipe.UnwrapErr ().kind;

	return NoError;
}


std::vector<std::vector<UniqueID>> CollectTouchingRoutingNodes (const std::vector<UniqueID>& routeIds)
{
	std::vector<std::pair<UniqueID, API_Coord3D>> nodesWithCoords;
	for (const UniqueID& routeId : routeIds) {
		ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get (routeId);
		if (routingElement.IsErr ())
			continue;

		std::vector<UniqueID> nodeIds = routingElement->GetRoutingNodeIds ();
		ACAPI::Result<RoutingNode> firstNode = RoutingNode::Get (nodeIds.front ());
		if (firstNode.IsOk ())
			nodesWithCoords.push_back ({ *nodeIds.begin (), firstNode->GetPosition () });

		ACAPI::Result<RoutingNode> lastNode = RoutingNode::Get (nodeIds.back ());
		if (lastNode.IsOk ())
			nodesWithCoords.push_back ({ nodeIds.back (), lastNode->GetPosition () });
	}

	const auto IsNear = [](const API_Coord3D& lhs, const API_Coord3D& rhs) {
		return (lhs.x - rhs.x) * (lhs.x - rhs.x) + (lhs.y - rhs.y) * (lhs.y - rhs.y) + (lhs.z - rhs.z) * (lhs.z - rhs.z) < 1e-10;
	};

	std::unordered_set<UniqueID> processedNodes;
	std::vector<std::vector<UniqueID>> result;

	for (int i = 0; i < nodesWithCoords.size () - 1; ++i) {
		const auto& [nodeId, coord] = nodesWithCoords[i];
		std::vector<UniqueID> coincidentIds;

		if (processedNodes.find (nodeId) == processedNodes.end ()) {
			for (int j = i + 1; j < nodesWithCoords.size (); ++j) {
				const auto& [otherNodeId, otherCoord] = nodesWithCoords[j];
				if (IsNear (coord, otherCoord)) {
					coincidentIds.push_back (otherNodeId);
					processedNodes.emplace (otherNodeId);
				}
			}
		}

		if (!coincidentIds.empty ()) {
			coincidentIds.emplace_back (nodeId);
			result.emplace_back (std::move (coincidentIds));
		}
	}

	return result;
}


API_Coord3D GetConnectionGeometricalInformation (const std::vector<UniqueID>&		routingNodeIds, 
												 std::vector<Geometry::Vector3D>&	routingSegmentDirections,
												 std::vector<double>&				routingSegmentDiameters)
{
	std::optional<API_Coord3D> nodePosition;

	for (const UniqueID& nodeId : routingNodeIds) {
		ACAPI::Result<RoutingNode> node = RoutingNode::Get (nodeId);
		std::optional<UniqueID> otherNodeId;

		if (node->GetIncomingSegmentId ().has_value ()) {
			ACAPI::Result<RoutingSegment> routingSegment = RoutingSegment::Get (*node->GetIncomingSegmentId ());
			otherNodeId = routingSegment->GetBeginNodeId ();
			routingSegmentDiameters.push_back (routingSegment->GetCrossSectionWidth ());
		} else {
			ACAPI::Result<RoutingSegment> routingSegment = RoutingSegment::Get (*node->GetOutgoingSegmentId ());
			otherNodeId = routingSegment->GetEndNodeId ();
			routingSegmentDiameters.push_back (routingSegment->GetCrossSectionWidth ());
		}

		if (!nodePosition.has_value ())
			nodePosition = node->GetPosition ();

		ACAPI::Result<RoutingNode> otherNode = RoutingNode::Get (*otherNodeId);
		API_Coord3D otherPosition = otherNode->GetPosition ();

		Geometry::Vector3D result = { otherPosition.x - nodePosition->x, otherPosition.y - nodePosition->y, otherPosition.z - nodePosition->z };
		result.NormalizeVector ();
		routingSegmentDirections.push_back (result);
	}

	return *nodePosition;
}


Orientation CreateCrossOrientation (const std::vector<Geometry::Vector3D>& routeDirections)
{
	const auto ToAPI_Vector3D = [](const Geometry::Vector3D& vect) -> API_Vector3D {
		return { vect.x, vect.y, vect.z };
	};

	Orientation crossOrientation;
	crossOrientation.direction = ToAPI_Vector3D (routeDirections[0]);
	for (int i = 1; i < 4; ++i) {
		if (Geometry::IsNearZero (routeDirections[0] * routeDirections[i], 1e-5)) {
			crossOrientation.rotation = ToAPI_Vector3D (routeDirections[i]);
			break;
		}
	}

	return crossOrientation;
}


ACAPI::Result<FittingDefault> GetPipeCrossDefault ()
{
	ACAPI::Result<FittingDefault> fittingDefault = CreateFittingDefault (Domain::Piping);
	fittingDefault->Modify ([](FittingDefault::Modifier& modifier) {
		// Pipe Cross libPart id
		modifier.SetObjectId (APIGuidFromString ("{75D63A86-F1DB-4F05-951F-816BE7E6CA0E}"));
	});

	return fittingDefault;
}


std::vector<std::pair<UniqueID, UniqueID>> ResizeAndPairCrossPortsWithRoutingNodes (const Fitting&							fitting, 
																					const std::vector<UniqueID>&			routingNodeIds,
																					const std::vector<Geometry::Vector3D>&	routeDirections,
																					const std::vector<double>&				routeDiameters)
{
	std::vector<std::pair<UniqueID, UniqueID>> portToRouteMap;
	for (const UniqueID& portId : fitting.GetPortIDs ()) {
		ACAPI::Result<PipingPort> port = PipingPort::Get (portId);
		Orientation portOrientation = port->GetOrientation ();
		Geometry::Vector3D portDirection = { portOrientation.direction.x, portOrientation.direction.y, portOrientation.direction.z };

		for (int i = 0; i < 4; ++i) {
			if (portDirection.IsNear (routeDirections[i])) {
				portToRouteMap.push_back ({ portId, routingNodeIds[i] });
				port->Modify ([&](PipingPort::Modifier& portModifier) {
					portModifier.SetOuterDiameter (routeDiameters[i]);
				}, "Resize cross port.");
			}
		}
	}

	return portToRouteMap;
}


void MoveTheCrossToTheRoutesIntersectionPoint (Fitting fitting, const API_Coord3D& intersectionPoint)
{
	Geometry::Point3D actualCrossCenter = { 0.0, 0.0, 0.0 };
	for (const UniqueID& portId : fitting.GetPortIDs ()) {
		ACAPI::Result<Port> port = Port::Get (portId);
		const API_Coord3D portPosition = port->GetPosition ();
		actualCrossCenter += {portPosition.x, portPosition.y, portPosition.z};
	}
	actualCrossCenter /= 4;

	Geometry::Point3D requiredCrossCenter = { intersectionPoint.x, intersectionPoint.y, intersectionPoint.z };
	API_Coord3D actualFittingPosition = fitting.GetAnchorPoint ();
	Geometry::Point3D newFittingPosition = (requiredCrossCenter - actualCrossCenter) + Geometry::Point3D (actualFittingPosition.x, actualFittingPosition.y, actualFittingPosition.z);
	
	fitting.Modify ([&newFittingPosition](Fitting::Modifier& fittingModifier) {
		fittingModifier.SetAnchorPoint ({ newFittingPosition.x, newFittingPosition.y, newFittingPosition.z });
	}, "Move Cross to the routes intersection.");
}


void CutBackRoutesToCrossPorts (const std::vector<std::pair<UniqueID, UniqueID>>& portToRouteMap)
{
	for (const auto& [portId, nodeId] : portToRouteMap) {
		ACAPI::Result<RoutingNode> node = RoutingNode::Get (nodeId);
		ACAPI::Result<Port> port = Port::Get (portId);

		node->Modify ([&port](RoutingNode::Modifier& nodeModifier) {
			nodeModifier.SetPosition (port->GetPosition ());
		}, "Cut back route end to cross");
	}
}


ACAPI::Result<UniqueID> PlacePipeCrossBetweenRoutes (const std::vector<UniqueID>& routingNodeIds)
{
	std::vector<Geometry::Vector3D> routeDirections;
	std::vector<double> routeDiameters;
	API_Coord3D nodePosition = GetConnectionGeometricalInformation (routingNodeIds, routeDirections, routeDiameters);
	Orientation crossOrientation = CreateCrossOrientation (routeDirections);

	ACAPI::Result<FittingDefault> fittingDefault = GetPipeCrossDefault ();
	
	ACAPI::Result<UniqueID> fittingId = fittingDefault->Place (nodePosition, crossOrientation);
	if (fittingId.IsErr ())
		return fittingId;

	ACAPI::Result<Fitting> fitting = Fitting::Get (*fittingId);

	std::vector<std::pair<UniqueID, UniqueID>> portToRouteMap = ResizeAndPairCrossPortsWithRoutingNodes (*fitting, routingNodeIds, routeDirections, routeDiameters);

	MoveTheCrossToTheRoutesIntersectionPoint (*fitting, nodePosition);
	CutBackRoutesToCrossPorts (portToRouteMap);

	return fittingId;
}


GSErrCode ConnectRoutesWithTeeAndCross (const std::vector<UniqueID>& routeIds)
{
	std::vector<std::vector<UniqueID>> touchingRoutingNodes = CollectTouchingRoutingNodes (routeIds);

	const auto GetRouteIdFromNodeId = [](const UniqueID& id)->UniqueID {
		ACAPI::Result<RoutingNode> node = RoutingNode::Get (id);
		return node->GetRoutingElementId ();
	};

	for (const std::vector<UniqueID>& nodeIds : touchingRoutingNodes) {
		if (nodeIds.size () != 4)
			continue;

		ACAPI::Result<UniqueID> crossId = PlacePipeCrossBetweenRoutes (nodeIds);
		if (crossId.IsErr ())
			return crossId.UnwrapErr ().kind;

		for (const UniqueID& nodeId : nodeIds) {
			ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get (GetRouteIdFromNodeId (nodeId));
			if (routingElement.IsErr ())
				continue;

			routingElement->Modify ([&crossId](RoutingElement::Modifier& modifier) {
				ACAPI::Result<RouteConnectionResult> firstConnectResult = modifier.ConnectLogically (*crossId);
			}, "Connect route with Cross");
		}
	}

	for (const std::vector<UniqueID>& nodeIds : touchingRoutingNodes) {
		if (nodeIds.size () != 3)
			continue;

		ACAPI::Result<RoutingElement> firstRoutingElement = RoutingElement::Get (GetRouteIdFromNodeId (nodeIds[0]));
		if (firstRoutingElement.IsErr ())
			continue;

		firstRoutingElement->Modify ([&nodeIds, &GetRouteIdFromNodeId](RoutingElement::Modifier& modifier) {
			ACAPI::Result<RouteConnectionResult> firstConnectResult = modifier.ConnectLogically (GetRouteIdFromNodeId (nodeIds[1]));
			ACAPI::Result<RouteConnectionResult> secondConnectResult = modifier.ConnectLogically (GetRouteIdFromNodeId (nodeIds[2]));
		}, "Connect routes with Tee");
	}

	for (const std::vector<UniqueID>& nodeIds : touchingRoutingNodes) {
		if (nodeIds.size () != 2)
			continue;

		ACAPI::Result<RoutingElement> firstRoutingElement = RoutingElement::Get (GetRouteIdFromNodeId (nodeIds[0]));
		if (firstRoutingElement.IsErr ())
			continue;

		firstRoutingElement->Modify ([&nodeIds, &GetRouteIdFromNodeId](RoutingElement::Modifier& modifier) {
			ACAPI::Result<RouteConnectionResult> connectResult = modifier.ConnectLogically (GetRouteIdFromNodeId (nodeIds[1]));
		}, "Merge routes");
	}

	return NoError;
}


GSErrCode PlaceAndConnectRoutes ()
{
	ACAPI::Result<RoutingElementDefault> routingElementDefault = CreateRoutingElementDefault (Domain::Piping);

	ACAPI::Result<PipeReferenceSet> referenceSet = GetPipeReferenceSet ();
	bool found = false;
	for (uint32_t i = 0; i < referenceSet->GetSize (); ++i) {
		if (*referenceSet->GetReferenceId (i) == 200) {
			found = true;
			break;
		}
	}

	if (!found) {
		referenceSet->Modify ([&] (ReferenceSetBase::Modifier& modifier) {
			modifier.Add (200);
		}, "Add new value to the Reference Set.");
	}

	RoutingSegmentDefault routingSegmentDefault = routingElementDefault->GetRoutingSegmentDefault ();
	UniqueID segmentTableId = routingSegmentDefault.GetPreferenceTableId ();
	ACAPI::Result<PipeSegmentPreferenceTable> pipeSegmentTable = PipeSegmentPreferenceTable::Get (segmentTableId);
	pipeSegmentTable->Modify ([&] (PipeSegmentPreferenceTable::Modifier& modifier) {
		modifier.SetDiameterByReferenceId (200, 0.2);
		modifier.SetWallThicknessByReferenceId (200, 0.002);
	}, "Set Diameter and Wall Thickness for Segment");

	RoutingNodeDefault routingNodeDefault = routingElementDefault->GetRoutingNodeDefault ();
	BendDefault bendDefault = routingNodeDefault.GetBendDefault ();
	ACAPI::Result<UniqueID> elbowTableId = bendDefault.GetPreferenceTable ();
	ACAPI::Result<PipeElbowPreferenceTable> pipeElbowTable = PipeElbowPreferenceTable::Get (*elbowTableId);
	pipeElbowTable->Modify ([&] (PipeElbowPreferenceTable::Modifier& modifier) {
		modifier.SetDiameterByReferenceId (200, 0.2);
		modifier.SetRadiusByReferenceId (200, 0.2);
	}, "Set Diameter and Radius for Elbow");

	ACAPI::Result<UniqueID> branchTableId = routingElementDefault->GetBranchPreferenceTableId ();
	ACAPI::Result<PipeBranchPreferenceTable> pipeBranchTable = PipeBranchPreferenceTable::Get (*branchTableId);
	pipeBranchTable->Modify ([&] (PipeBranchPreferenceTable::Modifier& modifier) {
		modifier.AddNewRow (200, 200);
		modifier.SetMainAxisDiameterByReferenceIds (200, 200, 0.2);
		modifier.SetBranchAxisDiameterByReferenceIds (200, 200, 0.2);
		modifier.SetLengthByReferenceIds (200, 200, 0.6);
		modifier.SetBranchOffsetByReferenceIds (200, 200, 0.3);
		modifier.SetConnectionLengthByReferenceIds (200, 200, 0.012);
	}, "Set Diameter and Radius for Elbow");

	ACAPI::Result<void> commandResult = ACAPI::CallUndoableCommand ([]() {
		static const std::vector<RoutePlacementData> routes = {
			{{{-5, 5, 0}, {-2, 5, 0}}, { {}, {{ 0, 200 }}}},
			{{{-2, 5, 0}, { 0, 5, 0}}, { {}, {{ 0, 200 }}}},
			{{{ 0, 5, 0}, { 2, 5, 0}}, { {}, {{ 0, 200 }}}},
			{{{ 2, 5, 0}, { 2, 7, 0}}, { {}, {{ 0, 200 }}}},
			{{{ 0, 5, 0}, { 0, 3, 0}}, { {}, {{ 0, 200 }}}},
			{{{ 0, 3, 0}, { 0, 0, 0}}, { {}, {{ 0, 200 }}}},
			{{{ 0, 3, 0}, { 0, 3, 3}}, { {}, {{ 0, 200 }}}},
			{{{ 0, 3, 0}, { 0, 3,-3}}, { {}, {{ 0, 200 }}}}
		};

		ACAPI::Result<std::vector<UniqueID>> routeIds = PlaceRoutes (routes, Domain::Piping);
		if (routeIds.IsErr ())
			return routeIds.UnwrapErr ().kind;

		return ConnectRoutesWithTeeAndCross (*routeIds);
	}, ACAPI_GetToken (), "Place and connect Pipe Routing Elements");

	if (commandResult.IsErr ())
		return commandResult.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceAnAccessoryBetweenTwoRoutes ()
{
	ACAPI::Result<ACAPI::MEP::RoutingElementDefault> routingElementDefault = ACAPI::MEP::CreateRoutingElementDefault (ACAPI::MEP::Domain::Piping);

	std::vector<API_Coord3D> referenceCoords;
	referenceCoords.push_back ({ 0.0, 0.0, 2.0 });
	referenceCoords.push_back ({ 10.0, 0.0, 2.0 });

	ACAPI::Result<ACAPI::MEP::UniqueID> routingElemId1 = routingElementDefault->Place (referenceCoords, {}, {});
	if (routingElemId1.IsErr ())
		return routingElemId1.UnwrapErr ().kind;

	ACAPI::Result<ACAPI::MEP::RoutingElement> routingElement1 = ACAPI::MEP::RoutingElement::Get (*routingElemId1);

	ACAPI::Result<ACAPI::MEP::AccessoryDefault> accessoryDefault = ACAPI::MEP::CreateAccessoryDefault (ACAPI::MEP::Domain::Piping);
	ACAPI::Result<ACAPI::MEP::PipingPortDefault> accessoryInPortDefault = accessoryDefault->GetPipingPortDefault (0);

	accessoryInPortDefault->Modify ([&](ACAPI::MEP::PipingPortDefault::Modifier& modifier) {
		modifier.SetDiameter (routingElement1->GetRoutingSegmentDefaultParameters ().GetCrossSectionWidth ());
		modifier.SetMEPSystem (routingElementDefault->GetMEPSystem ());
	});

	ACAPI::Result<ACAPI::MEP::UniqueID> accessoryId = accessoryDefault->Place (routingElement1->GetRoutingNodeIds ().back ());
	if (accessoryId.IsErr ())
		return accessoryId.UnwrapErr ().kind;

	ACAPI::Result<ACAPI::MEP::Accessory> accessory = ACAPI::MEP::Accessory::Get (*accessoryId);
	ACAPI::Result<ACAPI::MEP::Port> accessoryOutPort = ACAPI::MEP::Port::Get (accessory->GetPortIDs ()[1]);

	referenceCoords.clear ();
	API_Coord3D point = accessoryOutPort->GetPosition ();
	referenceCoords.push_back (point);
	API_Vector3D dirV = accessoryOutPort->GetOrientation ().direction;
	point.x += 10.0 * dirV.x;
	point.y += 10.0 * dirV.y;
	point.z += 10.0 * dirV.z;
	referenceCoords.push_back (point);

	ACAPI::Result<ACAPI::MEP::UniqueID> routingElemId2 = routingElementDefault->Place (referenceCoords, {}, {});
	if (routingElemId2.IsErr ())
		return routingElemId2.UnwrapErr ().kind;

	ACAPI::Result<ACAPI::MEP::RoutingElement> routingElement2 = ACAPI::MEP::RoutingElement::Get (*routingElemId2);

	routingElement2->Modify ([&](ACAPI::MEP::RoutingElement::Modifier& modifier) {
		ACAPI::Result<ACAPI::MEP::RouteConnectionResult> connResult = modifier.ConnectLogically (*accessoryId);
	}, "Continue Accessory with a route.");

	return NoError;
}


} // namespace MEPExample