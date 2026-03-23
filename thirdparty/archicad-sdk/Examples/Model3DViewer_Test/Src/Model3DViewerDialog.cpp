#include "Model3DViewerDialog.hpp"
#include "IAttributeReader.hpp"
#include "BackgroundGridGenerator.hpp"


static constexpr short DialogResId = 32600;
static constexpr short CloseButtonResId = 1;
static constexpr short Model3DItemResId = 2;


Model3DViewerTestDialog::Model3DViewerTestDialog (const Modeler::Model3DPtr& model, const ModelViewer::ICameraControllerRef& camera, bool showGrid) :
	DG::ModalDialog (ACAPI_GetOwnResModule (), DialogResId, ACAPI_GetOwnResModule ()),
	item (GetReference (), Model3DItemResId, camera, ACAPI_Attribute_GetCurrentAttributeSetReader ()),
	closeButton (GetReference (), CloseButtonResId)
{
	Attach (*this);
	closeButton.Attach (*this);

	item.SetModel (model);

	if (showGrid) {
		item.AddExtraModel (GS::Guid { }, GS::NewRef<ModelViewer::BackgroundGridGenerator> (10, 10, GS_RGBColor { 52428 , 52428 , 52428 }, GS_RGBColor { 58981 , 58981 , 58981 }));
	}
}


void Model3DViewerTestDialog::PanelResized (const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems ();
	item.MoveAndResize (0, 0, ev.GetHorizontalChange (), ev.GetVerticalChange ());
	closeButton.Move (ev.GetHorizontalChange (), ev.GetVerticalChange ());
	EndMoveResizeItems ();
}


void Model3DViewerTestDialog::ButtonClicked (const DG::ButtonClickEvent& ev)
{
	if (ev.GetSource () == &closeButton) {
		PostCloseRequest (DG::ModalDialog::Accept);
	}
}