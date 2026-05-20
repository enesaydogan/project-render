#pragma once
#include <windows.h>

namespace Input {
extern bool g_imguiEnabled;
void Update(float dt);
void SetQtWidgetFocused(bool focused);
void SetQtKeyState(int virtualKey, bool down);
void SetQtMouseButtonState(int virtualKey, bool down);
void AddQtMouseDelta(float dx, float dy);
void ResetQtInputState();
void ResetTransientInputState();
}
