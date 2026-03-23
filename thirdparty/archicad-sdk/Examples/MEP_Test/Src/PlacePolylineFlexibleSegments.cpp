#include "PlacePolylineFlexibleSegments.hpp"

// API
#include "ACAPinc.h"
#include "API_Guid.hpp"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"
#include "ACAPI/MEPFlexibleSegment.hpp"
#include "ACAPI/MEPFlexibleSegmentDefault.hpp"
#include "ACAPI/MEPTerminal.hpp"
#include "ACAPI/MEPTerminalDefault.hpp"
#include "ACAPI/MEPPort.hpp"

#include "ACAPI/MEPRoutingElement.hpp"
#include "ACAPI/MEPRoutingElementDefault.hpp"
#include "ACAPI/MEPRoutingNode.hpp"
#include "ACAPI/MEPRoutingSegment.hpp"
#include "ACAPI/MEPRoutingSegmentDefault.hpp"


// Geometry
#include "Vector3D.hpp"


using namespace ACAPI::MEP;
using RectangularCrossSectionDataMap = std::map<UInt32, RoutingSegmentRectangularCrossSectionData>;
using CircularCrossSectionDataMap = std::map<UInt32, RoutingSegmentCircularCrossSectionData>;
using CrossSectionDataMapPair = std::pair<RectangularCrossSectionDataMap, CircularCrossSectionDataMap>;
using RoutePlacementData = std::pair<std::vector<API_Coord3D>, CrossSectionDataMapPair>;


namespace MEPExample {


namespace {


GSErrCode PlaceFlexibleSegment_BigShape1 ()
{
// ! [FlexibleSegment-Polyline-Placement-Example]
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::MEP::FlexibleSegmentPolyline polyline;

	double offsetY = 20;

	for (UIndex i = 0; i < 5; ++i) {

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ i * 4.0 + 0.0, offsetY + 0.0, 0.0 },
			{ i * 4.0 + 0.0, offsetY + 10.0, 0.0 },
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ i * 4.0 + 0.0, offsetY + 10.0, 0.0 },
			{ i * 4.0 + 1.0, offsetY + 11.0, 0.0 },
			{ i * 4.0 + 2.0, offsetY + 10.0, 0.0 }
		} });

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ i * 4.0 + 2.0, offsetY + 10.0, 0.0 },
			{ i * 4.0 + 2.0, offsetY + 0.0, 0.0 },
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ i * 4.0 + 2.0, offsetY + 0.0, 0.0 },
			{ i * 4.0 + 3.0, offsetY + -1.0, 0.0 },
			{ i * 4.0 + 4.0, offsetY + 0.0, 0.0 }
		} });

	}

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (polyline, std::nullopt, API_Vector3D { 1.0, 0.0, 0.0 }, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;
// ! [FlexibleSegment-Polyline-Placement-Example]

	return NoError;
}


GSErrCode PlaceFlexibleSegment_BigShape1_Vertical ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::MEP::FlexibleSegmentPolyline polyline;

	double offsetY = 20;

	for (UIndex i = 0; i < 5; ++i) {

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ i * 4.0 + 0.0, 0.0, offsetY + 0.0 },
			{ i * 4.0 + 0.0, 0.0, offsetY + 10.0 },
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ i * 4.0 + 0.0, 0.0, offsetY + 10.0 },
			{ i * 4.0 + 1.0, 0.0, offsetY + 11.0 },
			{ i * 4.0 + 2.0, 0.0, offsetY + 10.0 }
		} });

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ i * 4.0 + 2.0, 0.0, offsetY + 10.0 },
			{ i * 4.0 + 2.0, 0.0, offsetY + 0.0 },
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ i * 4.0 + 2.0, 0.0, offsetY + 0.0 },
			{ i * 4.0 + 3.0, 0.0, offsetY + -1.0 },
			{ i * 4.0 + 4.0, 0.0, offsetY + 0.0 }
		} });

	}

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (polyline, std::nullopt, API_Vector3D { 1.0, 0.0, 0.0 }, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceFlexibleSegment_BigShape2 ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::MEP::FlexibleSegmentPolyline polyline;

	auto AddInwards = [&] (const double offsetX , const double offsetY, const double width, const double height, double turnSize) {

		// up
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + 0.0, offsetY + 0.0, 0.0 },
			{ offsetX + 0.0, offsetY + height - turnSize * 1, 0.0 }
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + 0.0, offsetY + height - turnSize * 1, 0.0 },
			{ offsetX + turnSize * 1, offsetY + height + 0.0, 0.0 }
		} });

		// right
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + turnSize * 1, offsetY + height + 0.0, 0.0 },
			{ offsetX + width + 0.0, offsetY + height + 0.0, 0.0 }
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + width + 0.0, offsetY + height + 0.0, 0.0 },
			{ offsetX + width + turnSize * 1, offsetY + height - turnSize * 1, 0.0 },
		} });

		// down
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + width + turnSize * 1, offsetY + height - turnSize * 1, 0.0 },
			{ offsetX + width + turnSize * 1, offsetY + turnSize * 1, 0.0 }
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + width + turnSize * 1, offsetY + turnSize * 1, 0.0 },
			{ offsetX + width + 0.0, offsetY + 0.0, 0.0 },
		} });

		// left
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + width + 0.0, offsetY + 0.0, 0.0 },
			{ offsetX + turnSize * 2, offsetY + 0.0, 0.0 }
		});

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + turnSize * 2, offsetY + 0.0, 0.0 },
			{ offsetX + turnSize * 1, offsetY + turnSize * 1, 0.0 },
		} });
	};

	auto AddOutwards = [&] (const double offsetX , const double offsetY, const double width, const double height, double turnSize) {

		// left

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + turnSize * 1, offsetY + turnSize * 1, 0.0 },
			{ offsetX + turnSize * 2, offsetY + 0.0, 0.0 },
		} });
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + turnSize * 2, offsetY + 0.0, 0.0 },
			{ offsetX + width + 0.0, offsetY + 0.0, 0.0 }
		});

		// down

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + width + 0.0, offsetY + 0.0, 0.0 },
			{ offsetX + width + turnSize * 1, offsetY + turnSize * 1, 0.0 },
		} });
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + width + turnSize * 1, offsetY + turnSize * 1, 0.0 },
			{ offsetX + width + turnSize * 1, offsetY + height - turnSize * 1, 0.0 }
		});

		// right

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + width + turnSize * 1, offsetY + height - turnSize * 1, 0.0 },
			{ offsetX + width + 0.0, offsetY + height + 0.0, 0.0 },
		} });
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + width + 0.0, offsetY + height + 0.0, 0.0 },
			{ offsetX + turnSize * 1, offsetY + height + 0.0, 0.0 }
		});

		// up

		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
			{ offsetX + turnSize * 1, offsetY + height + 0.0, 0.0 },
			{ offsetX + 0.0, offsetY + height - turnSize * 1, 0.0 }
		} });
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			{ offsetX + 0.0, offsetY + height - turnSize * 1, 0.0 },
			{ offsetX + 0.0, offsetY + 0.0, 0.0 }
		});
	};

	double turnSize = 1.0;
		
	AddInwards (0.0, 0.0, 10.0, 10.0, turnSize);
	AddInwards (1.0, 1.0, 8.0, 8.0, turnSize);
	AddInwards (2.0, 2.0, 6.0, 6.0, turnSize);
	AddInwards (3.0, 3.0, 4.0, 4.0, turnSize);
		
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		{ 4.0, 4.0, 0.0 },
		{ 4.0, 5.0, 0.0 },
	});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { std::vector<API_Coord3D> {
		{ 4.0, 5.0, 0.0 },
		{ 4.25, 5.25, 0.0 },
		{ 4.5, 5.0, 0.0 },
	} });
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		{ 4.5, 5.0, 0.0 },
		{ 4.5, 4.5, 0.0 },
	});

	AddOutwards (3.5, 3.5, 3.0, 3.0, turnSize);
	AddOutwards (2.5, 2.5, 5.0, 5.0, turnSize);
	AddOutwards (1.5, 1.5, 7.0, 7.0, turnSize);
	AddOutwards (0.5, 0.5, 9.0, 9.0, turnSize);

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (polyline, std::nullopt, std::nullopt, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}



static ACAPI::MEP::FlexibleSegmentPolyline GetValidPolylineGeometry_SplineLine ()
{
	ACAPI::MEP::FlexibleSegmentPolyline polyline;
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
		API_Coord3D { -10.0 + 0, 10, 0 },
		API_Coord3D { -10.0 + 1, 12, 0 },
		API_Coord3D { -10.0 + 4, 10, 0 },
	}});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		API_Coord3D { -10.0 + 4, 10, 0 },
		API_Coord3D { -10.0 + 5, 10, 0 }
	});
	return polyline;
}


static ACAPI::MEP::FlexibleSegmentPolyline GetValidPolylineGeometry_SplineLineSpline ()
{
	ACAPI::MEP::FlexibleSegmentPolyline polyline;
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
		API_Coord3D { -10.0 + 0, 20, 0 },
		API_Coord3D { -10.0 + 1, 22, 0 },
		API_Coord3D { -10.0 + 4, 20, 0 },
	}});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		API_Coord3D { -10.0 + 4, 20, 0 },
		API_Coord3D { -10.0 + 5, 20, 0 }
	});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
		API_Coord3D { -10.0 + 5, 20, 0 },
		API_Coord3D { -10.0 + 6, 22, 0 },
		API_Coord3D { -10.0 + 9, 20, 0 },
	}});
	return polyline;
}


static ACAPI::MEP::FlexibleSegmentPolyline GetValidPolylineGeometry_LineSpline ()
{
	ACAPI::MEP::FlexibleSegmentPolyline polyline;
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		API_Coord3D { -10.0 + 0, 30, 0 },
		API_Coord3D { -10.0 + 1, 31, 0 }
	});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
		API_Coord3D { -10.0 + 1, 31, 0 },
		API_Coord3D { -10.0 + 5, 33, 0 },
		API_Coord3D { -10.0 + 7, 32, 0 },
	}});
	return polyline;
}


static ACAPI::MEP::FlexibleSegmentPolyline GetValidPolylineGeometry_LineSplineLine ()
{
	ACAPI::MEP::FlexibleSegmentPolyline polyline;
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		API_Coord3D { -10.0 + 0, 40, 0 },
		API_Coord3D { -10.0 + 1, 41, 0 }
	});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
		API_Coord3D { -10.0 + 1, 41, 0 },
		API_Coord3D { -10.0 + 5, 45, 0 },
		API_Coord3D { -10.0 + 2, 42, 0 },
	}});
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		API_Coord3D { -10.0 + 2, 42, 0 },
		API_Coord3D { -10.0 + 5, 42, 0 }
	});
	return polyline;
}


static ACAPI::MEP::FlexibleSegmentPolyline GetValidPolylineGeometry_Spline ()
{
	ACAPI::MEP::FlexibleSegmentPolyline polyline;
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
		API_Coord3D { -10.0 + 5, 0, 0 },
		API_Coord3D { -10.0 + 6, 2, 0 },
		API_Coord3D { -10.0 + 9, 0, 0 },
	}});
	return polyline;
}


static ACAPI::MEP::FlexibleSegmentPolyline GetValidPolylineGeometry_Line ()
{
	ACAPI::MEP::FlexibleSegmentPolyline polyline;
	polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
		API_Coord3D { -10.0 + 4, 0, 0 },
		API_Coord3D { -10.0 + 5, 0, 0 }
	});
	return polyline;
}


GSErrCode PlaceFlexibleSegment_SplineLine ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (GetValidPolylineGeometry_SplineLine (), API_Vector3D { -1.0, 0.0, 0.0 }, std::nullopt, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceFlexibleSegment_LineSpline ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (GetValidPolylineGeometry_LineSpline (), std::nullopt, API_Vector3D { 1.0, 1.0, 0.0 }, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceFlexibleSegment_LineSplineLine ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (GetValidPolylineGeometry_LineSplineLine (), std::nullopt, std::nullopt, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceFlexibleSegment_SplineLineSpline ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (GetValidPolylineGeometry_SplineLineSpline (), API_Vector3D { -1.0, 0.0, 0.0 } , API_Vector3D { 1.0, 0.0, 0.0 }, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceFlexibleSegment_Spline ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (GetValidPolylineGeometry_Spline (), API_Vector3D { -1.0, 0.0, 0.0 } , API_Vector3D { 1.0, 0.0, 0.0 }, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


GSErrCode PlaceFlexibleSegment_Line ()
{
	auto flexiblePipe = CreateFlexibleSegmentDefault (Domain::Piping);
	if (flexiblePipe.IsErr ())
		return flexiblePipe.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> flexibleSegment = flexiblePipe->PlacePolyline (GetValidPolylineGeometry_Line (), std::nullopt, std::nullopt, GenerateAPIGuid ());
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	return NoError;
}


ACAPI::Result<std::vector<UniqueID>> PlaceRoutes (const std::vector<RoutePlacementData>& placementData, Domain domain)
{
	std::vector<UniqueID> routeIds;
	routeIds.reserve (placementData.size ());

	ACAPI::Result<RoutingElementDefault> routingElementDefault = CreateRoutingElementDefault (domain);
	if (routingElementDefault.IsErr ())
		return { routingElementDefault.UnwrapErr (), ACAPI_GetToken () };

	for (const auto& [referenceCoords, crossSectionDataMaps] : placementData) {
		ACAPI::Result<UniqueID> routingElemId = routingElementDefault->Place (referenceCoords, crossSectionDataMaps.first, crossSectionDataMaps.second);
		if (routingElemId.IsErr ())
			return { routingElemId.UnwrapErr (), ACAPI_GetToken () };

		routeIds.emplace_back (*routingElemId);
	}

	return ACAPI::Ok (std::move (routeIds));
}


GSErrCode PlaceFlexibleSegment_ConnectToRouteAndTerminal (ACAPI::MEP::FlexibleSegmentPolyline polyline, std::optional<API_Vector3D> endDirectionVector, API_Vector3D terminalPosition, API_Vector3D routeStartPosition)
{
	auto ductTerminalDefault = CreateTerminalDefault (Domain::Piping);
	if (ductTerminalDefault.IsErr ())
		return ductTerminalDefault.UnwrapErr ().kind;

	ACAPI::Result<UniqueID> terminalId = ductTerminalDefault->Place (terminalPosition, { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } });
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
		modifier.SetWidth (0.35);
	}, "Set Terminal Port size.");

	auto ductFlexibleSegmentDefault = CreateFlexibleSegmentDefault (Domain::Piping);
	if (ductFlexibleSegmentDefault.IsErr ())
		return ductFlexibleSegmentDefault.UnwrapErr ().kind;

	ACAPI::MEP::Orientation terminalPortOrientation = terminalPort->GetOrientation ();

	ACAPI::MEP::Orientation flexibleSegemntStartOrientation = terminalPortOrientation;
	flexibleSegemntStartOrientation.direction.x *= -1.0;
	flexibleSegemntStartOrientation.direction.y *= -1.0;
	flexibleSegemntStartOrientation.direction.z *= -1.0;

	ACAPI::Result<UniqueID> flexibleSegmentId = ductFlexibleSegmentDefault->PlacePolyline (polyline, flexibleSegemntStartOrientation.direction, endDirectionVector);
	if (flexibleSegmentId.IsErr ())
		return flexibleSegmentId.UnwrapErr ().kind;

	ACAPI::Result<FlexibleSegment> flexibleSegment = FlexibleSegment::Get (*flexibleSegmentId);
	if (flexibleSegment.IsErr ())
		return flexibleSegment.UnwrapErr ().kind;

	ACAPI::Result<Port> flexibleSegmentPort = Port::Get (flexibleSegment->GetPortIDs ()[0]);
	if (flexibleSegmentPort.IsErr ())
		return flexibleSegmentPort.UnwrapErr ().kind;

	flexibleSegmentPort->Modify ([&](Port::Modifier& modifier) {
		modifier.SetShape (ConnectorShape::Circular);
		modifier.SetWidth (0.35);

		ACAPI::Result<Port> flexibleSegmentPort2 = Port::Get (flexibleSegment->GetPortIDs ()[1]);
		flexibleSegmentPort2->Modify ([&](Port::Modifier& modifier) {
			modifier.SetShape (ConnectorShape::Circular);
			modifier.SetWidth (0.35);
		}, "Set FlexibleSegment Port2 size.");
	}, "Set FlexibleSegment Port size.");


	std::vector<API_Coord3D> referenceCoords;
	referenceCoords.push_back (routeStartPosition);
	referenceCoords.push_back ({ routeStartPosition.x + 5.0, routeStartPosition.y, routeStartPosition.z });
	
	std::map<UInt32, ACAPI::MEP::RoutingSegmentRectangularCrossSectionData> rectangularCrossSectionData;
	std::map<UInt32, ACAPI::MEP::RoutingSegmentCircularCrossSectionData> circularCrossSectionData;

	circularCrossSectionData.emplace (0, ACAPI::MEP::RoutingSegmentCircularCrossSectionData (20));

	ACAPI::Result<std::vector<UniqueID>> routingElementId = PlaceRoutes ({ { referenceCoords, std::make_pair (rectangularCrossSectionData, circularCrossSectionData) } }, Domain::Piping);
	if (routingElementId.IsErr ())
		return routingElementId.UnwrapErr ().kind;

	ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get ((*routingElementId)[0]);
	if (routingElement.IsErr ())
		return routingElement.UnwrapErr ().kind;

	routingElement->Modify ([&](RoutingElement::Modifier& modifier) -> GSErrCode {
		ACAPI::Result<RouteConnectionResult> connectionResult = modifier.ConnectLogically (*flexibleSegmentId);
		if (connectionResult.IsErr ())
			return connectionResult.UnwrapErr ().kind;

		return NoError;
	}, "Connect RoutingElement to FlexibleSegment.");

	return NoError;
}


} // namespace


GSErrCode PlacePolylineFlexibleSegments ()
{
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_BigShape1 ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_BigShape1_Vertical ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_BigShape2 ());

	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_LineSpline ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_SplineLine ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_LineSplineLine ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_SplineLineSpline ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_Line ());
	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_Spline ());

	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_ConnectToRouteAndTerminal ([] () {
		ACAPI::MEP::FlexibleSegmentPolyline polyline;
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
			API_Coord3D { 20.0, 0.0, 0.0 },
			API_Coord3D { 23.0, -2.0, 0.0 },
			API_Coord3D { 24.0, 0.0, 0.0 },
		}});
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			API_Coord3D { 24.0, 0.0, 0.0 },
			API_Coord3D { 27.0, 0.0, 0.0 }
		});
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
			API_Coord3D { 27.0, 0.0, 0.0 },
			API_Coord3D { 28.0, 2.0, 0.0 },
			API_Coord3D { 30.0, 0.0, 0.0 },
		}});
		return polyline;
		}(), API_Vector3D { 1.0, 0.0, 0.0 }, API_Vector3D { 20.0, 0.0, 0.0 }, API_Vector3D { 30.0, 0.0, 0.0 } ));

	ERRCHK_NO_ASSERT (PlaceFlexibleSegment_ConnectToRouteAndTerminal ([] () {
		ACAPI::MEP::FlexibleSegmentPolyline polyline;
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineSplinePart { {
			API_Coord3D { 20.0, 10.0, 0.0 },
			API_Coord3D { 22.0, 12.0, 0.0 },
			API_Coord3D { 24.0, 8.0, 0.0 },
			API_Coord3D { 25.0, 10.0, 0.0 },
		}});
		polyline.push_back (ACAPI::MEP::FlexibleSegmentPolylineLinePart {
			API_Coord3D { 25.0, 10.0, 0.0 },
			API_Coord3D { 30.0, 10.0, 0.0 }
		});
		return polyline;
	}(), std::nullopt, API_Vector3D { 20.0, 10.0, 0.0 }, API_Vector3D { 30.0, 10.0, 0.0 } ));


	return NoError;
}


} // namepspace MEPExample
