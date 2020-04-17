#pragma once

#include "DeviceResources.h"
#include "RaytracingHlslCompat.h"
#include "Texture.h"
#include "Scene.h"

using namespace DX;
using namespace dxrf;

class Renderer : public DX::IDeviceNotify
{
public:
    Renderer(HWND hwnd, int width, int height);
    void Init();
    void Done();
    void OnSizeChanged(int width, int height, bool minimized);
    void Update();
    void Render();
    
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

private:
    void InitializeScene();
    void UpdateCameraMatrices();
    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
    void CreateRaytracingOutputResource();
    void DoRaytracing();
    void CopyRaytracingOutputToBackbuffer();
    void CreateRootSignatures();
    void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* sig);
    void CreateRaytracingInterfaces();
    void CreateRaytracingPipelineStateObject();
    void LoadScene();
    void BuildGeometry();
    UINT CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize);
    void BuildAccelerationStructures();
    void CreateConstantBuffers();
    void BuildShaderTables();

private:
    static const int BACK_BUFFER_COUNT = 3;

    char m_work_dir[MAX_PATH];
    HWND m_hwnd = NULL;
    int m_width = 0;
    int m_height = 0;
    float m_aspect = 1.0f;
    std::unique_ptr<DeviceResources> m_device;

    // Raytracing output
    ComPtr<ID3D12Resource> m_raytracing_output;
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracing_output_descriptor = { };
    UINT m_raytracing_output_descriptor_index = UINT_MAX;

    // Root signatures
    ComPtr<ID3D12RootSignature> m_raytracing_global_sig;
    ComPtr<ID3D12RootSignature> m_raytracing_local_sig;

    // DirectX Raytracing (DXR) attributes
    ComPtr<ID3D12Device5> m_dxr_device;
    ComPtr<ID3D12GraphicsCommandList5> m_dxr_cmd;
    ComPtr<ID3D12StateObject> m_dxr_state;

    // Raytracing scene
    SceneConstantBuffer m_scene_cb[BACK_BUFFER_COUNT] = { };
    XMVECTOR m_eye = { };
    XMVECTOR m_at = { };
    XMVECTOR m_up = { };

    // Geometry
    D3DBuffer m_index_buffer;
    D3DBuffer m_vertex_buffer;

    // Acceleration structure
    ComPtr<ID3D12Resource> m_bottom_structure;
    ComPtr<ID3D12Resource> m_top_structure;

    // ConstantBuffer
    static_assert(sizeof(SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here.");
    union AlignedSceneConstantBuffer
    {
        SceneConstantBuffer constants;
        uint8_t alignment_padding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
    };
    AlignedSceneConstantBuffer* m_mapped_cb = nullptr;
    ComPtr<ID3D12Resource> m_frame_cb;

    // Shader tables
    ComPtr<ID3D12Resource> m_raygen_table;
    ComPtr<ID3D12Resource> m_miss_table;
    ComPtr<ID3D12Resource> m_hit_group_table;
    uint64_t m_hit_group_stride = 0;
   
    std::unique_ptr<Texture> m_texture_bg;
    std::unique_ptr<Texture> m_texture_mesh;

    std::unique_ptr<Scene> m_scene;
};
