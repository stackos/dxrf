#include "Renderer.h"

Renderer::Renderer(HWND hwnd, int width, int height)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
}

void Renderer::Init()
{
    m_device = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        3,
        D3D_FEATURE_LEVEL_11_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the sample requires build 1809 (RS5) or higher, we don't need to handle non-tearing cases.
        DeviceResources::c_RequireTearingSupport,
        UINT_MAX);
    m_device->RegisterDeviceNotify(this);
    m_device->SetWindow(m_hwnd, m_width, m_height);
    m_device->InitializeDXGIAdapter();

    ThrowIfFalse(IsDirectXRaytracingSupported(m_device->GetAdapter()),
        "ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

    m_device->CreateDeviceResources();
    m_device->CreateWindowSizeDependentResources();

    this->CreateDeviceDependentResources();
    this->CreateWindowSizeDependentResources();
}

void Renderer::Done()
{
    m_device.reset();
}

void Renderer::OnSizeChanged(int width, int height, bool minimized)
{
    m_width = width;
    m_height = height;

    if (!m_device->WindowSizeChanged(width, height, minimized))
    {
        return;
    }

    this->ReleaseWindowSizeDependentResources();
    this->CreateWindowSizeDependentResources();
}

void Renderer::Render()
{
    if (!m_device->IsWindowVisible())
    {
        return;
    }

    m_device->Prepare();
    this->DoRaytracing();
    this->CopyRaytracingOutputToBackbuffer();

    m_device->Present(D3D12_RESOURCE_STATE_PRESENT);
}

void Renderer::OnDeviceLost()
{
    this->ReleaseWindowSizeDependentResources();
    this->ReleaseDeviceDependentResources();
}

void Renderer::OnDeviceRestored()
{
    this->CreateDeviceDependentResources();
    this->CreateWindowSizeDependentResources();
}

void Renderer::CreateDeviceDependentResources()
{
    this->CreateDescriptorHeap();
}

void Renderer::CreateWindowSizeDependentResources()
{
    this->CreateRaytracingOutputResource();
}

void Renderer::ReleaseDeviceDependentResources()
{
    m_descriptor_heap.Reset();
    m_descriptors_allocated = 0;
    m_descriptor_size = UINT_MAX;
}

void Renderer::ReleaseWindowSizeDependentResources()
{
    m_raytracing_output.Reset();
}

void Renderer::CreateDescriptorHeap()
{
    auto device = m_device->GetD3DDevice();

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    // Allocate a heap for 3 descriptors:
    // 2 - vertex and index buffer SRVs
    // 1 - raytracing output texture SRV
    desc.NumDescriptors = 3;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptor_heap));
    NAME_D3D12_OBJECT(m_descriptor_heap);

    m_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void Renderer::CreateRaytracingOutputResource()
{
    auto device = m_device->GetD3DDevice();
    auto format = m_device->GetBackBufferFormat();

    // Create the output resource. The dimensions and format should match the swap-chain.
    auto tex_desc = CD3DX12_RESOURCE_DESC::Tex2D(format, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &tex_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracing_output)));
    NAME_D3D12_OBJECT(m_raytracing_output);

    D3D12_CPU_DESCRIPTOR_HANDLE desc_handle;
    m_raytracing_output_descriptor_index = this->AllocateDescriptor(&desc_handle, m_raytracing_output_descriptor_index);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_raytracing_output.Get(), nullptr, &uav_desc, desc_handle);
    m_raytracing_output_descriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), m_raytracing_output_descriptor_index, m_descriptor_size);
}

UINT Renderer::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* descriptor, UINT index)
{
    auto base = m_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    if (index >= m_descriptor_heap->GetDesc().NumDescriptors)
    {
        index = m_descriptors_allocated++;
    }
    *descriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(base, index, m_descriptor_size);
    return index;
}

void Renderer::DoRaytracing()
{

}

void Renderer::CopyRaytracingOutputToBackbuffer()
{
    auto cmd = m_device->GetCommandList();
    auto rt = m_device->GetRenderTarget();

    D3D12_RESOURCE_BARRIER pre_copy_barriers[2];
    pre_copy_barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    pre_copy_barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracing_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->ResourceBarrier(ARRAYSIZE(pre_copy_barriers), pre_copy_barriers);

    cmd->CopyResource(rt, m_raytracing_output.Get());

    D3D12_RESOURCE_BARRIER post_copy_barriers[2];
    post_copy_barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(rt, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    post_copy_barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracing_output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->ResourceBarrier(ARRAYSIZE(post_copy_barriers), post_copy_barriers);
}
