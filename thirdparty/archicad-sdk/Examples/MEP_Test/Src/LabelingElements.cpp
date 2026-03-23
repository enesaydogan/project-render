#include "LabelingElements.hpp"

// API
#include "ACAPinc.h"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"
#include "ACAPI/MEPElement.hpp"
#include "ACAPI/MEPRoutingElement.hpp"
#include "ACAPI/MEPRoutingSegment.hpp"
#include "ACAPI/MEPRoutingNode.hpp"


using namespace ACAPI::MEP;


namespace MEPExample {


static API_Guid PlaceLabelOn (const UniqueID& associatedElemId)
{
	bool autoTextFlag;
	if (ACAPI_AutoText_GetAutoTextFlag (&autoTextFlag) != NoError)
		return APINULLGuid;

	if (autoTextFlag) {
		bool autoTextFlagOff = false;
		if (ACAPI_AutoText_ChangeAutoTextFlag (&autoTextFlagOff) != NoError)
			return APINULLGuid;
	}

	API_Element element = {};
	element.header.type = API_LabelID;

	API_ElementMemo memo = {};

	if (ACAPI_Element_GetDefaults (&element, &memo) != NoError)
		return APINULLGuid;
	
	if (autoTextFlag) {
		if (ACAPI_AutoText_ChangeAutoTextFlag (&autoTextFlag) != NoError)
		return APINULLGuid;
	}

	element.label.createAtDefaultPosition = true;
	element.label.parentType			  = API_ExternalElemID;
	element.label.parent				  = GSGuid2APIGuid (associatedElemId.GetGuid ());

	if (ACAPI_Element_Create (&element, &memo) != NoError)
		return APINULLGuid;

	if (ACAPI_DisposeElemMemoHdls (&memo) != NoError)
		return APINULLGuid;

	return element.header.guid;
}


GSErrCode LabelSubelementsOfSelectedRoute ()
{
	GS::Array<API_Neig> selNeigs;
	API_SelectionInfo	selectionInfo = {};
	ERRCHK_NO_ASSERT (ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false));
	
	return ACAPI_CallUndoableCommand ("Label Segments of Selected Route Elements", [&] () -> GSErrCode {
		for (const API_Neig& neig : selNeigs) {
			API_Element elem = {};
			elem.header.guid = neig.guid;
			ERRCHK_NO_ASSERT (ACAPI_Element_Get (&elem));

			if (elem.header.type.typeID != API_ExternalElemID || !IsRoutingElement (elem.header.type.classID))
				continue;

// ! [RoutingSegment-Labeling-Example]
			// The following code snippet places an associative (autotext) Label on each RigidSegment, Bend and Transition of a RoutingElement.
			// The label will be associative to the middle of the RigidSegment, and the base points of the Bend/Transition (createAtDefaultPosition = true).
			// The content of these Labels will be taken from the current Label default.

			const ACAPI::Result<RoutingElement> routingElement = RoutingElement::Get (Adapter::UniqueID (neig.guid));
			if (routingElement.IsErr ()) {
				ACAPI_WriteReport (routingElement.UnwrapErr ().text.c_str (), false);
				return routingElement.UnwrapErr ().kind;
			}

			for (const UniqueID& segmentId : routingElement->GetRoutingSegmentIds ()) {
				const ACAPI::Result<RoutingSegment> routingSegment = RoutingSegment::Get (segmentId);
				if (routingSegment.IsErr ()) {
					ACAPI_WriteReport (routingSegment.UnwrapErr ().text.c_str (), false);
					return routingSegment.UnwrapErr ().kind;
				}

// ! [RigidSegment-Labeling-Example]
				for (const UniqueID& rigidSegmentId : routingSegment->GetRigidSegmentIds ()) {
					const API_Guid labelGuid = PlaceLabelOn (rigidSegmentId);
					if (labelGuid == APINULLGuid)
						return Error;

					GS::Array<API_Guid> connectedLabels;
					ERRCHK_NO_ASSERT (ACAPI_Grouping_GetConnectedElements (GSGuid2APIGuid (rigidSegmentId.GetGuid ()), API_LabelID, &connectedLabels));
					if (!connectedLabels.Contains (labelGuid))
						return Error;
				}
// ! [RigidSegment-Labeling-Example]
			}

			for (const UniqueID& nodeId : routingElement->GetRoutingNodeIds ()) {
				const ACAPI::Result<RoutingNode> routingSegment = RoutingNode::Get (nodeId);
				if (routingSegment.IsErr ()) {
					ACAPI_WriteReport (routingSegment.UnwrapErr ().text.c_str (), false);
					return routingSegment.UnwrapErr ().kind;
				}

// ! [Bend-Labeling-Example]
				for (const UniqueID& bendId : routingSegment->GetBendIds ()) {
					const API_Guid labelGuid = PlaceLabelOn (bendId);
					if (labelGuid == APINULLGuid)
						return Error;

					GS::Array<API_Guid> connectedLabels;
					ERRCHK_NO_ASSERT (ACAPI_Grouping_GetConnectedElements (GSGuid2APIGuid (bendId.GetGuid ()), API_LabelID, &connectedLabels));
					if (!connectedLabels.Contains (labelGuid))
						return Error;
				}
// ! [Bend-Labeling-Example]

// ! [Transition-Labeling-Example]
				for (const UniqueID& transitionId : routingSegment->GetTransitionIds ()) {
					const API_Guid labelGuid = PlaceLabelOn (transitionId);
					if (labelGuid == APINULLGuid)
						return Error;

					GS::Array<API_Guid> connectedLabels;
					ERRCHK_NO_ASSERT (ACAPI_Grouping_GetConnectedElements (GSGuid2APIGuid (transitionId.GetGuid ()), API_LabelID, &connectedLabels));
					if (!connectedLabels.Contains (labelGuid))
						return Error;
				}
// ! [Transition-Labeling-Example]
			}
		}

		return NoError;
	});
}


GSErrCode LabelSelectedMEPElements ()
{
	GS::Array<API_Neig> selNeigs;
	API_SelectionInfo	selectionInfo = {};
	ERRCHK_NO_ASSERT (ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false));
	
	return ACAPI_CallUndoableCommand ("Label Selected MEP Elements", [&] () -> GSErrCode {
		for (const API_Neig& neig : selNeigs) {
			API_Element elem = {};
			elem.header.guid = neig.guid;
			ERRCHK_NO_ASSERT (ACAPI_Element_Get (&elem));

			if (elem.header.type.typeID != API_ExternalElemID)
				continue;

			const ACAPI::Result<Element> mepElement = Element::Get (Adapter::UniqueID (neig.guid));
			if (mepElement.IsErr ()) {
				continue;
			}
			
			const API_Guid labelGuid = PlaceLabelOn (Adapter::UniqueID (neig.guid));
			if (labelGuid == APINULLGuid)
				return Error;
		}

		return NoError;
	});
}


} // namespace MEPExample
