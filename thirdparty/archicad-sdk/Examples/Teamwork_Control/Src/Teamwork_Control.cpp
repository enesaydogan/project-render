// *****************************************************************************
// Source code for the Teamwork Control Add-On
// *****************************************************************************

#include "APIEnvir.h"
#define	_TEAMWORK_CONTROL_TRANSL_

// ---------------------------------- Includes ---------------------------------
#include "BuiltInLibrary.hpp"
#include "GSUnID.hpp"

// from GSRoot
#include "UniString.hpp"

// from IO
#include "FileSystem.hpp"

// from ACAPI
#include "ACAPinc.h"					// also includes APIdefs.h
#include "APICommon.h"

#include "TWControlResourceIds.hpp"
#include "OnExit.hpp"

// ---------------------------------- Types ------------------------------------

#define CompositesGuid			(ACAPI_Teamwork_FindLockableObjectSet ("Composites"))
#define FavoritesGuid			(ACAPI_Teamwork_FindLockableObjectSet ("Favorites"))
#define FillTypesGuid			(ACAPI_Teamwork_FindLockableObjectSet ("FillTypes"))
#define LayerSettingsGuid		(ACAPI_Teamwork_FindLockableObjectSet ("LayerSettingsDialog"))
#define LineTypesGuid			(ACAPI_Teamwork_FindLockableObjectSet ("LineTypes"))
#define MaterialsGuid			(ACAPI_Teamwork_FindLockableObjectSet ("Surfaces"))
#define MEPSystemsGuid			(ACAPI_Teamwork_FindLockableObjectSet ("MEPSystems"))
#define ModelViewOptionsGuid	(ACAPI_Teamwork_FindLockableObjectSet ("ModelViewOptions"))
#define OperationProfilesGuid	(ACAPI_Teamwork_FindLockableObjectSet ("OperationProfiles"))
#define PenTablesGuid			(ACAPI_Teamwork_FindLockableObjectSet ("PenTables"))
#define ProfilesGuid			(ACAPI_Teamwork_FindLockableObjectSet ("Profiles"))
#define ProjectInfoGuid			(ACAPI_Teamwork_FindLockableObjectSet ("ProjectInfo"))
#define ProjectPreferencesGuid	(ACAPI_Teamwork_FindLockableObjectSet ("PreferencesDialog"))
#define ZoneCategoriesGuid		(ACAPI_Teamwork_FindLockableObjectSet ("ZoneCategories"))
#define BuildingMaterialsGuid	(ACAPI_Teamwork_FindLockableObjectSet ("BuildingMaterials"))
#define BIMDataContainerGuid	(ACAPI_Teamwork_FindLockableObjectSet ("ClassificationsAndProperties"))
#define IssueTagsGuid			(ACAPI_Teamwork_FindLockableObjectSet ("IssueTags"))
#define ProjectLibraryListGuid	(ACAPI_Teamwork_FindLockableObjectSet ("ProjectLibraryList"))
#define GraphicOverridesGuid	(ACAPI_Teamwork_FindLockableObjectSet ("GraphicOverrides"))


static const GS::HashTable<API_Guid, GS::UniString>& GetObjectSetNamesForGuids ()
{
	static const GS::HashTable<API_Guid, GS::UniString> objectSetNamesForGuids = {{CompositesGuid, "Composites"},
																				  {FavoritesGuid, "Favorites"},
																				  {FillTypesGuid, "Fill Types"},
																				  {LayerSettingsGuid, "Layer Settings"},
																				  {LineTypesGuid, "Line Types"},
																				  {MaterialsGuid, "Surfaces"},
																				  {MEPSystemsGuid, "MEP Systems"},
																				  {ModelViewOptionsGuid, "Model View Options"},
																				  {OperationProfilesGuid, "Operation Profiles"},
																				  {PenTablesGuid, "Pen Sets"},
																				  {ProfilesGuid, "Profiles"},
																				  {ProjectInfoGuid, "Project Info"},
																				  {ProjectPreferencesGuid, "Preferences Dialog"},
																				  {ZoneCategoriesGuid, "Zone Categories"},
																				  {BuildingMaterialsGuid, "Building Materials"},
																				  {BIMDataContainerGuid, "Classifications and Properties"},
																				  {IssueTagsGuid, "Issue Tags"},
																				  {ProjectLibraryListGuid, "ProjectLibraryList"},
																				  {GraphicOverridesGuid, "GraphicOverrides"}};
	return objectSetNamesForGuids;
}



static GS::UniString GetObjectSetName (const API_Guid& objectId)
{
	const GS::HashTable<API_Guid, GS::UniString>& objectSetNamesForGuids = GetObjectSetNamesForGuids ();
	if (!objectSetNamesForGuids.ContainsKey (objectId)) {
		return "an unexpected object set";
	}
	return objectSetNamesForGuids[objectId];
}

// ---------------------------------- Variables --------------------------------

static GS::HashSet<API_Guid>	attributeTypesToBeNotifiedOnReservationChanges;

// ---------------------------------- Prototypes -------------------------------

static GS::UniString BuildLockedStatusStringFromConflicts (const GS::PagedArray<short>& conflicts)
{
	const GS::UniString lockedStatusStringBase = L("Locked by ");
	if (conflicts.IsEmpty ()) {
		return lockedStatusStringBase + "other member";
	}

	GS::UniString username;
	ACAPI_Teamwork_GetUsernameFromId (conflicts[0], &username);
	return lockedStatusStringBase + username;
}


static GS::UniString BuildLockableStatusReportStr (API_LockableStatus lockableStatus, const GS::PagedArray<short>& conflicts)
{
	const GS::UniString reportStringBase = L("Teamwork status: ");

	switch (lockableStatus) {
		case APILockableStatus_Free:
			return reportStringBase + "Free for Reservation";
		case APILockableStatus_Editable:
			return reportStringBase + "Editable";
		case APILockableStatus_Locked:
			return reportStringBase + BuildLockedStatusStringFromConflicts (conflicts);
		case APILockableStatus_NotAvailable:
			return reportStringBase + "Not available";
		default:
			return reportStringBase;
	}
}


static GS::UniString BuildOperationResultStringForErrCode (const GSErrCode err, const GS::UniString& operationName)
{
	switch (err) {
		case NoError:
			return operationName + " succeeded";
		case APIERR_CANCEL:
			return operationName + " cancelled";
		default:
			return operationName + " failed";
	}
}


struct OperationResult {
	API_LockableStatus lockableStatus;
	GS::UniString operationResultString;
	GS::PagedArray<short> conflicts;
};


static OperationResult ReserveObject (const API_Guid& objectGuid, const OperationResult& result)
{
	OperationResult operationResult = result;
	const GSErrCode errCode = ACAPI_Teamwork_ReserveLockable (objectGuid, &operationResult.conflicts);

	const GS::UniString operationName = L("Reservation");
	operationResult.operationResultString = BuildOperationResultStringForErrCode (errCode, operationName);

	return operationResult;
}


static OperationResult ReleaseObject (const API_Guid& objectGuid, const OperationResult& result)
{
	const GSErrCode errCode = ACAPI_Teamwork_ReleaseLockable (objectGuid);

	OperationResult operationResult = result;
	const GS::UniString operationName = L("Release");
	operationResult.operationResultString = BuildOperationResultStringForErrCode (errCode, operationName);

	return operationResult;
}


static OperationResult ReserveOrReleaseObject (const API_Guid& objectGuid)
{
	OperationResult operationResult = {};
	const API_LockableStatus lockableStatus = ACAPI_Teamwork_GetLockableStatus (objectGuid, &operationResult.conflicts);
	if (lockableStatus == APILockableStatus_Free) {
		operationResult = ReserveObject (objectGuid, operationResult);
	} else if (lockableStatus == APILockableStatus_Editable) {
		operationResult = ReleaseObject (objectGuid, operationResult);
	}
	operationResult.lockableStatus = ACAPI_Teamwork_GetLockableStatus (objectGuid, &operationResult.conflicts);

	return operationResult;
}


// =============================================================================
//
// Testing Teamwork Reservation functions of Elements and Lockable Object Sets
//
// =============================================================================

static void	ReserveOrRelease (const API_Guid& objectGuid, bool showAlert = true)
{
	const bool hasCreateRight = ACAPI_Teamwork_HasCreateRight (objectGuid);
	const bool hasDeleteModifyRight = ACAPI_Teamwork_HasDeleteModifyRight (objectGuid);
	if (!hasCreateRight && !hasDeleteModifyRight) {
		const GS::UniString reportString = L("You have insufficient privileges to modify ") + GetObjectSetName (objectGuid);
		ACAPI_WriteReport (reportString, true);
		return;
	}

	GS::UniString reportString (GetObjectSetName (objectGuid));
	
	const OperationResult operationResult = ReserveOrReleaseObject (objectGuid);
	if (!operationResult.operationResultString.IsEmpty ()) {
		reportString.Append (" - " + operationResult.operationResultString);
	}
	reportString.Append ("\n\n");
	reportString.Append (BuildLockableStatusReportStr (operationResult.lockableStatus,
													   operationResult.conflicts));

	ACAPI_WriteReport (reportString, showAlert);
}		// ReserveOrRelease


template <template<typename> class ContainerType>
static ContainerType<API_Guid> TransformSelectionNeigsToElementGuids (const GS::Array<API_Neig>& selNeigs);

template<>
GS::Array<API_Guid> TransformSelectionNeigsToElementGuids<GS::Array> (const GS::Array<API_Neig>& selNeigs)
{
	return selNeigs.Transform<API_Guid> ([] (const API_Neig& neig) { return neig.guid; });
}


template<>
GS::HashSet<API_Guid> TransformSelectionNeigsToElementGuids<GS::HashSet> (const GS::Array<API_Neig>& selNeigs)
{
	GS::HashSet<API_Guid> elementGuids;
	for (const API_Neig& neig : selNeigs) {
		elementGuids.Add (neig.guid);
	}
	return elementGuids;
}


static GS::Optional<GS::Array<API_Neig>> GetCurrentSelection ()
{
	API_SelectionInfo selectionInfo = {};
	const GS::OnExit killMarqueeCoords (
		[&selectionInfo] () { BMKillHandle (reinterpret_cast<GSHandle*> (&selectionInfo.marquee.coords)); });
	GS::Array<API_Neig>	selNeigs;

	const GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false);
	if (err != NoError) {
		return GS::NoValue;
	}
	return selNeigs;
}


template <template<typename> class ContainerType>
static GS::Optional<ContainerType<API_Guid>> GetGuidsFromCurrentSelection ()
{
	const GS::Optional<GS::Array<API_Neig>> selNeigs = GetCurrentSelection ();
	if (selNeigs.IsEmpty ()) {
		return GS::NoValue;
	}

	return TransformSelectionNeigsToElementGuids<ContainerType> (*selNeigs);
}


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------

static void	ReserveSelectedElements (const bool enableDialogs = true)
{
	const GS::Optional<GS::Array<API_Guid>> elements = GetGuidsFromCurrentSelection<GS::Array> ();
	if (elements.IsEmpty () || elements->IsEmpty ()) {
		return;
	}
	
	GS::HashTable<API_Guid, short> conflicts;
	ACAPI_Teamwork_ReserveElements (*elements, &conflicts, enableDialogs);
	if (!conflicts.IsEmpty ()) {
		ACAPI_WriteReport ("Teamwork Control failed to reserve %d element(s)", true, conflicts.GetSize ());
	}
}		// ReserveSelectedElements


static void ReserveSelectedElementsSilently ()
{
	constexpr bool enableDialogs = false;
	ReserveSelectedElements (enableDialogs);
}


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------

static void	ReleaseSelectedElements (const bool enableDialogs = true)
{
	const GS::Optional<GS::Array<API_Guid>> elements = GetGuidsFromCurrentSelection<GS::Array> ();
	if (elements.IsEmpty () || elements->IsEmpty ()) {
		return;
	}

	ACAPI_Teamwork_ReleaseElements (*elements, enableDialogs);
}		// ReleaseSelectedElements


static void ReleaseSelectedElementsSilently ()
{
	constexpr bool enableDialogs = false;
	ReleaseSelectedElements (enableDialogs);
}


// =============================================================================
//
// Testing Reservation notifications of selected Elements and Lockable Object Sets
//
// =============================================================================

static GSErrCode ElementReservationChangeHandler (const GS::HashTable<API_Guid, short>&	/*reserved*/,
																 const GS::HashSet<API_Guid>&			released,
																 const GS::HashSet<API_Guid>&			/*deleted*/)
{
	if (released.IsEmpty ())
		return NoError;

	ACAPI_WriteReport ("=   Teamwork Control Example add-on attempts to Reserve %d elements", false, released.GetSize ());

	return ACAPI_Teamwork_ReserveElements (released.Convert ());
}		// ElementReservationChangeHandler


static void InstallElementReservationChangeHandler (const short menuResID, const short itemIndex)
{
	const GS::Optional<GS::HashSet<API_Guid>> filterElementsInterestedOnly = GetGuidsFromCurrentSelection<GS::HashSet> ();
	if (filterElementsInterestedOnly.IsEmpty () || filterElementsInterestedOnly->IsEmpty ()) {
		return;
	}
	ACAPI_Notification_CatchElementReservationChange (ElementReservationChangeHandler, filterElementsInterestedOnly.GetPtr ());
	InvertMenuItemMark (menuResID, itemIndex);
}


static void UninstallElementReservationChangeHandler (const short menuResID, const short itemIndex)
{
	ACAPI_Notification_CatchElementReservationChange (nullptr); // stop watching element reservation changes
	InvertMenuItemMark (menuResID, itemIndex);
}


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------

static void	WatchSelectedElementsReservation (const short menuResID, const short itemIndex)
{
	if (!GetMenuItemMark (menuResID, itemIndex)) {
		InstallElementReservationChangeHandler (menuResID, itemIndex);
	} else {
		UninstallElementReservationChangeHandler (menuResID, itemIndex);
	}
}		// WatchSelectedElementsReservation


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------

static GSErrCode LockableReservationChangeHandler (const API_Guid& objectId, short ownerId)
{
	if (ownerId != 0) {		// interested only in release actions
		return NoError;
	}

	if (attributeTypesToBeNotifiedOnReservationChanges.Contains (objectId)) {
		attributeTypesToBeNotifiedOnReservationChanges.Delete (objectId);
		if (!attributeTypesToBeNotifiedOnReservationChanges.IsEmpty ()) {
			ACAPI_Notification_CatchLockableReservationChange (LockableReservationChangeHandler,
														 &attributeTypesToBeNotifiedOnReservationChanges);
		} else {
			ACAPI_Notification_CatchLockableReservationChange (nullptr);
		}
	}
	
	const GS::UniString objectSetName = GetObjectSetName (objectId);
	ACAPI_WriteReport ("=   Teamwork Control Example add-on attempts to Reserve " + objectSetName, false);

	ReserveOrRelease (objectId, false);

	return NoError;
}		// LockableReservationChangeHandler


// -----------------------------------------------------------------------------
// Collect attribute types that are currently reserved by other users
// -----------------------------------------------------------------------------
static GS::UniString	GetReservedAttributeTypes (GS::HashSet<API_Guid>& attributeTypesReservedByOthers)
{
	GS::UniString reportString;
	for (const auto& pair : GetObjectSetNamesForGuids ()) {
		const API_Guid& guid = pair.key;
		const GS::UniString& objectSetName = pair.value;
		if (ACAPI_Teamwork_GetLockableStatus (guid) == APILockableStatus_Locked) {
			attributeTypesReservedByOthers.Add (guid);
			if (!reportString.IsEmpty ()) {
				reportString.Append (", ");
			}
			reportString.Append (objectSetName);
		}
	}

	return reportString;
}		// GetReservedAttributeTypes


static void InstallAttributesReservationChangeHandler (const short menuResID, const short itemIndex)
{
	GS::UniString reportString = GetReservedAttributeTypes (attributeTypesToBeNotifiedOnReservationChanges);
	if (attributeTypesToBeNotifiedOnReservationChanges.IsEmpty ()) {
		return;
	}

	if (DBVERIFY (!reportString.IsEmpty ())) {
		reportString.Append (" will be reserved automatically when they get released by the current locker user(s).");
		ACAPI_WriteReport (reportString, true);
	}
	ACAPI_Notification_CatchLockableReservationChange (LockableReservationChangeHandler, &attributeTypesToBeNotifiedOnReservationChanges);
	InvertMenuItemMark (menuResID, itemIndex);
}
	

static void UninstallAttributesReservationChangeHandler (const short menuResID, const short itemIndex)
{
	ACAPI_Notification_CatchLockableReservationChange (nullptr);			// stop watching attribute reservation changes
	attributeTypesToBeNotifiedOnReservationChanges.Clear ();
	InvertMenuItemMark (menuResID, itemIndex);
}

// -----------------------------------------------------------------------------
// Install lockable object reservation change handler
// to reserve them when they get released by others
// -----------------------------------------------------------------------------
static void	WatchAttributesReservation (const short menuResID, const short itemIndex)
{
	if (!GetMenuItemMark (menuResID, itemIndex)) {
		InstallAttributesReservationChangeHandler (menuResID, itemIndex);
	} else {
		UninstallAttributesReservationChangeHandler (menuResID, itemIndex);
	}
}		// WatchAttributesReservation


// -----------------------------------------------------------------------------
static constexpr const char* folderNameForTWTest = "FolderForLibParts (Teamwork_Control Test)";
static constexpr const char* libPartNameForTWTest1 = "LPForTWTest1";
static constexpr const char* libPartNameForTWTest2 = "LPForTWTest2";


// -----------------------------------------------------------------------------
static IO::Location	GetEmbeddedLibPartFolder ()
{
	GS::Array<API_LibraryInfo> libInfo;
	Int32 embeddedLibraryIndex = -1;
	GSErrCode err = ACAPI_LibraryManagement_GetLibraries (&libInfo, &embeddedLibraryIndex);
	
	if (err != NoError || embeddedLibraryIndex < 0) {
		return {};
	}

	const IO::Location retLoc = libInfo[embeddedLibraryIndex].location;
	IO::Location ownFolderLoc (retLoc);
	ownFolderLoc.AppendToLocal (IO::Name (folderNameForTWTest));
	err = IO::fileSystem.CreateFolder (ownFolderLoc);
	
	return (err == NoError || err == IO::FileSystem::TargetExists) ? ownFolderLoc : retLoc;
}


bool IsExistingLibPart (const API_LibPart& libPart)
{
	API_LibPart copiedLibPart = libPart;
	const GSErrCode err = ACAPI_LibraryPart_Search (&copiedLibPart, false);
	delete copiedLibPart.location;
	return err == NoError;
}


static GS::Optional<API_LibPart> BuildEmbeddedLibpart (const GS::UniString& libPartName)
{
	API_LibPart libPart = {};
	GS::ucscpy (libPart.docu_UName, libPartName.ToUStr ());
	libPart.typeID = APILib_ObjectID;
	libPart.isTemplate = false;
	libPart.isPlaceable = true;
	libPart.location = nullptr;

	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ()
		.GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::ModelElementLibPartID);

	CHCopyC (unID.ToUniString ().ToCStr (), libPart.parentUnID);	// Model Element subtype

	if (IsExistingLibPart (libPart)) {
		ACAPI_WriteReport ("Library Part was already created", true);
		return GS::NoValue;
	}
	return libPart;
}


static void Write3DSectionScript ()
{
	char buffer[256];
	API_LibPartSection section = {};
	section.sectType = API_Sect3DScript;
	ACAPI_LibraryPart_NewSection (&section);
	sprintf (buffer, "MATERIAL mat%s%s", GS::EOL, GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	sprintf (buffer, "BLOCK a, b, 1%s", GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	sprintf (buffer, "ADD a * 0.5, b* 0.5, 1%s", GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	sprintf (buffer, "CYLIND zzyzx - 3, MIN (a, b) * 0.5%s", GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	sprintf (buffer, "ADDZ zzyzx - 3%s", GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	sprintf (buffer, "CONE 2, MIN (a, b) * 0.5, 0.0, 90, 90%s", GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	ACAPI_LibraryPart_EndSection ();
}


static void Write2DSectionScript ()
{
	char buffer[256];
	API_LibPartSection section = {};
	section.sectType = API_Sect2DScript;
	ACAPI_LibraryPart_NewSection (&section);
	sprintf (buffer, "PROJECT2 3, 270, 2%s", GS::EOL);
	ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
	ACAPI_LibraryPart_EndSection ();
}


// -----------------------------------------------------------------------------
// Create embedded libpart into a new folder
// -----------------------------------------------------------------------------
static void	CreateEmbeddedLibPart (const GS::UniString& libPartName)
{
	GS::Optional<API_LibPart> libPart = BuildEmbeddedLibpart (libPartName);
	if (libPart.IsEmpty ()) {
		return;
	}
	
	IO::Location libPartLoc = GetEmbeddedLibPartFolder ();
	libPart->location = &libPartLoc;

	ACAPI_LibraryManagement_OverwriteLibPart (static_cast<Int32> (true));
	GSErrCode err = ACAPI_LibraryPart_Create (libPart.GetPtr ());
	const GS::OnExit deleteLibPartLocation ([&libPart] () { delete libPart->location; });
	ACAPI_LibraryManagement_OverwriteLibPart (static_cast<Int32> (false));

	if (err != NoError) {
		ACAPI_WriteReport ("Library Part creation failed", true);
		return;
	}

	Write3DSectionScript ();
	Write2DSectionScript ();

	err = ACAPI_LibraryPart_Save (libPart.GetPtr ());
	if (err != NoError) {
		ACAPI_WriteReport ("Library Part creation failed", true);
		return;
	}
	
	ACAPI_WriteReport ("Library Part \"%T\" created", true, GS::UniString (libPart->docu_UName).ToPrintf ());
}		// CreateEmbeddedLibPart


static GS::Array<GS::UniString> GetLibPartNamesForTWTest ()
{
	return {libPartNameForTWTest1, libPartNameForTWTest2};
}



// -----------------------------------------------------------------------------
// Create embedded libparts into a new folder
// -----------------------------------------------------------------------------
static void	CreateEmbeddedLibParts ()
{
	for (const GS::UniString& name : GetLibPartNamesForTWTest ()) {
		CreateEmbeddedLibPart (name);
	}
} 		// CreateEmbeddedLibParts


// -----------------------------------------------------------------------------
// Delete embedded libpart
// -----------------------------------------------------------------------------
static void	DeleteEmbeddedLibPart ()
{
	API_LibPart libPart = {};
	libPart.typeID = APILib_ObjectID;
	GS::snuprintf (libPart.docu_UName, sizeof (libPart.docu_UName) / sizeof (GS::uchar_t), libPartNameForTWTest1);

	GSErrCode err = ACAPI_LibraryPart_Search (&libPart, false);
	const GS::OnExit deleteLibPartLocation ([&libPart] () { delete libPart.location; });
	if (err != NoError) {
		ACAPI_WriteReport ("Library Part was already deleted or wasn't created yet", true);
		return;
	}

	err = ACAPI_LibraryManagement_DeleteEmbeddedLibItem (libPart.location);
	if (err != NoError) {
		ACAPI_WriteReport ("Library Part deletion failed", true);
		return;
	}
	
	ACAPI_WriteReport ("Library Part \"%T\" was deleted",
					   true,
					   GS::UniString (libPart.docu_UName).ToPrintf ());
}		// DeleteEmbeddedLibPart

using ExistingLibPartDatas = GS::Pair<GS::Array<API_LibPart>, GS::Array<IO::Location>>;
static ExistingLibPartDatas GetExistingLibPartsAndLocationsForTest ()
{
	GS::Array<API_LibPart> libPartArray;
	GS::Array<IO::Location> locArray;
	for (const GS::UniString& name : GetLibPartNamesForTWTest ()) {
		API_LibPart libPart = {};
		libPart.typeID = APILib_ObjectID;
		GS::ucscpy (libPart.docu_UName, name.ToUStr ());

		const GSErrCode err = ACAPI_LibraryPart_Search (&libPart, false);
		if (err != NoError) {
			continue;
		}

		locArray.Push (*libPart.location);
		libPartArray.Push (libPart);
	}
	return {libPartArray, locArray};
}


// -----------------------------------------------------------------------------
// Delete embedded libparts
// -----------------------------------------------------------------------------
static void	DeleteEmbeddedLibParts ()
{
	ExistingLibPartDatas libPartDatas = GetExistingLibPartsAndLocationsForTest ();
	GS::Array<API_LibPart>& libPartArray = libPartDatas.first;
	const GS::OnExit deleteLibPartLocations (
		[&libPartArray] () {
			for (API_LibPart& libPart : libPartArray) {
				delete libPart.location;
			}
		});
	GS::Array<IO::Location>& locArray = libPartDatas.second;
	
	if (locArray.IsEmpty () || libPartArray.IsEmpty ()) {
		ACAPI_WriteReport ("Library Parts were already deleted or were not created yet", true);
		return;
	}

	const GSErrCode err = ACAPI_LibraryManagement_DeleteEmbeddedLibItems (&locArray);
	if (err != NoError) {
		ACAPI_WriteReport ("Library Part deletion failed", true);
		return;
	}
	for (const API_LibPart& libPart : libPartArray) {
		ACAPI_WriteReport ("Library Part \"%T\" was deleted", true, GS::UniString (libPart.docu_UName).ToPrintf ());
	}
}		// DeleteEmbeddedLibParts


// -----------------------------------------------------------------------------
//  Reserve or Release the library folder
// -----------------------------------------------------------------------------
static void	ReserveOrReleaseEmbeddedLibraryFolder ()
{
	IO::Location libPartFolderLoc = GetEmbeddedLibPartFolder ();
	API_Guid libItemRefGuid;
	const GSErrCode err = ACAPI_LibraryPart_GetLibPartRefGuid (&libPartFolderLoc, &libItemRefGuid);
	
	if (err != NoError) {
		ACAPI_WriteReport ("Please create the embedded libpart at first with the another menu item!", true);
		return;
	}

	ReserveOrRelease (libItemRefGuid);
}


static void ReceiveChanges ()
{
	ACAPI_Teamwork_ReceiveChanges ();
}


static void SendChanges (const GS::UniString& comment = u"Sent changes by Teamwork Control Addon."sv)
{
	ACAPI_Teamwork_SendChanges (comment);
}


static void HandleTeamworkControlMenuItemIndex (const short menuResId, const Int32 itemIndex)
{
	enum TeamworkControlMenuItem {
		ReserveReleaseLayerSettings = 1,
		ReserveReleaseLineTypes,
		ReserveReleaseFillTypes,
		ReserveReleaseComposites,
		ReserveReleasePenSets,
		ReserveReleaseSurfaces,
		ReserveReleaseBuildingMaterials,
		ReserveReleaseZoneCategories,
		ReserveReleaseIssueTags,
		ReserveReleaseProfiles,
		ReserveReleaseFavorites,
		ReserveReleaseClassificationsAndProperties,
		ReserveReleaseMEPSystems,
		ReserveReleaseModelViewOptions,
		ReserveReleaseOperationProfiles,
		ReserveReleaseProjectInfo,
		ReserveReleasePreferencesDialog,
		ReserveReleaseProjectLibraryList,
		ReserveReleaseGraphicOverrides,
		Separator1,
		ReserveSelection,
		ReleaseSelection,
		ReserveSelectionSilently,
		ReleaseSelectionSilently,
		Separator2,
		WatchReservationOfSelectedElements,
		WatchAttributeReservation,
		Separator3,
		ReserveReleaseEmbeddedLibraryFolder,
		Separator4,
		ReceiveChangesCommand,
		SendChangesCommand
	};

	switch (itemIndex) {
		case ReserveReleaseLayerSettings:
			ReserveOrRelease (LayerSettingsGuid); break;
		case ReserveReleaseLineTypes:
			ReserveOrRelease (LineTypesGuid); break;
		case ReserveReleaseFillTypes:
			ReserveOrRelease (FillTypesGuid); break;
		case ReserveReleaseComposites:
			ReserveOrRelease (CompositesGuid); break;
		case ReserveReleasePenSets:
			ReserveOrRelease (PenTablesGuid); break;
		case ReserveReleaseSurfaces:
			ReserveOrRelease (MaterialsGuid); break;
		case ReserveReleaseBuildingMaterials:
			ReserveOrRelease (BuildingMaterialsGuid); break;
		case ReserveReleaseZoneCategories:
			ReserveOrRelease (ZoneCategoriesGuid); break;
		case ReserveReleaseIssueTags:
			ReserveOrRelease (IssueTagsGuid); break;
		case ReserveReleaseProfiles:
			ReserveOrRelease (ProfilesGuid); break;
		case ReserveReleaseFavorites:
			ReserveOrRelease (FavoritesGuid); break;
		case ReserveReleaseClassificationsAndProperties:
			ReserveOrRelease (BIMDataContainerGuid); break;
		case ReserveReleaseMEPSystems:
			ReserveOrRelease (MEPSystemsGuid); break;
		case ReserveReleaseModelViewOptions:
			ReserveOrRelease (ModelViewOptionsGuid); break;
		case ReserveReleaseOperationProfiles:
			ReserveOrRelease (OperationProfilesGuid); break;
		case ReserveReleaseProjectInfo:
			ReserveOrRelease (ProjectInfoGuid); break;
		case ReserveReleasePreferencesDialog:
			ReserveOrRelease (ProjectPreferencesGuid); break;
		case ReserveReleaseProjectLibraryList:
			ReserveOrRelease (ProjectLibraryListGuid); break;
		case ReserveReleaseGraphicOverrides:
			ReserveOrRelease (GraphicOverridesGuid); break;
		case ReserveSelection:
			ReserveSelectedElements (); break;
		case ReleaseSelection:
			ReleaseSelectedElements (); break;
		case ReserveSelectionSilently:
			ReserveSelectedElementsSilently (); break;
		case ReleaseSelectionSilently:
			ReleaseSelectedElementsSilently (); break;
		case WatchReservationOfSelectedElements:
			WatchSelectedElementsReservation (menuResId, static_cast<short> (itemIndex)); break;
		case WatchAttributeReservation:
			WatchAttributesReservation (menuResId, static_cast<short> (itemIndex)); break;
		case ReserveReleaseEmbeddedLibraryFolder:
			ReserveOrReleaseEmbeddedLibraryFolder (); break;
		case ReceiveChangesCommand:
			ReceiveChanges (); break;
		case SendChangesCommand:
			SendChanges (); break;
	}
}


static void HandleEmbeddedLibraryMenuItemIndex (const UInt32 itemIndex)
{
	enum EmbeddedLibraryMenuItem {
		CreateEmbeddedLibraryPartsIntoANewFolder = 1,
		DeleteEmbeddedLibraryPart = 2,
		DeleteEmbeddedLibraryParts = 3
	};
	switch (itemIndex) {
		case CreateEmbeddedLibraryPartsIntoANewFolder:	CreateEmbeddedLibParts ();	break;
		case DeleteEmbeddedLibraryPart:					DeleteEmbeddedLibPart ();	break;
		case DeleteEmbeddedLibraryParts:				DeleteEmbeddedLibParts ();	break;
	}
}


// -----------------------------------------------------------------------------
// Callback function on menu command event
// -----------------------------------------------------------------------------
GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
	switch (params->menuItemRef.menuResID) {
		case TEAMWORK_CONTROL_MENU_STRINGS_RES_ID:	// "Teamwork Control"
			if (!ACAPI_Teamwork_HasConnection ()) {
				ACAPI_WriteReport ("This function is available only in Teamwork mode", true);
				return NoError;
			}

			if (!ACAPI_Teamwork_IsOnline ()) {
				ACAPI_WriteReport ("Sorry, the BIM Server is currently not available", true);
				return NoError;
			}
			HandleTeamworkControlMenuItemIndex (params->menuItemRef.menuResID,
												params->menuItemRef.itemIndex);
			break;

		case EMBEDDED_LIBRARY_MENU_STRINGS_RES_ID:	// "Teamwork Control - Embedded Library Utility"
			HandleEmbeddedLibraryMenuItemIndex (params->menuItemRef.itemIndex);
			break;
	}
	return NoError;
}		// MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================


//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	enum AddonStringIds {
		AddOnNameId = 1,
		AddOnDescriptionId = 2
	};
	envir->addOnInfo.name = RSGetIndString (ADD_ON_STRINGS_RES_ID, AddOnNameId, ACAPI_GetOwnResModule ());
	envir->addOnInfo.description = RSGetIndString (ADD_ON_STRINGS_RES_ID, AddOnDescriptionId, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface ()
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (TEAMWORK_CONTROL_MENU_STRINGS_RES_ID,
										 TEAMWORK_CONTROL_STATUS_BAR_STRINGS_RES_ID,
										 MenuCode_UserDef,
										 MenuFlag_Default);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_MenuItem_RegisterMenu (TEAMWORK_CONTROL_MENU_STRINGS_RES_ID)", err);
		return err;
	}
	err = ACAPI_MenuItem_RegisterMenu (EMBEDDED_LIBRARY_MENU_STRINGS_RES_ID,
							   EMBEDDED_LIBRARY_STATUS_BAR_STRINGS_RES_ID,
							   MenuCode_UserDef,
							   MenuFlag_Default);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_MenuItem_RegisterMenu (EMBEDDED_LIBRARY_MENU_STRINGS_RES_ID)", err);
		return err;
	}
	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize ()
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (TEAMWORK_CONTROL_MENU_STRINGS_RES_ID, MenuCommandHandler);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_MenuItem_InstallMenuHandler (TEAMWORK_CONTROL_MENU_STRINGS_RES_ID)", err);
		return err;
	}
	err = ACAPI_MenuItem_InstallMenuHandler (EMBEDDED_LIBRARY_MENU_STRINGS_RES_ID, MenuCommandHandler);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_MenuItem_InstallMenuHandler (EMBEDDED_LIBRARY_MENU_STRINGS_RES_ID)", err);
		return err;
	}
	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData ()
{
	return NoError;
}		// FreeData
