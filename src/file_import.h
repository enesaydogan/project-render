#pragma once
#include <string>
#include <windows.h>


// Opens a Win32 file-open dialog for glTF files. Returns true if a file was
// chosen.
bool OpenModelFileDialog(HWND owner, std::wstring &outPath);
// Opens a Win32 file-open dialog for HDR/EXR files. Returns true if a file was
// chosen.
bool OpenHDRFileDialog(HWND owner, std::wstring &outPath);
// Opens a Win32 file-open dialog for common texture/image files. Returns true
// if a file was chosen.
bool OpenTextureFileDialog(HWND owner, std::wstring &outPath);
// Opens a Win32 file-open dialog for scene files. Returns true if a file was
// chosen.
bool OpenSceneFileDialog(HWND owner, std::wstring &outPath);
// Opens a Win32 file-save dialog for scene files. Returns true if a file was
// chosen.
bool SaveSceneFileDialog(HWND owner, std::wstring &outPath);
// Opens a Win32 file-save dialog for rendered image export. Defaults to PNG.
bool SaveRenderImageFileDialog(HWND owner, std::wstring &outPath);
