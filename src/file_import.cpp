#include "file_import.h"
#include <commdlg.h>

bool OpenModelFileDialog(HWND owner, std::wstring &outPath) {
  OPENFILENAMEW ofn = {};
  WCHAR szFile[1024] = {};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = (DWORD)std::size(szFile);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_NOCHANGEDIR;
  ofn.lpstrFilter =
      L"Model files\0*.skp;*.gltf;*.glb;*.obj;*.stl;*.fbx\0All files\0*.*\0";
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
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_NOCHANGEDIR;
  ofn.lpstrFilter = L"HDR/EXR files\0*.hdr;*.exr\0All files\0*.*\0";
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    return true;
  }
  return false;
}

bool OpenTextureFileDialog(HWND owner, std::wstring &outPath) {
  OPENFILENAMEW ofn = {};
  WCHAR szFile[1024] = {};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = (DWORD)std::size(szFile);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_NOCHANGEDIR;
  ofn.lpstrFilter =
      L"Image files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.exr\0All "
      L"files\0*.*\0";
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    return true;
  }
  return false;
}
bool OpenSceneFileDialog(HWND owner, std::wstring &outPath) {
  OPENFILENAMEW ofn = {};
  WCHAR szFile[1024] = {};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = (DWORD)std::size(szFile);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
              OFN_NOCHANGEDIR;
  ofn.lpstrFilter = L"Scene files\0*.json;*.render\0All files\0*.*\0";
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    return true;
  }
  return false;
}

bool SaveSceneFileDialog(HWND owner, std::wstring &outPath) {
  OPENFILENAMEW ofn = {};
  WCHAR szFile[1024] = {L"scene.json"};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = (DWORD)std::size(szFile);
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
              OFN_OVERWRITEPROMPT;
  ofn.lpstrFilter = L"Scene files\0*.json;*.render\0All files\0*.*\0";
  ofn.lpstrDefExt = L"json";
  if (GetSaveFileNameW(&ofn)) {
    outPath = szFile;
    return true;
  }
  return false;
}
