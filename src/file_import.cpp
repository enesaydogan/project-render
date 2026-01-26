#include "file_import.h"
#include <commdlg.h>

bool OpenGltfFileDialog(HWND owner, std::wstring &outPath) {
    OPENFILENAMEW ofn = {};
    WCHAR szFile[1024] = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = (DWORD)std::size(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrFilter = L"glTF files\0*.gltf;*.glb\0All files\0*.*\0";
    if (GetOpenFileNameW(&ofn)) {
        outPath = szFile;
        return true;
    }
    return false;
}
