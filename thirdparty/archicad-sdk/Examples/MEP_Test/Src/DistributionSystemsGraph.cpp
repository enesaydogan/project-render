#include "DistributionSystemsGraph.hpp"

// API
#include "ACAPinc.h"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPDistributionSystem.hpp"
#include "ACAPI/MEPDistributionSystemsGraph.hpp"
#include "ACAPI/MEPDistributionSystemsGraphTreeNode.hpp"
#include "ACAPI/MEPElement.hpp"

// STL
#include <vector>


using namespace ACAPI::MEP;


namespace MEPExample {


GSErrCode LabelConnectionsInGraph ()
{
	return ACAPI_CallUndoableCommand ("Label Segments of Selected Route Elements", [&] () -> GSErrCode {
// ! [CreateDistributionSystemsGraph_Example]
		auto newGraph = ACAPI::MEP::CreateDistributionSystemsGraph ();
		if (newGraph.IsErr ())
			return APIERR_COMMANDFAILED;
// ! [CreateDistributionSystemsGraph_Example]

		size_t index = 0;
		std::map<UniqueID, size_t> idToIndex;
		auto distributionGraph = newGraph.Unwrap ();

// ! [GetElements_Example]
		for (const auto elementId : distributionGraph.GetElements ())
			idToIndex[elementId] = ++index;
// ! [GetElements_Example]

// ! [GetSystems_Example]
		for (const auto& system : distributionGraph.GetSystems ()) {
// ! [GetSystems_Example]
// ! [TraverseTree_Example]
			GSErrCode traversalError { NoError };
			ACAPI::Result<void, ACAPI::Error> result = system.TraverseTree ([&] (const ACAPI::MEP::DistributionSystemsGraphTreeNode& node) -> bool {
				const ACAPI::Result<Element> mepElement = Element::Get (node.GetElement ());
				// If the element is invalid, continue traversal.
				if (mepElement.IsErr ())
					return true;


				std::string connectedFrom;
				if (node.GetPreviousElement ().has_value ())
					connectedFrom = std::to_string (idToIndex[*node.GetPreviousElement ()]);
				else
					connectedFrom = "No connection";
				std::string connectedTo;
				if (node.GetNextElements ().size () == 0)
					connectedTo = "No connections";
				else {
					for (const auto nextElement : node.GetNextElements ()) {
						if (!connectedTo.empty ())
							connectedTo += ", ";
						connectedTo += std::to_string (idToIndex[nextElement]);
					}
				}
				
				API_Element textElement = {};
				textElement.header.type = API_TextID;
				API_ElementMemo	textMemo;
				traversalError = ACAPI_Element_GetDefaults (&textElement, &textMemo);
				if (traversalError != NoError)
					return false;

				textElement.text.size = 0.5;
				textElement.text.loc.x = mepElement->GetAnchorPoint ().x;
				textElement.text.loc.y = mepElement->GetAnchorPoint ().y;
				textElement.text.multiStyle = false;
				textMemo.textContent = new GS::UniString { GS::UniString::SPrintf ("%llu:\nConnectedFrom: %s\nConnectedTo: %s", idToIndex[node.GetElement ()], connectedFrom.c_str (), connectedTo.c_str ()) };
				traversalError = ACAPI_Element_Create (&textElement, &textMemo);
				if (traversalError == NoError)
					traversalError = ACAPI_DisposeElemMemoHdls (&textMemo);

				// Don't continue traversal if we've encountered an error
				if (traversalError != NoError)
					return false;

				return true;
			});
// ! [TraverseTree_Example]

			if (traversalError == NoError)
				return traversalError;

			if (result.IsErr ())
				return APIERR_COMMANDFAILED;
		}

		return NoError;
	});
}


} // namespace MEPExample
