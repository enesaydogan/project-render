#include "file_import.h"
#include <commdlg.h>
#include <filesystem>

namespace {

enum class DialogLocation {
  Model,
  Hdr,
  Texture,
  OpenScene,
  SaveScene,
  RenderImage,
  Count
};

std::wstring &LastDirectory(DialogLocation location) {
  static std::wstring directories[static_cast<size_t>(DialogLocation::Count)];
  return directories[static_cast<size_t>(location)];
}

void ApplyInitialDirectory(OPENFILENAMEW &ofn, DialogLocation location) {
  const std::wstring &directory = LastDirectory(location);
  if (!directory.empty()) {
    ofn.lpstrInitialDir = directory.c_str();
  }
}

void RememberSelectedDirectory(DialogLocation location, const std::wstring &path) {
  std::filesystem::path selected(path);
  std::filesystem::path parent = selected.parent_path();
  if (!parent.empty()) {
    LastDirectory(location) = parent.wstring();
  }
}

} // namespace

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
      L"Model files\0*.skp;*.gltf;*.glb;*.obj;*.stl;*.fbx;*.ltm;*.lmod\0All files\0*.*\0";
  ApplyInitialDirectory(ofn, DialogLocation::Model);
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    RememberSelectedDirectory(DialogLocation::Model, outPath);
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
  ApplyInitialDirectory(ofn, DialogLocation::Hdr);
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    RememberSelectedDirectory(DialogLocation::Hdr, outPath);
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
  ApplyInitialDirectory(ofn, DialogLocation::Texture);
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    RememberSelectedDirectory(DialogLocation::Texture, outPath);
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
  ofn.lpstrFilter = L"Project Render Scene (*.prs)\0*.prs\0Legacy Scene (*.json)\0*.json\0All files\0*.*\0";
  ApplyInitialDirectory(ofn, DialogLocation::OpenScene);
  if (GetOpenFileNameW(&ofn)) {
    outPath = szFile;
    RememberSelectedDirectory(DialogLocation::OpenScene, outPath);
    return true;
  }
  return false;
}

bool SaveSceneFileDialog(HWND owner, std::wstring &outPath) {
  OPENFILENAMEW ofn = {};
  WCHAR szFile[1024] = {L"scene.prs"};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = (DWORD)std::size(szFile);
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
              OFN_OVERWRITEPROMPT;
  ofn.lpstrFilter = L"Project Render Scene (*.prs)\0*.prs\0All files\0*.*\0";
  ofn.lpstrDefExt = L"prs";
  ApplyInitialDirectory(ofn, DialogLocation::SaveScene);
  if (GetSaveFileNameW(&ofn)) {
    outPath = szFile;
    RememberSelectedDirectory(DialogLocation::SaveScene, outPath);
    return true;
  }
  return false;
}

bool SaveRenderImageFileDialog(HWND owner, std::wstring &outPath) {
  OPENFILENAMEW ofn = {};
  WCHAR szFile[1024] = {L"render.png"};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = (DWORD)std::size(szFile);
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
              OFN_OVERWRITEPROMPT;
  ofn.lpstrFilter = L"PNG image\0*.png\0All files\0*.*\0";
  ofn.lpstrDefExt = L"png";
  ApplyInitialDirectory(ofn, DialogLocation::RenderImage);
  if (GetSaveFileNameW(&ofn)) {
    outPath = szFile;
    RememberSelectedDirectory(DialogLocation::RenderImage, outPath);
    return true;
  }
  return false;
}
