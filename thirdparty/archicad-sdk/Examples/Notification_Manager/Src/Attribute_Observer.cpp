// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"

// ------------------------------ Includes -------------------------------------

#include "ACAPinc.h"
#include "Notification_Manager.h"


GS::Optional<API_Guid> attributeEventHandlerId;
GS::Optional<API_Guid> attributeFolderEventHandlerId;


#define CHECKERR(f) { GSErrCode err = (f); if (err != NoError) { return err; } }


static void WriteReport (const GS::UniString& message, const GS::HashSet<API_Guid>& ids)
{
	ACAPI_WriteReport (message + "\n", false);

	for (const auto& id : ids) {
		ACAPI_WriteReport ("  - " + APIGuidToString (id) + "\n", false);
	}
}


static GSErrCode RegisterAttributeHandler ()
{
	class AttributeEventHandler : public API_IAttributeEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Attributes were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Attributes were modified:", ids); 
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override 
		{
			WriteReport ("The following Attributes were deleted:", ids); 
		}
	};

	attributeEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<AttributeEventHandler> (), *attributeEventHandlerId);
	if (result != NoError) {
		attributeEventHandlerId.Clear ();
	}

	return result;
}


static GSErrCode RegisterAttributeFolderHandler ()
{
	class AttributeFolderEventHandler : public API_IAttributeFolderEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Attribute Folders were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Attribute Folders were modified:", ids); 
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override 
		{
			WriteReport ("The following Attribute Folders were deleted:", ids); 
		}
	};

	attributeFolderEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<AttributeFolderEventHandler> (), *attributeFolderEventHandlerId);
	if (result != NoError) {
		attributeFolderEventHandlerId.Clear ();
	}

	return result;
}


GSErrCode RegisterAttributeEventHandlers (void)
{
	CHECKERR (RegisterAttributeHandler ());
	CHECKERR (RegisterAttributeFolderHandler ());

	return NoError;
}


GSErrCode UnRegisterAttributeHandlers (void)
{
	if (attributeEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*attributeEventHandlerId);
		attributeEventHandlerId.Clear ();
	}

	if (attributeFolderEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*attributeFolderEventHandlerId);
		attributeFolderEventHandlerId.Clear ();
	}

	return NoError;
}


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_AttributeMonitor (bool switchOn)
{
	if (switchOn)
		RegisterAttributeEventHandlers ();
	else
		UnRegisterAttributeHandlers ();
}
