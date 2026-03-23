#include "QueryElementsAndDefaults.hpp"
#include "Reporter.hpp"

// API
#include "ACAPinc.h"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"

#include "ACAPI/MEPElement.hpp"

#include "ACAPI/MEPPort.hpp"
#include "ACAPI/MEPVentilationPort.hpp"
#include "ACAPI/MEPPipingPort.hpp"

#include "ACAPI/MEPRoutingElement.hpp"
#include "ACAPI/MEPRoutingElementDefault.hpp"
#include "ACAPI/MEPRoutingSegment.hpp"
#include "ACAPI/MEPRoutingSegmentDefault.hpp"
#include "ACAPI/MEPRigidSegment.hpp"
#include "ACAPI/MEPRigidSegmentDefault.hpp"
#include "ACAPI/MEPRoutingNode.hpp"
#include "ACAPI/MEPRoutingNodeDefault.hpp"
#include "ACAPI/MEPBend.hpp"
#include "ACAPI/MEPBendDefault.hpp"
#include "ACAPI/MEPTransition.hpp"
#include "ACAPI/MEPTransitionDefault.hpp"

#include "ACAPI/MEPBranch.hpp"
#include "ACAPI/MEPTerminal.hpp"
#include "ACAPI/MEPFlexibleSegment.hpp"

using namespace ACAPI::MEP;


namespace MEPExample {


namespace {


void WriteElemHeadDetails (const API_Element& elem) 
{
	Reporter reporter;

	GS::UniString renovationStatus;
	ACAPI_Renovation_GetRenovationStatusName (elem.header.renovationStatus, &renovationStatus);

	reporter.SetTabCount (1);
	reporter.Add ("Guid", APIGuidToString (elem.header.guid));
	reporter.Add ("Floor index", static_cast<Int32> (elem.header.floorInd));
	reporter.Add ("GroupId", APIGuidToString (elem.header.groupGuid));
	reporter.Add ("Modistamp", static_cast<Int32> (elem.header.modiStamp));
	reporter.Add ("Layer", elem.header.layer.ToInt32_Deprecated ());
	reporter.Add ("Renovation Status", renovationStatus);

	reporter.AddNewLine ();
	reporter.Write ();
}


void WriteElementBaseDetails (const ElementBase& mepElem)
{
	Reporter elementBaseReporter;

	Orientation orientation = mepElem.GetOrientation ();

	elementBaseReporter.Add ("Id", mepElem.GetElemId ());
	elementBaseReporter.Add ("Anchor point", mepElem.GetAnchorPoint ());
	elementBaseReporter.Add ("Orientation - direction vector", orientation.direction);
	elementBaseReporter.Add ("Orientation - rotation vector", orientation.rotation);

	elementBaseReporter.AddNewLine ();
	elementBaseReporter.Write ();
}


void WritePortDetails (const ElementBase& mepElem)
{
// ! [Port-Getters-Example]
	Reporter portReporter;

	portReporter.Add ("Ports:");
	portReporter.SetTabCount (1);

	for (const UniqueID& portID : mepElem.GetPortIDs ()) {
		portReporter.SetTabCount (1);

		Port::Get (portID).Then ([&](const Port& port) {
			portReporter.Add ("Port name", port.GetName ());
			portReporter.Add ("Port width", port.GetWidth ());
			portReporter.Add ("Port height", port.GetHeight ());
			portReporter.Add ("Port position", port.GetPosition ());
			portReporter.Add ("Port domain", port.GetDomain ());
			portReporter.Add ("Port shape", port.GetShape ());
			portReporter.Add ("Port Orientation - direction vector", port.GetOrientation ().direction);
			portReporter.Add ("Port Orientation - rotation vector", port.GetOrientation ().rotation);
			portReporter.Add ("Port MEP System", port.GetMEPSystem ());
			port.IsPhysicallyConnected () ? portReporter.Add ("Port is connected with another port.") : portReporter.Add ("Port is not connected with another port.");

			if (port.IsPhysicallyConnected ()) {

				Port::Get (*port.GetConnectedPortId ()).Then ([&](const Port& port) {
					portReporter.Add ("Connected port's name", port.GetName ());
				});

				portReporter.Add ("Details about connected element:");
				portReporter.SetTabCount (2);

				Element::Get (*port.GetConnectedMEPElementId ()).Then ([&](const Element& element) {
					Orientation orientation = element.GetOrientation ();

					portReporter.Add ("Id", element.GetElemId ());
					portReporter.Add ("Anchor point", element.GetAnchorPoint ());
					portReporter.Add ("Orientation - direction vector", orientation.direction);
					portReporter.Add ("Orientation - rotation vector", orientation.rotation);
				});
			}
		});

		VentilationPort::Get (portID).Then ([&](const VentilationPort& port) {
			portReporter.Add ("Port wall thickness", port.GetWallThickness ());
			portReporter.Add ("Port flow direction", port.GetFlowDirection ());
		});

		PipingPort::Get (portID).Then ([&](const PipingPort& port) {
			portReporter.Add ("Port wall thickness", port.GetWallThickness ());
			portReporter.Add ("Port diameter", port.GetOuterDiameter ());
			portReporter.Add ("Port flow direction", port.GetFlowDirection ());
		});

		portReporter.Add ("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -");
	}

	portReporter.Write ();
// ! [Port-Getters-Example]
}


void WriteBranchDetails (const UniqueID& id)
{
// ! [Branch-Getters-Example]
	ACAPI::Result<Branch> branch = Branch::Get (id);
	if (branch.IsErr ()) {
		ACAPI_WriteReport (branch.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter branchReporter;
	branchReporter.Add ("Branch details");
	branchReporter.SetTabCount (1);

	branchReporter.Add ("Main axis length", branch->GetLength ());
	branchReporter.Add ("Branch length", branch->GetBranchLength ());
	branchReporter.Add ("Branch angle", branch->GetBranchAngle ());
	branchReporter.Add ("Branch offset", branch->GetBranchOffset ());

	std::optional<double> insulationThickness = branch->GetInsulationThickness ();
	insulationThickness.has_value () ? branchReporter.Add ("It has insulation. Thickness", *insulationThickness) : branchReporter.Add ("It has no insulation");

	branchReporter.Write ();
// ! [Branch-Getters-Example]
}


void WriteBendDetails (const UniqueID& id)
{
// ! [Bend-Getters-Example]
	ACAPI::Result<Bend> bend = Bend::Get (id);
	if (bend.IsErr ()) {
		ACAPI_WriteReport (bend.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter bendReporter (3);

	bendReporter.Add ("Bend id", id);

	bendReporter.SetTabCount (4);
	ACAPI::Result<double> factorRadius = bend->GetFactorRadius ();
	if (factorRadius.IsOk ())
		bendReporter.Add ("Factor Radius", *factorRadius);
	bendReporter.Add ("Radius", bend->GetRadius ());
	bendReporter.Add ("Width", bend->GetWidth ());
	bendReporter.Add ("Height", bend->GetHeight ());
	bendReporter.Add ("Shape", bend->GetShape ());
	std::optional<double> insulation = bend->GetInsulationThickness ();
	insulation.has_value () ? bendReporter.Add ("Insulation thickness", *insulation) : bendReporter.Add ("Does not have insulation.");

	bendReporter.Write ();
// ! [Bend-Getters-Example]
}


void WriteTransitionDetails (const UniqueID& id)
{
// ! [Transition-Getters-Example]
	ACAPI::Result<Transition> transition = Transition::Get (id);
	if (transition.IsErr ()) {
		ACAPI_WriteReport (transition.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter transitionReporter (3);

	transitionReporter.Add ("Transition id", id);

	transitionReporter.SetTabCount (4);
	transitionReporter.Add ("Angle", transition->GetAngle ());
	transitionReporter.Add ("Length", transition->GetLength ());
	transitionReporter.Add ("Offset Y", transition->GetOffsetY ());
	transitionReporter.Add ("Offset Z", transition->GetOffsetZ ());
	transitionReporter.Add ("Id of the narrower Port", transition->GetNarrowerPortID ());
	transitionReporter.Add ("Id of the wider Port", transition->GetWiderPortID ());

	transitionReporter.Write ();
// ! [Transition-Getters-Example]
}


void WriteRoutingNodeDetails (const UniqueID& id)
{
// ! [RoutingNode-Getters-Example]
	ACAPI::Result<RoutingNode> routingNode = RoutingNode::Get (id);
	if (routingNode.IsErr ()) {
		ACAPI_WriteReport (routingNode.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter routingNodeReporter (1);

	routingNodeReporter.Add ("Routing Node id", id);
	routingNodeReporter.SetTabCount (2);

	routingNodeReporter.Add ("Position", routingNode->GetPosition ());
	routingNodeReporter.Add ("Preferred Transition Placement", routingNode->GetPreferredTransitionPlacement ());

	std::optional<UniqueID> incomingSegmentId = routingNode->GetIncomingSegmentId ();
	incomingSegmentId.has_value () ? routingNodeReporter.Add ("Id of Incoming Segment", *incomingSegmentId) : routingNodeReporter.Add ("Does not have Incoming Segment.");

	std::optional<UniqueID> outgoingSegmentId = routingNode->GetOutgoingSegmentId ();
	outgoingSegmentId.has_value () ? routingNodeReporter.Add ("Id of Outgoing Segment", *outgoingSegmentId) : routingNodeReporter.Add ("Does not have Outgoing Segment.");

	routingNodeReporter.Write ();

	// Bends
	std::vector<UniqueID> bendIds = routingNode->GetBendIds ();
	if (!bendIds.empty ()) {
		ACAPI_WriteReport ("\t\tList of Bends:\n", false);
		for (const UniqueID& id : bendIds)
			WriteBendDetails (id);
	}

	// Transitions
	std::vector<UniqueID> transitionIds = routingNode->GetTransitionIds ();
	if (!transitionIds.empty ()) {
		ACAPI_WriteReport ("\t\tList of Transitions:\n", false);
		for (const UniqueID& id : transitionIds)
			WriteTransitionDetails (id);
	}
// ! [RoutingNode-Getters-Example]
}


void WriteRigidSegmentDetails (const UniqueID& id)
{
// ! [RigidSegment-Getters-Example]
	ACAPI::Result<RigidSegment> rigidSegment = RigidSegment::Get (id);
	if (rigidSegment.IsErr ()) {
		ACAPI_WriteReport (rigidSegment.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter rigidSegmentReporter (3);

	rigidSegmentReporter.Add ("RigidSegment id", id);

	rigidSegmentReporter.SetTabCount (4);
	rigidSegmentReporter.Add ("Length", rigidSegment->GetLength ());
	rigidSegmentReporter.Add ("Width", rigidSegment->GetWidth ());
	rigidSegmentReporter.Add ("Height", rigidSegment->GetHeight ());

	rigidSegmentReporter.Write ();
// ! [RigidSegment-Getters-Example]
}


void WriteRoutingSegmentDetails (const UniqueID& id)
{
// ! [RoutingSegment-Getters-Example]
	ACAPI::Result<RoutingSegment> routingSegment = RoutingSegment::Get (id);
	if (routingSegment.IsErr ()) {
		ACAPI_WriteReport (routingSegment.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter routingSegmentReporter (1);

	routingSegmentReporter.Add ("Routing Segment", id);
	routingSegmentReporter.SetTabCount (2);

	ConnectorShape shape = routingSegment->GetCrossSectionShape ();

	routingSegmentReporter.Add ("Table of dimensions ID", routingSegment->GetPreferenceTableId ());
	if (shape == ConnectorShape::Circular)
		routingSegmentReporter.Add ("Reference ID", *routingSegment->GetCrossSectionReferenceId ());

	routingSegmentReporter.Add ("Cross Section Shape", shape);
	routingSegmentReporter.Add ("Cross Section Width", routingSegment->GetCrossSectionWidth ());
	routingSegmentReporter.Add ("Cross Section Height", routingSegment->GetCrossSectionHeight ());
	routingSegmentReporter.Add ("Wall thickness", routingSegment->GetWallThickness ());

	routingSegmentReporter.Add ("Anchor", routingSegment->GetAnchor ());
	routingSegmentReporter.Add ("Offset X", routingSegment->GetReferenceLineOffsetX ());
	routingSegmentReporter.Add ("Offset Y", routingSegment->GetReferenceLineOffsetY ());

	ConnectionStyle style = routingSegment->GetConnectionStyle ();
	routingSegmentReporter.Add ("Connection style", style);

	if (style == ConnectionStyle::Flanged)
		routingSegmentReporter.Add (shape == ConnectorShape::Circular ? "Flange diameter" : "Flange width", *routingSegment->GetFlangeSize ());

	routingSegmentReporter.Add ("Guid of Begin Node", routingSegment->GetBeginNodeId ());
	routingSegmentReporter.Add ("Guid of End Node", routingSegment->GetEndNodeId ());

	routingSegmentReporter.Write ();

	// Rigid Segments
	std::vector<UniqueID> rigidSegmentIds = routingSegment->GetRigidSegmentIds ();
	if (!rigidSegmentIds.empty ()) {
		ACAPI_WriteReport ("\t\tList of Rigid Segments:\n", false);
		for (const UniqueID& id : rigidSegmentIds)
			WriteRigidSegmentDetails (id);
	}
// ! [RoutingSegment-Getters-Example]
}


void WriteRoutingElementDetails (const UniqueID& id)
{
// ! [RoutingElement-Getters-Example]
	ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get (id);
	if (routingElement.IsErr ()) {
		ACAPI_WriteReport (routingElement.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter routingElementReporter;

	Domain domain = routingElement->GetDomain ();
	routingElementReporter.Add ("Domain", domain);

	std::vector<API_Coord3D> polyLine = routingElement->GetPolyLine ();
	routingElementReporter.Add ("Polyline of the routing element:");

	routingElementReporter.SetTabCount (1);
	for (const API_Coord3D& node : polyLine)
		routingElementReporter.Add (node);

	routingElementReporter.SetTabCount (0);
	if (domain != Domain::CableCarrier)
		routingElementReporter.Add ("Branch Preference Table Id", *routingElement->GetBranchPreferenceTableId ());

	routingElementReporter.Add ("Offset from Home Story", routingElement->GetOffsetFromHomeStory ());

	if (routingElement->GetMEPSystem () == APIInvalidAttributeIndex) {
		routingElementReporter.Add ("MEP System", GS::UniString { "Undefined" });
	} else {
		API_Attribute attribute = {};
		attribute.header.typeID = API_MEPSystemID;
		attribute.header.index = routingElement->GetMEPSystem ();
		const GSErrCode err = ACAPI_Attribute_Get (&attribute);
		if DBVERIFY (err == NoError)
			routingElementReporter.Add ("MEP System", GS::UniString { attribute.header.name });
	}

	std::optional<UniqueID> portOnBegin = routingElement->GetLogicallyConnectedPortIdAtRouteBegin ();
	portOnBegin.has_value () ? routingElementReporter.Add ("Logically connected Port Id at begin", *portOnBegin) : routingElementReporter.Add ("Unconnected begin node.");

	std::optional<UniqueID> portOnEnd = routingElement->GetLogicallyConnectedPortIdAtRouteEnd ();
	portOnEnd.has_value () ? routingElementReporter.Add ("Logically connected Port Id at end", *portOnEnd) : routingElementReporter.Add ("Unconnected end node.");
// ! [RoutingElement-Getters-Example]

// ! [RoutingElementDefault-Pickup-and-Getters-Example]
	// Write Defaults
	RoutingElementDefault routingElementDefault = routingElement->PickUpDefault ();

	// Default Routing Segment
	routingElementReporter.AddNewLine ();
	routingElementReporter.Add ("Default Routing Segment:");
	routingElementReporter.SetTabCount (1);

	RoutingSegmentDefault routingSegmentDefault = routingElementDefault.GetRoutingSegmentDefault ();

	ConnectorShape shape = routingSegmentDefault.GetCrossSectionShape ();

	routingElementReporter.Add ("Table of dimensions ID", routingSegmentDefault.GetPreferenceTableId ());
	if (shape == ConnectorShape::Circular)
		routingElementReporter.Add ("Reference ID", *routingSegmentDefault.GetCrossSectionReferenceId ());

	routingElementReporter.Add ("Cross Section Shape", shape);
	routingElementReporter.Add ("Cross Section Width", routingSegmentDefault.GetCrossSectionWidth ());
	routingElementReporter.Add ("Cross Section Height", routingSegmentDefault.GetCrossSectionHeight ());
	routingElementReporter.Add ("Wall thickness", routingSegmentDefault.GetWallThickness ());

	routingElementReporter.Add ("Anchor", routingSegmentDefault.GetAnchor ());
	routingElementReporter.Add ("Offset X", routingSegmentDefault.GetReferenceLineOffsetX ());
	routingElementReporter.Add ("Offset Y", routingSegmentDefault.GetReferenceLineOffsetY ());

	ConnectionStyle style = routingSegmentDefault.GetConnectionStyle ();
	routingElementReporter.Add ("Connection style", style);

	if (style == ConnectionStyle::Flanged)
		routingElementReporter.Add (shape == ConnectorShape::Circular ? "Flange diameter" : "Flange width", *routingSegmentDefault.GetFlangeSize ());

	routingElementReporter.AddNewLine ();
	routingElementReporter.Add ("Default Rigid Segment:");
	routingElementReporter.SetTabCount (2);

	RigidSegmentDefault rigidSegmentDefault = routingSegmentDefault.GetRigidSegmentDefault ();
	routingElementReporter.Add ("Width", rigidSegmentDefault.GetWidth ());
	routingElementReporter.Add ("Height", rigidSegmentDefault.GetHeight ());
	routingElementReporter.Add ("Shape", rigidSegmentDefault.GetShape ());

	// Default Routing Node
	routingElementReporter.SetTabCount (0);
	routingElementReporter.AddNewLine ();
	routingElementReporter.Add ("Default Routing Node:");
	routingElementReporter.SetTabCount (1);

	RoutingNodeDefault routingNodeDefault = routingElementDefault.GetRoutingNodeDefault ();
	routingElementReporter.Add ("Preferred Transition Placement", routingNodeDefault.GetPreferredTransitionPlacement ());

	routingElementReporter.AddNewLine ();
	routingElementReporter.Add ("Default Bend:");
	routingElementReporter.SetTabCount (2);

	BendDefault bendDefault = routingNodeDefault.GetBendDefault ();
	if (bendDefault.GetDomain () != Domain::Piping)
		routingElementReporter.Add ("Factor Radius", *bendDefault.GetFactorRadius ());

	routingElementReporter.SetTabCount (1);
	routingElementReporter.AddNewLine ();
	routingElementReporter.Add ("Default Transition:");
	routingElementReporter.SetTabCount (2);

	TransitionDefault transitionDefault = routingNodeDefault.GetTransitionDefault ();
	if (transitionDefault.GetDomain () != Domain::Piping)
		routingElementReporter.Add ("Angle", *transitionDefault.GetAngle ());

	routingElementReporter.Add ("Offset Y", transitionDefault.GetOffsetY ());
	routingElementReporter.Add ("Offset Z", transitionDefault.GetOffsetZ ());

	routingElementReporter.Write ();
// ! [RoutingElementDefault-Pickup-and-Getters-Example]

// ! [RoutingElement-Getters-Example2]
	// Write informations about placed Routing Segments
	std::vector<UniqueID> segmentIds = routingElement->GetRoutingSegmentIds ();
	ACAPI_WriteReport ("List of Routing Segments:\n", false);
	for (const UniqueID& id : segmentIds)
		WriteRoutingSegmentDetails (id);

	// Write informations about placed Routing Nodes
	std::vector<UniqueID> nodeIds = routingElement->GetRoutingNodeIds ();
	ACAPI_WriteReport ("List of Routing Nodes:\n", false);
	for (const UniqueID& id : nodeIds)
		WriteRoutingNodeDetails (id);
// ! [RoutingElement-Getters-Example2]
}


void WriteFlexibleSegmentDetails (const UniqueID& id)
{
// ! [FlexibleSegment-Getters-Example]
	ACAPI::Result<FlexibleSegment> flexibleSegment = FlexibleSegment::Get (id);
	if (flexibleSegment.IsErr ()) {
		ACAPI_WriteReport (flexibleSegment.UnwrapErr ().text.c_str (), false);
		return;
	}

	Reporter reporter;
	reporter.SetTabCount (0);
	reporter.Add ("Control points:");
	reporter.SetTabCount (1);
	ACAPI::Result<std::vector<API_Coord3D>> controlPoints = flexibleSegment->GetControlPoints ();
	if (controlPoints.IsErr ()) {
		ACAPI_WriteReport (controlPoints.UnwrapErr ().text.c_str (), false);
		return;
	}
	for (const API_Coord3D& controlPoint : *controlPoints)
		reporter.Add (ToString (controlPoint));

	reporter.AddNewLine ();
// ! [FlexibleSegment-Getters-Example]

// ! [FlexibleSegmentDefault-Pickup-and-Getters-Example]
	// Write Defaults
	FlexibleSegmentDefault flexibleSegmentDefault = flexibleSegment->PickUpDefault ();

	reporter.SetTabCount (0);
	reporter.Add ("Default Flexible Segment:");
	reporter.SetTabCount (1);
	reporter.Add ("Width", flexibleSegmentDefault.GetWidth ());
	reporter.Add ("Height", flexibleSegmentDefault.GetHeight ());
	reporter.Add ("Shape", flexibleSegmentDefault.GetShape ());
	reporter.Add ("Domain", flexibleSegmentDefault.GetDomain ());

	reporter.Write ();
// ! [FlexibleSegmentDefault-Pickup-and-Getters-Example]
}


// ! [ElementDefault-Getters-Example2]
void WriteElementDefaultDetails (const ElementDefault& mepElemDefault)
{
	Reporter elementDefaultReporter;

	elementDefaultReporter.Add ("Domain of default", mepElemDefault.GetDomain ());

	uint32_t portCount = mepElemDefault.GetPortCount ();
	for (uint32_t portIdx = 0; portIdx < portCount; portIdx++) {
		elementDefaultReporter.SetTabCount (1);

		ACAPI::Result<VentilationPortDefault> ventilationPortDefault = mepElemDefault.GetVentilationPortDefault (portIdx);
		if (ventilationPortDefault.IsOk ()) {
			elementDefaultReporter.Add ("Port name", ventilationPortDefault->GetName ());
			elementDefaultReporter.SetTabCount (2);
			elementDefaultReporter.Add ("Domain", ventilationPortDefault->GetDomain ());
			elementDefaultReporter.Add ("Width", ventilationPortDefault->GetWidth ());
			elementDefaultReporter.Add ("Height", ventilationPortDefault->GetHeight ());
			elementDefaultReporter.Add ("Shape", ventilationPortDefault->GetShape ());
			elementDefaultReporter.Add ("MEPSystem", ventilationPortDefault->GetMEPSystem ());
			elementDefaultReporter.Add ("WallThickness", ventilationPortDefault->GetWallThickness ());
			elementDefaultReporter.Add ("FlowDirection", ventilationPortDefault->GetFlowDirection ());
		} else {
			ACAPI::Result<PipingPortDefault> pipingPortDefault = mepElemDefault.GetPipingPortDefault (portIdx);
			if (pipingPortDefault.IsOk ()) {
				elementDefaultReporter.Add ("Port name", pipingPortDefault->GetName ());
				elementDefaultReporter.SetTabCount (2);
				elementDefaultReporter.Add ("Domain", pipingPortDefault->GetDomain ());
				elementDefaultReporter.Add ("Diameter", pipingPortDefault->GetDiameter ());
				elementDefaultReporter.Add ("Shape", pipingPortDefault->GetShape ());
				elementDefaultReporter.Add ("MEPSystem", pipingPortDefault->GetMEPSystem ());
				elementDefaultReporter.Add ("WallThickness", pipingPortDefault->GetWallThickness ());
				elementDefaultReporter.Add ("FlowDirection", pipingPortDefault->GetFlowDirection ());
			} else {
				ACAPI::Result<CableCarrierPortDefault> cableCarrierPortDefault = mepElemDefault.GetCableCarrierPortDefault (portIdx);
				if (cableCarrierPortDefault.IsOk ()) {
					elementDefaultReporter.Add ("Port name", cableCarrierPortDefault->GetName ());
					elementDefaultReporter.SetTabCount (2);
					elementDefaultReporter.Add ("Domain", cableCarrierPortDefault->GetDomain ());
					elementDefaultReporter.Add ("Width", cableCarrierPortDefault->GetWidth ());
					elementDefaultReporter.Add ("Height", cableCarrierPortDefault->GetHeight ());
					elementDefaultReporter.Add ("Shape", cableCarrierPortDefault->GetShape ());
					elementDefaultReporter.Add ("MEPSystem", cableCarrierPortDefault->GetMEPSystem ());
				}
			}
		}
	}
	elementDefaultReporter.SetTabCount (0);

	std::optional<double> insulation = mepElemDefault.GetInsulationThickness ();
	insulation.has_value () ? elementDefaultReporter.Add ("Insulation thickness", *insulation) : elementDefaultReporter.Add ("Has no insulation");

	elementDefaultReporter.Write ();
}
// ! [ElementDefault-Getters-Example2]


} // namespace


GSErrCode QuerySelectedElementDetails ()
{
	GS::Array<API_Neig>	selNeigs;
	API_SelectionInfo 	selectionInfo = {};

	ERRCHK_NO_ASSERT (ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false));

	for (const API_Neig& neig : selNeigs) {
		API_Element	elem = {};
		elem.header.guid = neig.guid;
		ERRCHK_NO_ASSERT (ACAPI_Element_Get (&elem));

		if (elem.header.type.typeID == API_ExternalElemID
			&&	(	IsBranch (elem.header.type.classID)
				||	IsAccessory (elem.header.type.classID)
				||	IsEquipment (elem.header.type.classID)
				||	IsTerminal (elem.header.type.classID)
				||	IsFitting (elem.header.type.classID)
				||	IsFlexibleSegment (elem.header.type.classID)
				||	IsRoutingElement (elem.header.type.classID)))
		{
			WriteElemHeadDetails (elem);

			Element::Get (Adapter::UniqueID (neig.guid)).Then ([&](const Element& element) {
				WriteElementBaseDetails (element);
				WritePortDetails (element);
			});
		}

		if (elem.header.type.typeID == API_ExternalElemID && IsBranch (elem.header.type.classID))
			WriteBranchDetails (Adapter::UniqueID (neig.guid));
		else if (elem.header.type.typeID == API_ExternalElemID && IsRoutingElement (elem.header.type.classID))
			WriteRoutingElementDetails (Adapter::UniqueID (neig.guid));
		else if (elem.header.type.typeID == API_ExternalElemID && IsFlexibleSegment (elem.header.type.classID))
			WriteFlexibleSegmentDetails (Adapter::UniqueID (neig.guid));
	}

	return NoError;
}


GSErrCode QueryDefaultDetails ()
{
	Reporter reporter;

	reporter.Add ("Branch defaults:");
	reporter.Write ();

// ! [ElementDefault-Getters-Example]
	auto ductBranchDefault = CreateBranchDefault (Domain::Ventilation);
	if (ductBranchDefault.IsErr ())
		return ductBranchDefault.UnwrapErr ().kind;

	WriteElementDefaultDetails (*ductBranchDefault);

	auto pipeBranchDefault = CreateBranchDefault (Domain::Piping);
	if (pipeBranchDefault.IsErr ())
		return pipeBranchDefault.UnwrapErr ().kind;

	WriteElementDefaultDetails (*pipeBranchDefault);

	auto cableCarrierBranchDefault = CreateBranchDefault (Domain::CableCarrier);
	if (cableCarrierBranchDefault.IsErr ())
		return cableCarrierBranchDefault.UnwrapErr ().kind;

	WriteElementDefaultDetails (*cableCarrierBranchDefault);
// ! [ElementDefault-Getters-Example]

	return NoError;
}


} // namespace MEPExample