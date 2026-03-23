#ifndef __FORMATOPTIONSDIALOG_HPP__
#define __FORMATOPTIONSDIALOG_HPP__

#include "DGModule.hpp"
#include "FormatTranslator.hpp"


class FormatOptionDialog :  public DG::ModalDialog,
							public DG::ButtonItemObserver
{
private:
	enum {
		OkButtonId		  = 1,
		CancelButtonId	  = 2,
		StaticTextId	  = 3,
		TranslatorPopupId = 4
	};

private:
	DG::Button							okButton;
	DG::Button							cancelButton;
	DG::LeftText						staticText;
	DG::PopUp							translatorPopUp;

	FormatTranslator						selectedTranslator;
	GS::HashTable<GS::UniString, GS::Guid>  translatorNameGuidTable;

private:
	void InitTranslatorPopUp (const GS::Array<FormatTranslator>& translatorList);

protected:
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;

public:
	FormatOptionDialog (const GS::Array<FormatTranslator>& translatorList, const FormatTranslator& selectedTranslator, GSResModule resModule);
	~FormatOptionDialog ();

	const FormatTranslator&		GetFormatTranslator () const { return selectedTranslator; }
};

#endif // __FORMATOPTIONSDIALOG_HPP__
