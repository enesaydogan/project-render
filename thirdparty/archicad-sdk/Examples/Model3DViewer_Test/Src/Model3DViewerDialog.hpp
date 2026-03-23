#ifndef	MODEL3DVIEWER_DIALOG_HPP
#define MODEL3DVIEWER_DIALOG_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGModule.hpp"
#include "Model3DItem.hpp"


class Model3DViewerTestDialog : public DG::ModalDialog,
								public DG::ButtonItemObserver,
								public DG::PanelObserver {
public:
	Model3DViewerTestDialog (const Modeler::Model3DPtr& model, const ModelViewer::ICameraControllerRef& camera, bool showGrid);

private:
	virtual void	PanelResized (const DG::PanelResizeEvent& ev) override;
	virtual void	ButtonClicked (const DG::ButtonClickEvent& ev) override;

private:
	ModelViewer::Model3DItem		item;
	DG::Button						closeButton;
};


#endif