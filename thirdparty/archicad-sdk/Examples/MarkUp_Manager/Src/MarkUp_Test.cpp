// *****************************************************************************
// Description:		Source code for the Markup Test
// *****************************************************************************

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"MarkUp_Test.hpp"

// DG
#include	"DGFileDialog.hpp"

// InputOutput
#include	"FileSystem.hpp"


// =============================================================================
// Utility helper functions
// =============================================================================

#define TestMarkUpName "- Test Issue -"

inline bool GetTestMarkUp (API_MarkUpType& testMarkUp)
{
	GS::Array<API_MarkUpType> markUpList;

	GSErrCode err = ACAPI_Markup_GetList (APINULLGuid, &markUpList);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Markup_GetList failed", err);
		return false;
	}

	for (auto it = markUpList.Enumerate (); it != nullptr; ++it) {
		if (it->name == TestMarkUpName) {
			testMarkUp = *it;
			return true;
		}
	}

	ACAPI_WriteReport ("Test Issue does not exist. Create test Issue!", false);

	return false;
}


static GS::UniString GSTimeToUniString (GSTime gsTime)
{
	GSTimeRecord gsTimeRecord;
	TIGetTimeRecord (gsTime, &gsTimeRecord, TI_UTC_TIME);

	GS::UniString result = GS::UniString::Printf (
		"%d-%.2d-%.2d %.2d:%.2d:%.2d",
		gsTimeRecord.year,
		gsTimeRecord.month,
		gsTimeRecord.day,
		gsTimeRecord.hour,
		gsTimeRecord.minute,
		gsTimeRecord.second
	);

	return result;
}


inline void DumpMarkUp (const API_MarkUpType& markUp)
{
	WriteReport ("--------------- API_MarkUpType ---------------");
	WriteReport ("GUID: %s", APIGuidToString (markUp.guid).ToCStr ().Get ());
	WriteReport ("Name: %s", markUp.name.ToCStr ().Get ());
	WriteReport ("Parent GUID: %s", APIGuidToString (markUp.parentGuid).ToCStr ().Get ());
	WriteReport ("Text: %s", markUp.tagText.ToCStr ().Get ());
	WriteReport ("Text Element GUID: %s", APIGuidToString (markUp.tagTextElemGuid).ToCStr ().Get ());
	WriteReport ("Is Text Visible: %s", markUp.isTagTextElemVisible ? "true" : "false");

	GS::Array<API_Guid> highlightedElems;
	ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Highlight, highlightedElems);
	GS::Array<API_Guid> modifiedElems;
	ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Modification, modifiedElems);
	GS::Array<API_Guid> createdElems;
	ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Creation, createdElems);
	GS::Array<API_Guid> deletedElems;
	ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Deletion, deletedElems);

	WriteReport ("Highlighted Elements:");
	highlightedElems.Enumerate ([] (const API_Guid& elemGuid) {
		WriteReport ("\t%s", APIGuidToString (elemGuid).ToCStr ().Get ());
	});
	WriteReport ("Modified Elements:");
	modifiedElems.Enumerate ([] (const API_Guid& elemGuid) {
		WriteReport ("\t%s", APIGuidToString (elemGuid).ToCStr ().Get ());
	});
	WriteReport ("Created Elements:");
	createdElems.Enumerate ([] (const API_Guid& elemGuid) {
		WriteReport ("\t%s", APIGuidToString (elemGuid).ToCStr ().Get ());
	});
	WriteReport ("Deleted Elements:");
	deletedElems.Enumerate ([] (const API_Guid& elemGuid) {
		WriteReport ("\t%s", APIGuidToString (elemGuid).ToCStr ().Get ());
	});


	GS::Array<API_MarkUpCommentType> comments;
	ACAPI_Markup_GetComments (markUp.guid, &comments);
	WriteReport ("Comments:");
	comments.Enumerate ([] (const API_MarkUpCommentType& comment) {
		WriteReport ("--------");
		WriteReport ("\tAuthor: %s", comment.author.ToCStr ().Get ());
		WriteReport ("\tCreation Time: %s", GSTimeToUniString (comment.creaTime).ToCStr ().Get ());
		auto GetCommentStatusStr = [] (API_MarkUpCommentStatusID commentStatusID) -> const char* {
			switch (commentStatusID) {
				case APIComment_Error:		return "Error";
				case APIComment_Warning:	return "Warning";
				case APIComment_Info:		return "Info";
				case APIComment_Unknown:
				default:					return "Unknown";
			}
		};
		WriteReport ("\tStatus: %s", GetCommentStatusStr (comment.status));
		WriteReport ("\tText: %s", comment.text.ToCStr ().Get ());
		WriteReport ("--------");
	});

	WriteReport ("----------------------------------------------");
}


static GS::Optional<API_Elem_Head> GetElemHead (const API_Guid& guid)
{
	API_Elem_Head elemHead = {};
	elemHead.guid = guid;

	const GSErrCode err = ACAPI_Element_GetHeader (&elemHead);
	if (err != NoError) {
		return GS::NoValue;
	}

	return elemHead;
}


static GS::Optional<API_Element> GetElement (const API_Guid& paramGuid)
{
	API_Element elem = {};
	elem.header.guid = paramGuid;

	const GSErrCode err = ACAPI_Element_Get (&elem);
	if (err != NoError) {
		return GS::NoValue;
	}

	return elem;
}


static GS::Optional<API_Guid> GetSectElemParentGuid (const API_Guid& guid)
{
	const GS::Optional<API_Element> elem = GetElement (guid);
	if (!elem.HasValue ()) {
		return GS::NoValue;
	}

	return elem->sectElem.parentGuid;
}


static GSErrCode GetSelection (GS::Array<API_Guid>& elemList)
{
	API_SelectionInfo	selectionInfo;
	GS::Array<API_Neig>	selNeigs;

	elemList.Clear ();

	GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false);
	BMKillHandle ((GSHandle*) &selectionInfo.marquee.coords);
	if (err == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty)
		return err;

	if (err != APIERR_NOSEL && err != NoError) {
		ErrorBeep ("ACAPI_Selection_GetInfo failed", err);
		return err;
	}

	for (Int32 i = 0; i < selectionInfo.sel_nElem; i++) {
		const GS::Optional<API_Elem_Head> elemHead = GetElemHead (selNeigs[i].guid);
		if (DBERROR (elemHead.IsEmpty ())) {
			continue;
		}

		if (elemHead->type == API_SectElemID) {
			const GS::Optional<API_Guid> parent = GetSectElemParentGuid (elemHead->guid);
			if (DBERROR (parent.IsEmpty ())) {
				continue;
			}

			elemList.Push (*parent);
		} else {
			elemList.Push (elemHead->guid);
		}
	}

	return err;
}


static  GSErrCode GetBCFFileLocation (DG::FileDialog::Type type, const GS::UniString& title, IO::Location& bcfFilePath)
{
	DG::FileDialog fileDialog (type);
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::CurrentFolder, &bcfFilePath);

	fileDialog.SetTitle (title);

	FTM::TypeID fileTypeID = FTM::FileTypeManager::SearchForMime ("gs/bcfzip2.0", nullptr);
	fileDialog.AddFilter (fileTypeID);
	fileTypeID = FTM::FileTypeManager::SearchForMime ("gs/bcfzip2.1", nullptr);
	fileDialog.AddFilter (fileTypeID);

	fileDialog.AddFilter (FTM::RootGroup);
	fileDialog.SelectFilter (0);
	fileDialog.SetFolder (bcfFilePath);

	if (!fileDialog.Invoke ())
		return Cancel;

	bcfFilePath = fileDialog.GetSelectedFile ();

	return NoError;
}


static API_IFCTranslatorIdentifier GetExportTranslator ()
{
	GS::Array<API_IFCTranslatorIdentifier> ifcExportTranslators;
	ACAPI_IFC_GetIFCExportTranslatorsList (ifcExportTranslators);

	if (ifcExportTranslators.GetSize () > 0)
		return ifcExportTranslators[0];

    API_IFCTranslatorIdentifier ifcTranslator;
    ifcTranslator.innerReference = nullptr;

	return ifcTranslator;
}


static API_IFCRelationshipData GetCurrentProjectIFCRelationshipData ()
{
	API_IFCRelationshipData ifcRelationshipData;
	API_IFCTranslatorIdentifier ifcTranslator = GetExportTranslator ();

	if (ifcTranslator.innerReference != nullptr)
		ACAPI_IFC_GetIFCRelationshipData (ifcTranslator, ifcRelationshipData);

	return ifcRelationshipData;
}


static GSErrCode GetIFCRelationshipData (GS::HashTable<GS::UniString, API_IFCRelationshipData>* apiIfcRelationshipDataTable, const void* par1)
{
	const API_IFCRelationshipData* ifcRelationshipDataTable = reinterpret_cast<const API_IFCRelationshipData*> (par1);

	GS::UniString ifcProjectId;
	ifcRelationshipDataTable->containmentTable.EnumerateValues ([&](const GS::UniString& value) {
		if (!ifcRelationshipDataTable->containmentTable.ContainsKey (value)) {
			DBASSERT (ifcProjectId.IsEmpty ());
			ifcProjectId = value;
		}
	});
	if (ifcProjectId.IsEmpty () && !ifcRelationshipDataTable->containmentTable.IsEmpty ()) {
		DBASSERT (!ifcProjectId.IsEmpty ());
	}
	apiIfcRelationshipDataTable->Put (ifcProjectId, *ifcRelationshipDataTable);

	return NoError;
}


static GSErrCode GetMarkupEntryIds (GS::Array<API_Guid>& markupEntryIds)
{
	GS::Array<API_MarkUpType> markUps;

	GSErrCode err = ACAPI_Markup_GetList (APINULLGuid, &markUps);

	if (err == NoError) {
		for (const auto& markUp : markUps) {
			markupEntryIds.Push (markUp.guid);
		}
	}

	return err;
}


// =============================================================================
// MarkUpTest namespace implementation
// =============================================================================

GSErrCode MarkUpTest::CreateMarkUp ()
{
	bool hasRight = false;
	ACAPI_Teamwork_GetTWAccessRight (APIMarkupEntryCreate, &hasRight);
	if (!hasRight)
		return APIERR_NOACCESSRIGHT;

	API_MarkUpType markUp (/*name:*/ TestMarkUpName);
	markUp.tagText = "Issue API";

	GSErrCode err = ACAPI_Markup_Create (markUp);
	if (err != NoError)
		ErrorBeep ("ACAPI_Markup_Create failed", err);

	return err;
}


GSErrCode MarkUpTest::DeleteMarkUp (bool acceptElements)
{
	bool hasRight = false;
	ACAPI_Teamwork_GetTWAccessRight (APIMarkupEntryDeleteModify, &hasRight);
	if (!hasRight)
		return APIERR_NOACCESSRIGHT;

	API_MarkUpType markUp;
	if (!GetTestMarkUp (markUp))
		return NoError;

	const GSErrCode err = ACAPI_Markup_Delete (markUp.guid, acceptElements);
	if (err != NoError)
		ErrorBeep ("ACAPI_Markup_Delete failed", err);

	return err;
}


GSErrCode MarkUpTest::AddSelectionToMarkUp (API_MarkUpComponentTypeID type)
{
	API_MarkUpType markUp;
	if (!GetTestMarkUp (markUp))
		return NoError;

	GS::Array<API_Guid> elemList;
	GSErrCode err = GetSelection (elemList);
	if (err != NoError)
		return err;

	err = ACAPI_Markup_AttachElements (markUp.guid, elemList, type);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Markup_AttachElements failed", err);
		return err;
	}

	ACAPI_WriteReport ("ACAPI_Markup_AttachElements succeeded", false);

	return NoError;
}


GSErrCode MarkUpTest::AddSelectionToMarkUpAsModification (ModificationAttachmentType type)
{
	API_MarkUpType markUp;
	if (!GetTestMarkUp (markUp))
		return NoError;

	GS::Array<API_Guid> selectedElements;
	GSErrCode err = GetSelection (selectedElements);
	if (err != NoError)
		return err;

	GS::Array<API_Guid>					originalElements;
	GS::HashTable<API_Guid, API_Guid>	modificationElemTable;
	if (type == SetSelectedPairsAsModification) {
		if (selectedElements.GetSize () % 2 != 0) {
			WriteReport ("The number of selected elements is not even. Each original element should have a modified pair.");
			return APIERR_BADPARS;
		}

		const USize numberOfSelectedPairs = selectedElements.GetSize () / 2;
		for (UIndex ii : GS::InRange<UIndex> (0, numberOfSelectedPairs)) {
			const API_Guid& selectedOriginalElement = selectedElements[ii * 2];
			const API_Guid& selectedModifiedElement = selectedElements[ii * 2 + 1];
			modificationElemTable.Add (selectedOriginalElement, selectedModifiedElement);
		}

		originalElements = modificationElemTable.ConvertKeys ();
	} else if (type == CreateNewElementForModification) {
		originalElements = selectedElements;
	}

	err = ACAPI_Markup_AttachElements (markUp.guid, originalElements, APIMarkUpComponent_Modification, &modificationElemTable);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Markup_AttachElements failed", err);
		return err;
	}

	WriteReport ("Modification(s) was/were successfully attached to Issue (%T):", APIGuidToString (markUp.guid).ToPrintf ());
	for (const auto& pair : modificationElemTable) {
		WriteReport ("\tOriginal element (%T) -> %s (%T)",
					APIGuidToString (pair.key).ToPrintf (),
					type == CreateNewElementForModification ? "Generated new modified element" : "Modified element",
					APIGuidToString (pair.value).ToPrintf ());
	}

	return NoError;
}


GSErrCode MarkUpTest::RemoveSelectionFromMarkUp ()
{
	API_MarkUpType markUp;
	if (!GetTestMarkUp (markUp))
		return NoError;

	GS::Array<API_Guid> elemList;
	GSErrCode err = GetSelection (elemList);
	if (err != NoError)
		return err;

	err = ACAPI_Markup_DetachElements (markUp.guid, elemList);
	if (err != NoError)
		ErrorBeep ("ACAPI_Markup_DetachElements failed", err);

	return err;
}


GSErrCode MarkUpTest::AddCommentToMarkUp ()
{
	API_MarkUpType markUp;
	if (!GetTestMarkUp (markUp))
		return NoError;

	API_MarkUpCommentType comment (/*author:*/ "API", /*text:*/ "ACAPI_Markup_AddComment", /*status:*/ APIComment_Warning);
	GSErrCode err = ACAPI_Markup_AddComment (markUp.guid, comment);
	if (err != NoError)
		ErrorBeep ("ACAPI_Markup_AddComment failed", err);

	return err;
}


void MarkUpTest::DumpMarkUp ()
{
	API_MarkUpType markUp;
	if (GetTestMarkUp (markUp))
		DumpMarkUp (markUp);
}


GSErrCode MarkUpTest::DumpMarkUpForSelection ()
{
	GS::Array<API_Guid> elemList;
	GSErrCode err = GetSelection (elemList);
	if (err != NoError)
		return err;

	elemList.Enumerate ([&] (const API_Guid& elemGuid) {
		GS::Array<API_MarkUpType> markUpList;
		bool asCorrected = false;
		GSErrCode errIn = ACAPI_Markup_GetList (elemGuid, &markUpList, &asCorrected);
		if (errIn != NoError) {
			ErrorBeep ("APIIo_IsElementAttachedToMarkUpID failed", err);
			return;
		}

		WriteReport ("----------------------------------------------");
		WriteReport ("Element with GUID {%s} is %s in the following MarkUp%s:",
						APIGuidToString (elemGuid).ToCStr ().Get (),
						asCorrected ? "corrected" : "highlighted",
						asCorrected ? ""		  : "(s)");
		markUpList.Enumerate ([] (const API_MarkUpType& markUp) {
			DumpMarkUp (markUp);
		});
		WriteReport ("----------------------------------------------");
	});

	return err;
}


static GS::HashTable<API_Guid, API_Guid> GetParentElemSectionElemTable ()
{
	GS::Array<API_Guid> sectElems;
	GSErrCode err = ACAPI_Element_GetElemList (API_SectElemID, &sectElems);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetElemList (API_SectElemID, ...) failed", err);
		return {};
	}

	GS::HashTable<API_Guid, API_Guid> parentElemSectionElemTable;
	for (const API_Guid& sectElemGuid : sectElems) {
		API_Element sectElem = {};
		sectElem.header.type = API_SectElemID;
		sectElem.header.guid = sectElemGuid;
		err = ACAPI_Element_Get (&sectElem);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Get failed", err);
			continue;
		}

		parentElemSectionElemTable.Add (sectElem.sectElem.parentGuid, sectElem.header.guid);
	}

	return parentElemSectionElemTable;
}


GSErrCode MarkUpTest::ZoomToElementsOfMarkUp ()
{
	API_MarkUpType markUp;
	if (!GetTestMarkUp (markUp)) {
		return NoError;
	}

	GS::Array<API_Guid> highlightedElems;
	GSErrCode err = ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Highlight, highlightedElems);
	GS::Array<API_Guid> modifiedElems;
	err |= ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Modification, modifiedElems);
	GS::Array<API_Guid> createdElems;
	err |= ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Creation, createdElems);
	GS::Array<API_Guid> deletedElems;
	err |= ACAPI_Markup_GetAttachedElements (markUp.guid, APIMarkUpComponent_Deletion, deletedElems);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Markup_GetAttachedElements failed", err);
		return err;
	}

	GS::Array<API_Guid> elemList;
	elemList.Append (highlightedElems);
	elemList.Append (modifiedElems);
	elemList.Append (createdElems);
	elemList.Append (deletedElems);
	if (markUp.isTagTextElemVisible && markUp.tagTextElemGuid != APINULLGuid)
		elemList.Push (markUp.tagTextElemGuid);

	GS::HashTable<API_Guid, API_Guid> parentElemSectionElemTable = GetParentElemSectionElemTable ();
	for (API_Guid& elemGuid : elemList) {
		const API_Guid& sectElemGuid = parentElemSectionElemTable.Retrieve (elemGuid, APINULLGuid);
		if (sectElemGuid != APINULLGuid) {
			elemGuid = sectElemGuid;
		}
	}

	err = ACAPI_View_ZoomToElements (&elemList);
	if (err != NoError) {
		ErrorBeep ("APIDo_ZoomToElementsID failed", err);
	}

	return err;
}


GSErrCode MarkUpTest::ImportFromBCFFileSilentModeFalse ()
{
	IO::Location bcfFilePath;

	GSErrCode err = GetBCFFileLocation (DG::FileDialog::OpenFile, "Import BCF File", bcfFilePath);
	if (err == NoError) {

		API_IFCRelationshipData ifcRelationshipData = GetCurrentProjectIFCRelationshipData ();
		GSErrCode err = ACAPI_Markup_ImportFromBCF (bcfFilePath, false, &GetIFCRelationshipData, &ifcRelationshipData, true);

		if (err != NoError)
			ErrorBeep ("ACAPI_Markup_ImportFromBCF failed", err);
	}

	return err;
}


GSErrCode MarkUpTest::ImportFromBCFFileSilentModeTrue ()
{
	IO::Location bcfFilePath;

	GSErrCode err = GetBCFFileLocation (DG::FileDialog::OpenFile, "Import BCF File", bcfFilePath);
	if (err == NoError) {

		API_IFCRelationshipData ifcRelationshipData = GetCurrentProjectIFCRelationshipData ();
		err = ACAPI_Markup_ImportFromBCF (bcfFilePath, true, &GetIFCRelationshipData, &ifcRelationshipData);

		if (err != NoError)
			ErrorBeep ("ACAPI_Markup_ImportFromBCF failed", err);
	}

	return err;
}


GSErrCode MarkUpTest::ExportToBCF ()
{
	IO::Location bcfFilePath;

	GSErrCode err = GetBCFFileLocation (DG::FileDialog::Save, "Export BCF File", bcfFilePath);

	if (err == NoError) {
		GS::Array<API_Guid> markupEntryIds;

		err = GetMarkupEntryIds (markupEntryIds);

		if (err == NoError)
			err = ACAPI_Markup_ExportToBCF (bcfFilePath, markupEntryIds, false, true);

		if (err != NoError)
			ErrorBeep ("ACAPI_Markup_ExportToBCF failed", err);
	}

	return err;
}
