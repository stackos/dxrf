#pragma once

#include "DeviceResources.h"

using namespace DX;

class Renderer : public DX::IDeviceNotify
{
public:
    Renderer(HWND hwnd, int width, int height);
    void Init();
    void Done();
    void OnSizeChanged(int width, int height, bool minimized);
    void Render();
    
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

private:
    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
    void CreateDescriptorHeap();
    void CreateRaytracingOutputResource();
    UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* descriptor, UINT index);
    void DoRaytracing();
    void CopyRaytracingOutputToBackbuffer();

private:
    HWND m_hwnd = NULL;
    int m_width = 0;
    int m_height = 0;
    std::unique_ptr<DeviceResources> m_device;

    // Descriptors
    ComPtr<ID3D12DescriptorHeap> m_descriptor_heap;
    UINT m_descriptors_allocated = 0;
    UINT m_descriptor_size = UINT_MAX;

    // Raytracing output
    ComPtr<ID3D12Resource> m_raytracing_output;
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracing_output_descriptor;
    UINT m_raytracing_output_descriptor_index;
};
