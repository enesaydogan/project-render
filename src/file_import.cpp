#include "file_import.h"
#include <commdlg.h>

bool OpenModelFileDialog(HWND owner, std::wstring &outPath) {
    OPENFILENAMEW ofn = {};
    WCHAR szFile[1024] = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = (DWORD)std::size(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrFilter = L"Model files\0*.gltf;*.glb;*.obj;*.stl;*.fbx\0All files\0*.*\0";
    if (GetOpenFileNameW(&ofn)) {
        outPath = szFile;
        return true;
    }
    return false;
}

bool OpenHDRFileDialog(HWND owner, std::wstring &outPath) {
    OPENFILENAMEW ofn = {};
    WCHAR szFile[1024] = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = (DWORD)std::size(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrFilter = L"HDR/EXR files\0*.hdr;*.exr\0All files\0*.*\0";
    if (GetOpenFileNameW(&ofn)) {
        outPath = szFile;
        return true;
    }
    return false;
}
