// *****************************************************************************
// Source code for the Owner Drawn ListBox Dialog in DG Test Add-On
// *****************************************************************************

#include	"OwnerDrawnListBoxPalette.hpp"
#include	"DGTestResIDs.hpp"

#include	"DGNativeContexts.hpp"
#include	"NativeImage.hpp"
#include	"TextEngine.hpp"

const GS::Guid& OwnerDrawnListBoxPalette::ODLBPaletteGuid () {
	// We need a fix and unique GUID to construct the palette to make it dockable.
	static GS::Guid guid ("B69AED35-3C1F-4246-8598-3C5A4013FD40");
	return guid;
}

Int32 OwnerDrawnListBoxPalette::ODLBPaletteRefId () {
	static Int32 refId (GS::CalculateHashValue (ODLBPaletteGuid ()));
	return refId;
}


GSErrCode OwnerDrawnListBoxPalette::PaletteAPIControlCallBack (Int32 referenceID, API_PaletteMessageID messageID, GS::IntPtr param)
{
	static bool paletteOpenAtClose = false;
	GSErrCode err = NoError;
	if (referenceID == OwnerDrawnListBoxPalette::ODLBPaletteRefId ()) {
		OwnerDrawnListBoxPalette& palette = OwnerDrawnListBoxPalette::GetInstance();
		switch (messageID) {
			case APIPalMsg_ClosePalette:
			case APIPalMsg_HidePalette_Begin:
				if (palette.IsVisible()) {
					paletteOpenAtClose = true;
					palette.Hide();
				} else {
					paletteOpenAtClose = false;
				}
				break;

			case APIPalMsg_OpenPalette:
				paletteOpenAtClose = true;
				// no break

			case APIPalMsg_HidePalette_End:
				if (paletteOpenAtClose && !palette.IsVisible()) {
					palette.Show ();
				}
				break;

			case APIPalMsg_DisableItems_Begin:
				palette.DisableItems();
				break;

			case APIPalMsg_DisableItems_End:
				palette.EnableItems();
				break;

			case APIPalMsg_IsPaletteVisible:
				(*reinterpret_cast<bool*> (param)) = palette.IsVisible();
				break;

			default:
				break;
		}
	}

	return err;
}


//------------------------ Class OwnerDrawnListBoxDialog -----------------------

OwnerDrawnListBoxPalette::OwnerDrawnListBoxPalette ():
	DG::Palette	(ACAPI_GetOwnResModule (), DGTEST_OWNERDRAWNLISTBOXPLT_RESID, ACAPI_GetOwnResModule (), ODLBPaletteGuid ()),

	okButton			(GetReference (), OkButtonId),
	switchImagesButton	(GetReference (), SwitchImagesButtonId),
    getButton			(GetReference (), GetButtonId),
	listBox				(GetReference (), ListBoxId)
{
	AutoResizeBlock (listBox.HVGrow, switchImagesButton.HVMove, getButton.HVMove, okButton.HVMove);

	// initialize the listbox
	listBox.SetHeaderSynchronState (false);
	listBox.SetTabFieldCount (NTabFields);
	listBox.SetHeaderItemCount (NTabFields);

	const short listBoxTab1Width = 151;
	const short listBoxTab2Width = 350;
	const short listBoxTab3Width = 200;
	short pos = 0;
	listBox.SetTabFieldProperties (Tab1, pos, pos + listBoxTab1Width, DG::ListBox::Center,	DG::ListBox::NoTruncate, false);
	pos += listBoxTab1Width;
	listBox.SetTabFieldProperties (Tab2, pos, pos + listBoxTab2Width, DG::ListBox::Left,	DG::ListBox::NoTruncate, false);
	pos += listBoxTab2Width;
	listBox.SetTabFieldProperties (Tab3, pos, pos + listBoxTab3Width, DG::ListBox::Left,	DG::ListBox::EndTruncate, false);

	// Set OwnerDraw Flag to catch ListBoxTabFieldUpdate Event:
	listBox.SetTabFieldOwnerDrawFlag (Tab1, true);
	listBox.SetTabFieldOwnerDrawFlag (Tab2, true);
	listBox.SetTabFieldOwnerDrawFlag (Tab3, true);

	// init observer
	this->Attach (*this);
	AttachToAllItems (*this);

	this->BeginEventProcessing();

	GetObjects ();
}


OwnerDrawnListBoxPalette::~OwnerDrawnListBoxPalette ()
{
	this->EndEventProcessing ();
	this->Detach (*this);
	DetachFromAllItems (*this);
}

OwnerDrawnListBoxPalette&	OwnerDrawnListBoxPalette::GetInstance ()
{
	static OwnerDrawnListBoxPalette	instance;
	return instance;
}


void OwnerDrawnListBoxPalette::PanelOpened (const DG::PanelOpenEvent& /*ev*/)
{
	SetClientSize (GetOriginalClientWidth (), GetOriginalClientHeight ());
}


void OwnerDrawnListBoxPalette::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &okButton) {
		Hide ();
	} else if (ev.GetSource () == &switchImagesButton) {
		data.state = ++data.state % 3;

		data.elementImages.Clear ();
		for (short i = 1; i <= listBox.GetItemCount (); ++i)
			listBox.RedrawTabItem (i, Tab1);
		// redraw all listbox:
		// listBox.Redraw ();
		// specific list item can be redrawn with RedrawItem function:
		// listBox.RedrawItem (listItemIndex);
	} else if (ev.GetSource () == &getButton) {
		GetObjects ();
	}
}


void OwnerDrawnListBoxPalette::ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& ev)
{
	if (ev.GetSource () == &listBox) {
		short listItemIndex = listBox.GetSelectedItem ();

		if (listItemIndex <= listBox.GetItemCount () && listItemIndex > 0) {
			// handle list item selection here
		}
	}
}


void OwnerDrawnListBoxPalette::PanelCloseRequested(const DG::PanelCloseRequestEvent& /*ev*/, bool* /*accepted*/)
{
	Hide ();
}


void OwnerDrawnListBoxPalette::UpdateTab1 (const DG::ListBoxTabItemUpdateEvent& ev)
{
	short item = ev.GetListItem ();
	if (item < 1 || (short) data.elements.GetSize () < item)
		return;

	// Is image already generated?
	if (data.elementImages.ContainsKey (data.elements[item - 1])) {
		NewDisplay::NativeImage&				nativeImage = data.elementImages.Get (data.elements[item - 1]);
		NewDisplay::ListBoxUpdateEventContext	context (ev);
		const short padding = 3;
		const short imageWidth   = ev.GetWidth () - padding * 2;
		const short imageHeight  = ev.GetHeight () - padding * 2;
		const float scaleX = (float) imageWidth / nativeImage.GetWidth ();
		const float scaleY = (float) imageHeight / nativeImage.GetHeight ();
		const float scale = fabs (scaleX) < fabs (scaleY) ? scaleX : scaleY;

		// Draw Image
		context.DrawImage (nativeImage, scale, scale, 0.0, padding, padding, true);
		// Draw Rect for the image
		context.FrameRect (padding, padding, padding + scale * nativeImage.GetWidth (), padding + scale * nativeImage.GetHeight ());
	} else {
		// Generate Image in a new thread
		class GenerateElementImageTask : public GS::Runnable {
		private:
			short item;
			OwnerDrawnListBoxPalette::Data& data;
			DG::SingleSelListBox& listBox;

		public:
			explicit GenerateElementImageTask (const short& item, OwnerDrawnListBoxPalette::Data& data, DG::SingleSelListBox& listBox) :
				item (item),
				data (data),
				listBox (listBox)
			{}

			virtual void Run () {
				if (item < 1 || (short) data.elements.GetSize () < item)
					return;

				NewDisplay::NativeImage		nativeImage (250, 250, 32, nullptr);
				API_VisualOverriddenImage	image = {};
				image.view = (API_ImageViewID) data.state;
				image.nativeImagePtr = &nativeImage;
				const API_Guid& guid = data.elements[item - 1];

				ACAPI_GraphicalOverride_GetVisualOverriddenImage (guid, &image);
				BMhFree (image.vectorImageHandle);

				data.elementImages.Add (guid, nativeImage);
				listBox.RedrawTabItem (item, Tab1);
			}
		};

		executor.Execute (new GenerateElementImageTask (item, data, listBox));
	}
}


void OwnerDrawnListBoxPalette::UpdateTab2 (const DG::ListBoxTabItemUpdateEvent& ev)
{
	const short item = ev.GetListItem ();
	if (item < 1)
		return;

	NewDisplay::ListBoxUpdateEventContext context (ev);
	const short height  = ev.GetHeight ();

	// Draw Text
	TE::Font fontBold;
	TE::Font fontPlain;
	DG::GetFont (listBox.GetFontSize (), DG::Font::Bold, &fontBold);
	DG::GetFont (listBox.GetFontSize (), DG::Font::Plain, &fontPlain);
	const double padding = fontBold.GetCapISize () * 1.5;
	double top = padding;

	context.SetForeColor ((item % 2) ? Gfx::Color::Green : Gfx::Color::Red);
	context.DrawPlainText (GS::UniString::Printf ("#%d Item", item), fontBold, padding, top, 0);
	top += 1.8 * fontBold.GetCapISize ();
	context.SetForeColor (Gfx::Color::Black);

	context.DrawPlainText ("example string1", fontPlain, padding, top, 0);
	top += 1.8 * fontPlain.GetCapISize ();
	context.DrawPlainText ("example string2", fontPlain, padding, top, 0);

	context.DrawPlainText (GS::UniString::Printf ("%02d-01-2017", item % 31 + 1), fontPlain, padding, height - fontPlain.GetCapISize (), 0);
}


void OwnerDrawnListBoxPalette::UpdateTab3 (const DG::ListBoxTabItemUpdateEvent& ev)
{
	short item = ev.GetListItem ();
	if (item < 1 || (short) data.elements.GetSize () < item)
		return;

	NewDisplay::ListBoxUpdateEventContext context (ev);

	API_Elem_Head elemHead = {};
	elemHead.guid = data.elements[item - 1];
	ACAPI_Element_GetHeader (&elemHead);

	// Draw Text
	TE::Font fontPlain;
	DG::GetFont (listBox.GetFontSize (), DG::Font::Plain, &fontPlain);
	const double padding = fontPlain.GetCapISize () * 1.5;
	double top = padding;

	GS::UniString str ("Description");
	context.DrawPlainText (str, fontPlain, padding, top, 0);
	double textWidth = 0.0;
	TE::textEngine.MeasurePlainText (str, fontPlain, &textWidth);
	ACAPI_Element_GetElementInfoString (&elemHead.guid, &str);
	context.DrawPlainText (str, fontPlain, padding + 1.5 * textWidth, top, 0);
	top += 2.0 * 1.8 * fontPlain.GetCapISize ();

	str = "Type";
	context.DrawPlainText (str, fontPlain, padding, top, 0);
	ACAPI_Element_GetElemTypeName (elemHead.type, str);
	//ACAPI_Goodies (APIAny_GetElemTypeNameID, &elemHead.typeID, &str);
	context.DrawPlainText (str, fontPlain, padding + 1.5 * textWidth, top, 0);
}

void OwnerDrawnListBoxPalette::GetObjects()
{
	listBox.DeleteItem (DG::ListBox::AllItems);
	// add items to the listbox
	data.elements.Clear (true /*keepCapacity*/);
	data.elementImages.Clear ();
	ACAPI_Element_GetElemList (API_ZombieElemID, &data.elements);
	for (UIndex i = 0; i < data.elements.GetSize (); ++i) {
		listBox.AppendItem ();
		if (i % 2)
			listBox.SetItemBackgroundColor (DG_LIST_BOTTOM, Gfx::Color::LightGray);
	}
}


void OwnerDrawnListBoxPalette::ListBoxTabFieldUpdate (const DG::ListBoxTabItemUpdateEvent& ev)
{
	if (ev.GetSource () == &listBox) {
		switch (ev.GetTabFieldIndex ()) {
			case Tab1:
				UpdateTab1 (ev);
				break;
			case Tab2:
				UpdateTab2 (ev);
				break;
			case Tab3:
				UpdateTab3 (ev);
				break;
			default:
				DBBREAK ();
				break;
		}
	}
}
