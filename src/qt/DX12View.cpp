#include "DX12View.h"
#include "../dx12_context.h" // adapt include path
#include "../editor_ui.h"
#include "../input_handler.h"
#include "../imgui.h"
#include "../scene.h"
#include <cfloat>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

namespace {

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
}

DX12View::~DX12View()
{
}

void DX12View::focusInEvent(QFocusEvent *e)
{
    Input::SetQtWidgetFocused(true);
    ImGui::GetIO().AddFocusEvent(true);
    QWidget::focusInEvent(e);
}

void DX12View::focusOutEvent(QFocusEvent *e)
{
    Input::SetQtWidgetFocused(false);
    Input::SetQtMouseButtonState(VK_LBUTTON, false);
    Input::SetQtMouseButtonState(VK_RBUTTON, false);
    Input::SetQtKeyState('G', false);
    Input::SetQtKeyState('R', false);
    Input::SetQtKeyState('T', false);
    Input::SetQtKeyState('L', false);
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
    if (e->key() == Qt::Key_Escape && !e->isAutoRepeat()) {
        if (IsPreviewRenderActive()) {
            CancelPreviewRender();
        } else {
            Scene::SelectNode(static_cast<size_t>(-1));
            Scene::SelectLight(-1);
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
    QWidget::keyReleaseEvent(e);
}

void DX12View::mousePressEvent(QMouseEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(e->globalPosition().x()),
                                    static_cast<float>(e->globalPosition().y()));
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
    DX12Context::QueueResize(static_cast<UINT>(width()),
                             static_cast<UINT>(height()));
}
