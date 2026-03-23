// *****************************************************************************
// Description:		Header file for the Markup Test
// *****************************************************************************

#if !defined _MARKUP_TEST_H
#define _MARKUP_TEST_H

enum ModificationAttachmentType { CreateNewElementForModification, SetSelectedPairsAsModification };

namespace MarkUpTest {
	GSErrCode	CreateMarkUp ();
	GSErrCode	DeleteMarkUp (bool acceptElement);
	GSErrCode	AddSelectionToMarkUp (API_MarkUpComponentTypeID type);
	GSErrCode	AddSelectionToMarkUpAsModification (ModificationAttachmentType type);
	GSErrCode	RemoveSelectionFromMarkUp ();
	GSErrCode	AddCommentToMarkUp ();
	void		DumpMarkUp ();
	GSErrCode	DumpMarkUpForSelection ();
	GSErrCode	ZoomToElementsOfMarkUp ();
	GSErrCode	ImportFromBCFFileSilentModeFalse ();
	GSErrCode	ImportFromBCFFileSilentModeTrue ();
	GSErrCode	ExportToBCF ();
}

#endif
