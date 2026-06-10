#include "DX12View.h"
#include "../asset_library/asset_id.h"
#include "../dx12_context.h" // adapt include path
#include "../volumetric_renderer.h"
#include "../dxr_renderer.h"
#include "../editor_ui.h"
#include "../input_handler.h"
#include "../imgui.h"
#include "../material_editor.h"
#include "../scene.h"
#include "asset_mime.h"
#include <algorithm>
#include <cfloat>
#include <vector>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

namespace {

class DashedSelectionBand : public QWidget
{
public:
    explicit DashedSelectionBand(QWidget *parent)
        : QWidget(parent,
                  Qt::Tool |
                      Qt::FramelessWindowHint |
                      Qt::WindowStaysOnTopHint |
                      Qt::WindowDoesNotAcceptFocus |
                      Qt::NoDropShadowWindowHint)
    {
        setObjectName(QStringLiteral("BoxSelectionOverlay"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(69, 196, 238, 28));
        QPen pen(QColor(69, 196, 238, 230), 1.0, Qt::DashLine,
                 Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        QRect border = rect().adjusted(0, 0, -1, -1);
        painter.drawRect(border);
    }
};

int MapQtKeyToVirtualKey(QKeyEvent *event)
{
    const quint32 native = event->nativeVirtualKey();
    if (native != 0) {
        return static_cast<int>(native);
    }

    switch (event->key()) {
    case Qt::Key_W: return 'W';
    case Qt::Key_A: return 'A';
    case Qt::Key_S: return 'S';
    case Qt::Key_D: return 'D';
    case Qt::Key_Q: return 'Q';
    case Qt::Key_E: return 'E';
    case Qt::Key_G: return 'G';
    case Qt::Key_R: return 'R';
    case Qt::Key_T: return 'T';
    case Qt::Key_L: return 'L';
    case Qt::Key_Shift: return VK_SHIFT;
    case Qt::Key_Control: return VK_CONTROL;
    case Qt::Key_Alt: return VK_MENU;
    case Qt::Key_F4: return VK_F4;
    case Qt::Key_F5: return VK_F5;
    default: return 0;
    }
}

ImGuiKey MapQtKeyToImGuiKey(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Tab: return ImGuiKey_Tab;
    case Qt::Key_Left: return ImGuiKey_LeftArrow;
    case Qt::Key_Right: return ImGuiKey_RightArrow;
    case Qt::Key_Up: return ImGuiKey_UpArrow;
    case Qt::Key_Down: return ImGuiKey_DownArrow;
    case Qt::Key_PageUp: return ImGuiKey_PageUp;
    case Qt::Key_PageDown: return ImGuiKey_PageDown;
    case Qt::Key_Home: return ImGuiKey_Home;
    case Qt::Key_End: return ImGuiKey_End;
    case Qt::Key_Insert: return ImGuiKey_Insert;
    case Qt::Key_Delete: return ImGuiKey_Delete;
    case Qt::Key_Backspace: return ImGuiKey_Backspace;
    case Qt::Key_Space: return ImGuiKey_Space;
    case Qt::Key_Enter:
    case Qt::Key_Return: return ImGuiKey_Enter;
    case Qt::Key_Escape: return ImGuiKey_Escape;
    case Qt::Key_A: return ImGuiKey_A;
    case Qt::Key_C: return ImGuiKey_C;
    case Qt::Key_D: return ImGuiKey_D;
    case Qt::Key_E: return ImGuiKey_E;
    case Qt::Key_G: return ImGuiKey_G;
    case Qt::Key_L: return ImGuiKey_L;
    case Qt::Key_Q: return ImGuiKey_Q;
    case Qt::Key_R: return ImGuiKey_R;
    case Qt::Key_S: return ImGuiKey_S;
    case Qt::Key_T: return ImGuiKey_T;
    case Qt::Key_V: return ImGuiKey_V;
    case Qt::Key_W: return ImGuiKey_W;
    case Qt::Key_X: return ImGuiKey_X;
    case Qt::Key_Y: return ImGuiKey_Y;
    case Qt::Key_Z: return ImGuiKey_Z;
    case Qt::Key_F2: return ImGuiKey_F2;
    case Qt::Key_F4: return ImGuiKey_F4;
    case Qt::Key_F5: return ImGuiKey_F5;
    default: return ImGuiKey_None;
    }
}

void ResetImGuiInputs()
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, false);
    io.AddMouseButtonEvent(1, false);
    io.AddMouseButtonEvent(2, false);
    io.ClearInputKeys();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

bool IsSupportedDroppedModelPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("skp") ||
           suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb") ||
           suffix == QStringLiteral("obj") ||
           suffix == QStringLiteral("stl") ||
           suffix == QStringLiteral("fbx") ||
           suffix == QStringLiteral("ltm") ||
           suffix == QStringLiteral("lmod");
}

QString FirstSupportedDroppedModelPath(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls()) {
        return {};
    }
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (IsSupportedDroppedModelPath(path)) {
            return path;
        }
    }
    return {};
}

int MapQtMouseButtonToVirtualKey(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return VK_LBUTTON;
    case Qt::RightButton: return VK_RBUTTON;
    case Qt::MiddleButton: return VK_MBUTTON;
    default: return 0;
    }
}

} // namespace

DX12View::DX12View(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);

    m_cloneOptionsTimer = new QTimer(this);
    connect(m_cloneOptionsTimer, &QTimer::timeout, this, [this]() {
        if (Scene::HasPendingCloneOptions()) {
            showPendingCloneOptions();
        }
    });
    m_cloneOptionsTimer->start(50);
}

DX12View::~DX12View()
{
}

void DX12View::focusInEvent(QFocusEvent *e)
{
    DxrRenderer::RequestInteractiveWake("Qt viewport focus");
    Input::SetQtWidgetFocused(true);
    ImGui::GetIO().AddFocusEvent(true);
    QWidget::focusInEvent(e);
}

void DX12View::focusOutEvent(QFocusEvent *e)
{
    Input::SetQtWidgetFocused(false);
    Input::ResetQtInputState();
    m_hasLastMousePos = false;
    cancelBoxSelection();
    ImGui::GetIO().AddFocusEvent(false);
    ResetImGuiInputs();
    QWidget::focusOutEvent(e);
}

void DX12View::leaveEvent(QEvent *e)
{
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    QWidget::leaveEvent(e);
}

void DX12View::keyPressEvent(QKeyEvent *e)
{
    const int virtualKey = MapQtKeyToVirtualKey(e);
    if (virtualKey != 0) {
        Input::SetQtKeyState(virtualKey, true);
    }
    const ImGuiKey imguiKey = MapQtKeyToImGuiKey(e);
    if (imguiKey != ImGuiKey_None) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(imguiKey, true);
        io.SetKeyEventNativeData(imguiKey, static_cast<int>(e->nativeVirtualKey()),
                                 static_cast<int>(e->nativeScanCode()));
    }
    ImGuiIO &io = ImGui::GetIO();
    io.KeyCtrl = e->modifiers().testFlag(Qt::ControlModifier);
    io.KeyShift = e->modifiers().testFlag(Qt::ShiftModifier);
    if (e->key() == Qt::Key_Escape && !e->isAutoRepeat()) {
        if (IsPreviewRenderActive()) {
            CancelPreviewRender();
        } else if (Scene::IsLightPlacementActive()) {
            Scene::CancelLightPlacement();
        } else {
            Scene::SelectNode(static_cast<size_t>(-1));
            Scene::SelectLight(-1);
        }
    }
    if (e->key() == Qt::Key_Delete && !e->isAutoRepeat()) {
        const std::vector<size_t> selectedLights = Scene::GetSelectedLightIndices();
        if (!selectedLights.empty()) {
            Scene::RemoveLightInstances(selectedLights);
            e->accept();
            return;
        }
    }
    QWidget::keyPressEvent(e);
}

void DX12View::keyReleaseEvent(QKeyEvent *e)
{
    const int virtualKey = MapQtKeyToVirtualKey(e);
    if (virtualKey != 0) {
        Input::SetQtKeyState(virtualKey, false);
    }
    const ImGuiKey imguiKey = MapQtKeyToImGuiKey(e);
    if (imguiKey != ImGuiKey_None) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddKeyEvent(imguiKey, false);
        io.SetKeyEventNativeData(imguiKey, static_cast<int>(e->nativeVirtualKey()),
                                 static_cast<int>(e->nativeScanCode()));
    }
    ImGuiIO &io = ImGui::GetIO();
    io.KeyCtrl = e->modifiers().testFlag(Qt::ControlModifier);
    io.KeyShift = e->modifiers().testFlag(Qt::ShiftModifier);
    QWidget::keyReleaseEvent(e);
}

void DX12View::mousePressEvent(QMouseEvent *e)
{
    DxrRenderer::RequestInteractiveWake("Qt viewport mouse press");
    setFocus(Qt::MouseFocusReason);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(e->globalPosition().x()),
                                    static_cast<float>(e->globalPosition().y()));

    if (e->button() == Qt::LeftButton && Scene::IsScatterPickingTarget()) {
        const QPointF pickPos = e->globalPosition();
        if (Scene::HandleScatterPick(static_cast<float>(pickPos.x()),
                                     static_cast<float>(pickPos.y()),
                                     static_cast<float>(DX12Context::g_windowWidth),
                                     static_cast<float>(DX12Context::g_windowHeight))) {
            e->accept();
            return;
        }
    }

    if (e->button() == Qt::LeftButton && Scene::IsLightPlacementActive()) {
        const QPointF pickPos = e->globalPosition();
        if (Scene::HandleLightPlacement(
                static_cast<float>(pickPos.x()),
                static_cast<float>(pickPos.y()),
                static_cast<float>(DX12Context::g_windowWidth),
                static_cast<float>(DX12Context::g_windowHeight))) {
            e->accept();
            return;
        }
    }

    if (e->button() == Qt::LeftButton && MaterialEditor::IsPickingEnabled()) {
        const QPointF pickPos = e->globalPosition();
        const int pickedMaterial = Scene::PickMaterialAt(
            static_cast<float>(pickPos.x()),
            static_cast<float>(pickPos.y()),
            static_cast<float>(DX12Context::g_windowWidth),
            static_cast<float>(DX12Context::g_windowHeight));
        if (pickedMaterial >= 0) {
            MaterialEditor::SelectMaterial(pickedMaterial);
            MaterialEditor::SetPickingEnabled(false);
        }
        e->accept();
        return;
    }

    if (e->button() == Qt::LeftButton &&
        Scene::GetSelectionToolMode() == Scene::SelectionToolMode::Box &&
        !Scene::IsTransformGizmoHitAt(
            static_cast<float>(e->globalPosition().x()),
            static_cast<float>(e->globalPosition().y()),
            static_cast<float>(DX12Context::g_windowWidth),
            static_cast<float>(DX12Context::g_windowHeight))) {
        m_boxSelecting = true;
        m_boxStartGlobalPos = e->globalPosition();
        m_boxStartLocalPos = e->position().toPoint();
        if (!m_boxSelectionBand) {
            m_boxSelectionBand = new DashedSelectionBand(this);
        }
        updateBoxSelectionBand(m_boxStartGlobalPos);
        e->accept();
        return;
    }

    const int virtualKey = MapQtMouseButtonToVirtualKey(e->button());
    if (virtualKey != 0) {
        Input::SetQtMouseButtonState(virtualKey, true);
    }
    if (e->button() == Qt::LeftButton) {
        ImGui::GetIO().AddMouseButtonEvent(0, true);
    } else if (e->button() == Qt::RightButton) {
        ImGui::GetIO().AddMouseButtonEvent(1, true);
    } else if (e->button() == Qt::MiddleButton) {
        ImGui::GetIO().AddMouseButtonEvent(2, true);
    }
    m_lastGlobalMousePos = e->globalPosition();
    m_hasLastMousePos = true;
    QWidget::mousePressEvent(e);
}

void DX12View::mouseReleaseEvent(QMouseEvent *e)
{
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(e->globalPosition().x()),
                                    static_cast<float>(e->globalPosition().y()));

    if (m_boxSelecting && e->button() == Qt::LeftButton) {
        if (m_boxSelectionBand) {
            m_boxSelectionBand->hide();
        }
        const QRect localRect =
            QRect(m_boxStartLocalPos, e->position().toPoint()).normalized();
        const bool tinyDrag = localRect.width() < 4 && localRect.height() < 4;
        const QPointF endPos = tinyDrag
            ? QPointF(m_boxStartGlobalPos.x() + 3.0, m_boxStartGlobalPos.y() + 3.0)
            : e->globalPosition();
        Scene::BoxSelect(static_cast<float>(m_boxStartGlobalPos.x()),
                         static_cast<float>(m_boxStartGlobalPos.y()),
                         static_cast<float>(endPos.x()),
                         static_cast<float>(endPos.y()),
                         static_cast<float>(DX12Context::g_windowWidth),
                         static_cast<float>(DX12Context::g_windowHeight),
                         e->modifiers().testFlag(Qt::ControlModifier));
        m_boxSelecting = false;
        e->accept();
        return;
    }

    const int virtualKey = MapQtMouseButtonToVirtualKey(e->button());
    if (virtualKey != 0) {
        Input::SetQtMouseButtonState(virtualKey, false);
    }
    if (e->button() == Qt::LeftButton) {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    } else if (e->button() == Qt::RightButton) {
        ImGui::GetIO().AddMouseButtonEvent(1, false);
    } else if (e->button() == Qt::MiddleButton) {
        ImGui::GetIO().AddMouseButtonEvent(2, false);
    }
    QWidget::mouseReleaseEvent(e);
}

void DX12View::mouseMoveEvent(QMouseEvent *e)
{
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(e->globalPosition().x()),
                                    static_cast<float>(e->globalPosition().y()));
    if (m_boxSelecting) {
        updateBoxSelectionBand(e->globalPosition());
        e->accept();
        return;
    }
    if (m_hasLastMousePos && (e->buttons() & Qt::RightButton)) {
        const QPointF delta = e->globalPosition() - m_lastGlobalMousePos;
        Input::AddQtMouseDelta(static_cast<float>(delta.x()),
                               static_cast<float>(delta.y()));
    }
    m_lastGlobalMousePos = e->globalPosition();
    m_hasLastMousePos = true;
    QWidget::mouseMoveEvent(e);
}

void DX12View::wheelEvent(QWheelEvent *e)
{
    DxrRenderer::RequestInteractiveWake("Qt viewport wheel");
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(e->globalPosition().x()),
                                    static_cast<float>(e->globalPosition().y()));
    const QPoint angleDelta = e->angleDelta();
    ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(angleDelta.x()) / 120.0f,
                                      static_cast<float>(angleDelta.y()) / 120.0f);
    QWidget::wheelEvent(e);
}

void DX12View::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    DxrRenderer::RequestInteractiveWake("Qt viewport resize");
    DX12Context::QueueResize(static_cast<UINT>(width()),
                             static_cast<UINT>(height()));
}

void DX12View::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData() && e->mimeData()->hasFormat(kAssetMimeType)) {
        e->acceptProposedAction();
        return;
    }
    if (!FirstSupportedDroppedModelPath(e->mimeData()).isEmpty()) {
        e->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(e);
}

void DX12View::dropEvent(QDropEvent *e)
{
    // Asset Manager drag-and-drop (model → instantiate, material → assign).
    if (e->mimeData() && e->mimeData()->hasFormat(kAssetMimeType)) {
        const QString payload =
            QString::fromUtf8(e->mimeData()->data(kAssetMimeType));
        const int colon = payload.indexOf(':');
        if (colon > 0) {
            const QString type = payload.left(colon);
            assetlib::AssetId id;
            if (assetlib::AssetId::FromString(
                    payload.mid(colon + 1).toStdString(), id)) {
                if (type == QStringLiteral("model")) {
                    float placement[3] = {};
                    const QPoint dropPos = mapToGlobal(e->position().toPoint());
                    if (Scene::ResolveViewportImportPlacement(
                            static_cast<float>(dropPos.x()),
                            static_cast<float>(dropPos.y()),
                            static_cast<float>(DX12Context::g_windowWidth),
                            static_cast<float>(DX12Context::g_windowHeight),
                            placement))
                        Scene::InstantiateAssetModel(id, placement);
                    else
                        Scene::InstantiateAssetModel(id);
                } else if (type == QStringLiteral("material")) {
                    const QPoint dropPos =
                        mapToGlobal(e->position().toPoint());
                    Scene::AssignMaterialAssetAtViewportPosition(
                        id, static_cast<float>(dropPos.x()),
                        static_cast<float>(dropPos.y()),
                        static_cast<float>(DX12Context::g_windowWidth),
                        static_cast<float>(DX12Context::g_windowHeight));
                } else if (type == QStringLiteral("volume")) {
                    // Instantiate the volume as a selectable, transformable
                    // scene node (which also activates it for rendering).
                    Scene::AddVolumeNode(id);
                } else if (type == QStringLiteral("scatter_object")) {
                    // Drop a saved scatter onto a surface: build a scatter
                    // model from the asset's prototypes and target the surface
                    // under the cursor — instant scattering on that surface.
                    const QPoint dropPos = mapToGlobal(e->position().toPoint());
                    const size_t modelIndex = Scene::AddScatterModel();
                    if (Scene::AddScatterObjectAssetToModel(modelIndex, id)) {
                        Scene::AddScatterTargetFromPick(
                            modelIndex, static_cast<float>(dropPos.x()),
                            static_cast<float>(dropPos.y()),
                            static_cast<float>(DX12Context::g_windowWidth),
                            static_cast<float>(DX12Context::g_windowHeight));
                    } else {
                        Scene::RemoveScatterModel(modelIndex);
                    }
                }
                e->acceptProposedAction();
                return;
            }
        }
    }

    const QString path = FirstSupportedDroppedModelPath(e->mimeData());
    if (!path.isEmpty()) {
        float placement[3] = {};
        const QPoint dropPos = mapToGlobal(e->position().toPoint());
        if (Scene::ResolveViewportImportPlacement(
                static_cast<float>(dropPos.x()), static_cast<float>(dropPos.y()),
                static_cast<float>(DX12Context::g_windowWidth),
                static_cast<float>(DX12Context::g_windowHeight), placement)) {
            Scene::ImportModelAsync(QString(path).toUtf8().constData(),
                                    placement);
        } else {
            Scene::ImportModelAsync(QString(path).toUtf8().constData());
        }
        e->acceptProposedAction();
        return;
    }
    QWidget::dropEvent(e);
}

void DX12View::showPendingCloneOptions()
{
    if (m_cloneOptionsDialogOpen || !Scene::HasPendingCloneOptions()) {
        return;
    }

    m_cloneOptionsDialogOpen = true;
    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("Clone Options"));
    dialog.setText(tr("Clone selection as:"));
    QPushButton *copyButton =
        dialog.addButton(tr("Copy"), QMessageBox::AcceptRole);
    QPushButton *instanceButton =
        dialog.addButton(tr("Instance"), QMessageBox::AcceptRole);
    dialog.setDefaultButton(instanceButton);
    dialog.setEscapeButton(instanceButton);
    dialog.exec();

    if (dialog.clickedButton() == copyButton) {
        Scene::ResolvePendingCloneAsCopy();
    } else {
        Scene::ResolvePendingCloneAsInstance();
    }
    m_cloneOptionsDialogOpen = false;
}

void DX12View::cancelBoxSelection()
{
    m_boxSelecting = false;
    if (m_boxSelectionBand) {
        m_boxSelectionBand->hide();
    }
}

void DX12View::updateBoxSelectionBand(const QPointF &currentGlobalPos)
{
    if (!m_boxSelectionBand) {
        return;
    }

    QRect globalRect(m_boxStartGlobalPos.toPoint(),
                     currentGlobalPos.toPoint());
    globalRect = globalRect.normalized();
    if (globalRect.width() < 1) {
        globalRect.setWidth(1);
    }
    if (globalRect.height() < 1) {
        globalRect.setHeight(1);
    }
    m_boxSelectionBand->setGeometry(globalRect);
    m_boxSelectionBand->show();
    m_boxSelectionBand->raise();
    m_boxSelectionBand->update();
}
