#pragma once
#include <windows.h>
#include <string>

// Opens a Win32 file-open dialog for glTF files. Returns true if a file was chosen.
bool OpenGltfFileDialog(HWND owner, std::wstring &outPath);
// Opens a Win32 file-open dialog for HDR/EXR files. Returns true if a file was chosen.
bool OpenHDRFileDialog(HWND owner, std::wstring &outPath);
