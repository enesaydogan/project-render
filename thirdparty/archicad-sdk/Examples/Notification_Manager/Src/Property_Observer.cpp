#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Notification_Manager.h"


GS::Optional<API_Guid> PropertyGroupEventHandlerId;
GS::Optional<API_Guid> PropertyDefinitionEventHandlerId;


#define CHECKERR(f) { GSErrCode err = (f); if (err != NoError) { return err; } }


static void WriteReport (const GS::UniString& message, const GS::HashSet<API_Guid>& ids)
{
	ACAPI_WriteReport (message + "\n", false);

	for (const auto& id : ids) {
		ACAPI_WriteReport ("  - " + APIGuidToString (id) + "\n", false);
	}
}


static GSErrCode RegisterPropertyGroupHandler ()
{
	class PropertyGroupEventHandler : public API_IPropertyGroupEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Property Groups were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Property Groups were modified:", ids); 
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override 
		{
			WriteReport ("The following Property Groups were deleted:", ids); 
		}
	};

	PropertyGroupEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<PropertyGroupEventHandler> (), *PropertyGroupEventHandlerId);
	if (result != NoError) {
		PropertyGroupEventHandlerId.Clear ();
	}

	return result;
}


static GSErrCode RegisterPropertyDefinitionHandler ()
{
	class PropertyDefinitionEventHandler : public API_IPropertyDefinitionEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Property Definitions were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Property Definitions were modified:", ids); 
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override 
		{
			WriteReport ("The following Property Definitions were deleted:", ids); 
		}
	};

	PropertyDefinitionEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<PropertyDefinitionEventHandler> (), *PropertyDefinitionEventHandlerId);
	if (result != NoError) {
		PropertyDefinitionEventHandlerId.Clear ();
	}

	return result;
}


GSErrCode RegisterPropertyEventHandlers (void)
{
	CHECKERR (RegisterPropertyGroupHandler ());
	CHECKERR (RegisterPropertyDefinitionHandler ());

	return NoError;
}


GSErrCode UnRegisterPropertyEventHandlers (void)
{
	if (PropertyDefinitionEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*PropertyDefinitionEventHandlerId);
		PropertyDefinitionEventHandlerId.Clear ();
	}

	if (PropertyGroupEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*PropertyGroupEventHandlerId);
		PropertyGroupEventHandlerId.Clear ();
	}

	return NoError;
}


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_PropertyMonitor (bool switchOn)
{
	if (switchOn)
		RegisterPropertyEventHandlers ();
	else
		UnRegisterPropertyEventHandlers ();
}