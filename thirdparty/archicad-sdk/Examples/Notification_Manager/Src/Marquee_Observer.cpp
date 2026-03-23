// *****************************************************************************
// Description:     Source code for the Notification Manager Add-On
// *****************************************************************************
#ifndef MARQUEE_OBERSER_HPP
#define MARQUEE_OBERSER_HPP


// ------------------------------ Includes -------------------------------------
#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Notification_Manager.h"

#include <optional>

static std::optional<API_Guid> marqueeEventHandlerId;


GSErrCode RegisterMarqueeEventHandler ()
{
	class MarqueeEventHandler : public API_IMarqueeEventHandler {
	public:
		virtual void OnMarqueeChanged (const API_SelectionInfo& selectionInfo) const override
		{
			if (selectionInfo.typeID == API_SelEmpty) {
				ACAPI_WriteReport ("MarqueeEventHandler: Marquee has disappeared", false);
			} else {
				ACAPI_WriteReport ("MarqueeEventHandler: Marquee has changed", false);
			}
		}
	};

	API_Guid handlerId;
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<MarqueeEventHandler> (), handlerId);
	if (result == NoError) {
		marqueeEventHandlerId = handlerId;
	}

	return result;
}


GSErrCode UnregisterMarqueeEventHandler ()
{
	if (marqueeEventHandlerId.has_value ()) {
		ACAPI_Notification_UnregisterEventHandler (*marqueeEventHandlerId);
		marqueeEventHandlerId.reset ();
	}

	return NoError;
}


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_MarqueeIDMonitor (bool switchOn)
{
	if (switchOn)
		RegisterMarqueeEventHandler ();
	else
		UnregisterMarqueeEventHandler ();
}

#endif