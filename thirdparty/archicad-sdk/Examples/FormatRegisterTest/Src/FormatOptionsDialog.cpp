#include "FormatOptionsDialog.hpp"
#include "Resources.hpp"


FormatOptionDialog::FormatOptionDialog (const GS::Array<FormatTranslator>& translatorList, const FormatTranslator& selectedTranslator, GSResModule resModule) :
	ModalDialog (resModule, FORMATOPTION_DIALOG_ID, resModule),

	okButton			(GetReference (), OkButtonId),
	cancelButton		(GetReference (), CancelButtonId),
	staticText			(GetReference (), StaticTextId),
	translatorPopUp		(GetReference (), TranslatorPopupId),

	selectedTranslator  (selectedTranslator)
{
	okButton.Attach (*this);
	cancelButton.Attach (*this);

	InitTranslatorPopUp (translatorList);
}


FormatOptionDialog::~FormatOptionDialog ()
{
	okButton.Detach (*this);
	cancelButton.Detach (*this);
}


void FormatOptionDialog::InitTranslatorPopUp (const GS::Array<FormatTranslator>& translatorList)
{
	short counter = 0;
	for (const FormatTranslator& translator : translatorList) {
		translatorNameGuidTable.Add (translator.GetName (), translator.GetGuid ());
		translatorPopUp.AppendItem ();
		translatorPopUp.SetItemText (++counter, translator.GetName ());
		if (translator.GetName ().IsEqual (selectedTranslator.GetName ()))
			translatorPopUp.SelectItem (counter);
	}
}


void FormatOptionDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		selectedTranslator.SetName (translatorPopUp.GetItemText (translatorPopUp.GetSelectedItem ()));
		selectedTranslator.SetGuid (translatorNameGuidTable[selectedTranslator.GetName ()]);
		PostCloseRequest (DG::ModalDialog::Accept);
	} else if (ev.GetSource () == &cancelButton) {
		PostCloseRequest (DG::ModalDialog::Cancel);
	}
}
