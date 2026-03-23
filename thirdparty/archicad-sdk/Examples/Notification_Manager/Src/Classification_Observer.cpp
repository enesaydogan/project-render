// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _CLASSIFICATION_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	"ACAPinc.h"
#include	"Notification_Manager.h"


GS::Optional<API_Guid> classificationSystemEventHandlerId;
GS::Optional<API_Guid> classificationItemEventHandlerId;


#define CHECKERR(f) { GSErrCode err = (f); if (err != NoError) { return err; } }


static void WriteReport (const GS::UniString& message, const GS::HashSet<API_Guid>& ids)
{
	ACAPI_WriteReport (message + "\n", false);

	for (const auto& id : ids) {
		ACAPI_WriteReport ("  - " + APIGuidToString (id) + "\n", false);
	}
}


static GSErrCode RegisterClassificationSystemHandler ()
{
	class ClassificationSystemEventHandler : public API_IClassificationSystemEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Classification Systems were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Classification Systems were modified:", ids); 
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override 
		{
			WriteReport ("The following Classification Systems were deleted:", ids); 
		}
	};

	classificationSystemEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<ClassificationSystemEventHandler> (), *classificationSystemEventHandlerId);
	if (result != NoError) {
		classificationSystemEventHandlerId.Clear ();
	}

	return result;
}


static GSErrCode RegisterClassificationItemHandler ()
{
	class ClassificationItemEventHandler : public API_IClassificationItemEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Classification Items were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Classification Items were modified:", ids); 
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override 
		{
			WriteReport ("The following Classification Items were deleted:", ids); 
		}
	};

	classificationItemEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<ClassificationItemEventHandler> (), *classificationItemEventHandlerId);
	if (result != NoError) {
		classificationItemEventHandlerId.Clear ();
	}

	return result;
}


GSErrCode RegisterClassificationEventHandlers (void)
{
	CHECKERR (RegisterClassificationSystemHandler ());
	CHECKERR (RegisterClassificationItemHandler ());

	return NoError;
}


GSErrCode UnRegisterClassificationEventHandlers (void)
{
	if (classificationItemEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*classificationItemEventHandlerId);
		classificationItemEventHandlerId.Clear ();
	}

	if (classificationSystemEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*classificationSystemEventHandlerId);
		classificationSystemEventHandlerId.Clear ();
	}

	return NoError;
}


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_ClassificationMonitor (bool switchOn)
{
	if (switchOn)
		RegisterClassificationEventHandlers ();
	else
		UnRegisterClassificationEventHandlers ();
}
