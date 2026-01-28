#include "ibl_manager.h"
#include <iostream>

bool IBLManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_device = device;
    m_queue = queue;
    return true;
}

bool IBLManager::LoadEnvironmentMap(const std::string& path) {
    if (!m_device) return false;

    bool isHDR = false;
    if (path.find(".hdr") != std::string::npos || path.find(".exr") != std::string::npos) {
        isHDR = true;
    }

    m_envMap = Asset::LoadTextureFromFile(path, isHDR);
    if (!m_envMap.resource) {
        std::cerr << "Failed to load environment map: " << path << std::endl;
        return false;
    }

    std::cout << "Loaded environment map: " << path << " (" << m_envMap.width << "x" << m_envMap.height << ")" << std::endl;
    return true;
}
