#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Notification_Manager.h"
#include "ACAPI/KeynoteEventHandler.hpp"


GS::Optional<API_Guid> KeynoteItemEventHandlerId;
GS::Optional<API_Guid> KeynoteFolderEventHandlerId;


#define CHECKERR(f) { GSErrCode err = (f); if (err != NoError) { return err; } }


static void WriteReport (const GS::UniString& message, const GS::HashSet<API_Guid>& ids)
{
	ACAPI_WriteReport (message + "\n", false);

	for (const auto& id : ids)
		ACAPI_WriteReport ("  - " + APIGuidToString (id) + "\n", false);
}


static GSErrCode RegisterKeynoteItemHandler ()
{
	class KeynoteItemEventHandler : public ACAPI::API_IKeynoteItemEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Keynote Items were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Keynote Items were modified:", ids);
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Keynote Items were deleted:", ids);
		}
	};

	KeynoteItemEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<KeynoteItemEventHandler> (), *KeynoteItemEventHandlerId);
	if (result != NoError)
		KeynoteItemEventHandlerId.Clear ();

	return result;
}


static GSErrCode RegisterKeynoteFolderHandler ()
{
	class KeynoteFolderEventHandler : public ACAPI::API_IKeynoteFolderEventHandler {
	public:
		virtual void OnCreated (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Keynote Folders were created:", ids);
		}
		virtual void OnModified (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Keynote Folders were modified:", ids);
		}
		virtual void OnDeleted (const GS::HashSet<API_Guid>& ids) const override
		{
			WriteReport ("The following Keynote Folders were deleted:", ids);
		}
	};

	KeynoteFolderEventHandlerId.New ();
	const auto result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<KeynoteFolderEventHandler> (), *KeynoteFolderEventHandlerId);
	if (result != NoError)
		KeynoteFolderEventHandlerId.Clear ();

	return result;
}


GSErrCode RegisterKeynoteEventHandlers (void)
{
	CHECKERR (RegisterKeynoteItemHandler ());
	CHECKERR (RegisterKeynoteFolderHandler ());

	return NoError;
}


GSErrCode UnRegisterKeynoteEventHandlers (void)
{
	if (KeynoteItemEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*KeynoteItemEventHandlerId);
		KeynoteItemEventHandlerId.Clear ();
	}

	if (KeynoteFolderEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*KeynoteFolderEventHandlerId);
		KeynoteFolderEventHandlerId.Clear ();
	}

	return NoError;
}


// ============================================================================
// Install Notification Handlers
// ============================================================================
void	Do_KeynoteMonitor (bool switchOn)
{
	if (switchOn)
		RegisterKeynoteEventHandlers ();
	else
		UnRegisterKeynoteEventHandlers ();
}