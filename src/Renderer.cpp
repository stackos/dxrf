#include "Renderer.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders/Raytracing.hlsl.h"

#define STB_IMAGE_IMPLEMENTATION
#include "3rd/stb/stb_image.h"

namespace GlobalRootSignatureParams
{
    enum Value
    {
        OutputViewSlot = 0,
        AccelerationStructureSlot,
        SceneConstantSlot,
        VertexBuffersSlot,
        TextureSlot,
        Count
    };
}

namespace LocalRootSignatureParams
{
    enum Value
    {
        TextureSlot = 0,
        CubeConstantSlot,
        Count
    };
}

static auto raygen_name = L"MyRaygenShader";
static auto closest_hit_name = L"MyClosestHitShader";
static auto miss_name = L"MyMissShader";
static auto hit_group_name = L"MyHitGroup";

Renderer::Renderer(HWND hwnd, int width, int height)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_aspect = width / (float) height;

    GetModuleFileName(NULL, m_work_dir, MAX_PATH);
    size_t len = strrchr(m_work_dir, '\\') - m_work_dir;
    m_work_dir[len] = 0;
}

void Renderer::Init()
{
    m_device = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        BACK_BUFFER_COUNT,
        D3D_FEATURE_LEVEL_11_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the sample requires build 1809 (RS5) or higher, we don't need to handle non-tearing cases.
        0,// DeviceResources::c_RequireTearingSupport,
        UINT_MAX);
    m_device->RegisterDeviceNotify(this);
    m_device->SetWindow(m_hwnd, m_width, m_height);
    m_device->InitializeDXGIAdapter();

    ThrowIfFalse(IsDirectXRaytracingSupported(m_device->GetAdapter()),
        "ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

    m_device->CreateDeviceResources();
    m_device->CreateWindowSizeDependentResources();

    this->InitializeScene();
    this->CreateDeviceDependentResources();
    this->CreateWindowSizeDependentResources();
}

void Renderer::Done()
{
    m_device->WaitForGpu();
    this->OnDeviceLost();
    m_device.reset();
}

void Renderer::OnSizeChanged(int width, int height, bool minimized)
{
    m_width = width;
    m_height = height;
    m_aspect = width / (float) height;

    if (!m_device->WindowSizeChanged(width, height, minimized))
    {
        return;
    }

    this->ReleaseWindowSizeDependentResources();
    this->CreateWindowSizeDependentResources();
}

void Renderer::Update()
{
    m_eye = { 4.0f, 2.0f, -4.0f, 1.0f };
    m_at = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_up = { 0.0f, 1.0f, 0.0f, 1.0f };

    static float deg = 0.0f;
    deg += 0.01f;
    XMMATRIX rot = XMMatrixRotationAxis({ 0.0f, 1.0f, 0.0 }, deg);
    m_eye = XMVector4Transform(m_eye, rot);

    this->UpdateCameraMatrices();
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

void Renderer::InitializeScene()
{
    auto frame_index = m_device->GetCurrentFrameIndex();

    m_cube_cb.albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

    m_eye = { 4.0f, 2.0f, -4.0f, 1.0f };
    m_at = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_up = { 0.0f, 1.0f, 0.0f, 1.0f };

    this->UpdateCameraMatrices();

    for (auto& cb : m_scene_cb)
    {
        cb = m_scene_cb[frame_index];
    }
}

void Renderer::UpdateCameraMatrices()
{
    auto frame_index = m_device->GetCurrentFrameIndex();

    float fov = 45.0f;
    XMMATRIX view = XMMatrixLookAtLH(m_eye, m_at, m_up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fov), m_aspect, 1.0f, 125.0f);
    XMMATRIX view_proj = view * proj;

    m_scene_cb[frame_index].camera_position = m_eye;
    m_scene_cb[frame_index].projection_to_world = XMMatrixInverse(nullptr, view_proj);
}

void Renderer::CreateDeviceDependentResources()
{
    this->CreateDescriptorHeap();
    this->CreateRootSignatures();
    this->CreateRaytracingInterfaces();
    this->CreateRaytracingPipelineStateObject();
    this->BuildGeometry();

    {
        int w, h, c;

        char path[MAX_PATH];
        sprintf(path, "%s/assets/720x1280.png", m_work_dir);
        void* data = stbi_load(path, &w, &h, &c, 4);
        if (data)
        {
            this->CreateTexture(&m_texture_mesh, w, h, false, (const void**) &data);
            stbi_image_free(data);
        }
    }

    {
        int w, h, c;

        std::vector<void*> datas(6);
        for (int i = 0; i < 6; ++i)
        {
            char path[MAX_PATH];
            sprintf(path, "%s/assets/sky/0_%d.png", m_work_dir, i);
            datas[i] = stbi_load(path, &w, &h, &c, 4);
        }

        this->CreateTexture(&m_texture_bg, w, h, true, (const void**) &datas[0]);

        for (int i = 0; i < 6; ++i)
        {
            stbi_image_free(datas[i]);
        }
    }

    this->BuildAccelerationStructures();
    this->CreateConstantBuffers();
    this->BuildShaderTables();
}

void Renderer::CreateWindowSizeDependentResources()
{
    this->CreateRaytracingOutputResource();
    this->UpdateCameraMatrices();
}

void Renderer::ReleaseDeviceDependentResources()
{
    m_texture_bg.texture.Reset();
    m_texture_mesh.texture.Reset();

    m_miss_table.Reset();
    m_hit_group_table.Reset();
    m_raygen_table.Reset();

    m_frame_cb.Reset();

    m_bottom_structure.Reset();
    m_top_structure.Reset();

    m_index_buffer.resource.Reset();
    m_vertex_buffer.resource.Reset();

    m_dxr_device.Reset();
    m_dxr_cmd.Reset();
    m_dxr_state.Reset();

    m_raytracing_global_sig.Reset();
    m_raytracing_local_sig.Reset();

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
    // Allocate a heap for 5 descriptors:
    // 2 - vertex and index buffer SRVs
    // 1 - raytracing output texture SRV
    // 1 - raytracing mesh texture SRV
    // 1 - raytracing bg texture SRV
    desc.NumDescriptors = 5;
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
    auto cmd = m_device->GetCommandList();
    auto frame_index = m_device->GetCurrentFrameIndex();

    auto DispatchRays = [&](auto* dxr_cmd, auto* state, auto* desc)
    {
        // Since each shader table has only one shader record, the stride is same as the size.
        desc->HitGroupTable.StartAddress = m_hit_group_table->GetGPUVirtualAddress();
        desc->HitGroupTable.SizeInBytes = m_hit_group_table->GetDesc().Width;
        desc->HitGroupTable.StrideInBytes = desc->HitGroupTable.SizeInBytes;
        desc->MissShaderTable.StartAddress = m_miss_table->GetGPUVirtualAddress();
        desc->MissShaderTable.SizeInBytes = m_miss_table->GetDesc().Width;
        desc->MissShaderTable.StrideInBytes = desc->MissShaderTable.SizeInBytes;
        desc->RayGenerationShaderRecord.StartAddress = m_raygen_table->GetGPUVirtualAddress();
        desc->RayGenerationShaderRecord.SizeInBytes = m_raygen_table->GetDesc().Width;
        desc->Width = m_width;
        desc->Height = m_height;
        desc->Depth = 1;
        dxr_cmd->SetPipelineState1(state);
        dxr_cmd->DispatchRays(desc);
    };

    auto SetCommonPipelineState = [&](auto* desc_cmd)
    {
        desc_cmd->SetDescriptorHeaps(1, m_descriptor_heap.GetAddressOf());
        // Set index and successive vertex buffer decriptor tables
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, m_index_buffer.gpu_desc);
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, m_raytracing_output_descriptor);
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::TextureSlot, m_texture_bg.srv);
    };

    cmd->SetComputeRootSignature(m_raytracing_global_sig.Get());

    // Copy the updated scene constant buffer to GPU.
    memcpy(&m_mapped_cb[frame_index].constants, &m_scene_cb[frame_index], sizeof(m_scene_cb[frame_index]));
    auto cb_gpu_address = m_frame_cb->GetGPUVirtualAddress() + frame_index * sizeof(m_mapped_cb[0]);
    cmd->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cb_gpu_address);

    // Bind the heaps, acceleration structure and dispatch rays.
    D3D12_DISPATCH_RAYS_DESC dispatch_sesc = { };
    SetCommonPipelineState(cmd);
    cmd->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, m_top_structure->GetGPUVirtualAddress());
    DispatchRays(m_dxr_cmd.Get(), m_dxr_state.Get(), &dispatch_sesc);
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

void Renderer::CreateRootSignatures()
{
    auto device = m_device->GetD3DDevice();

    {
        D3D12_STATIC_SAMPLER_DESC sampler = { };
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0.0f;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy = 0;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sampler.ShaderRegister = 0;

        CD3DX12_DESCRIPTOR_RANGE ranges[3]; // Perfomance TIP: Order from most frequent to least frequent.
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // 1 output texture
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);  // 2 static index and vertex buffers.
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // 3 material texture

        CD3DX12_ROOT_PARAMETER parameters[GlobalRootSignatureParams::Count];
        parameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
        parameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
        parameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
        parameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]);
        parameters[GlobalRootSignatureParams::TextureSlot].InitAsDescriptorTable(1, &ranges[2]);
        CD3DX12_ROOT_SIGNATURE_DESC desc(ARRAYSIZE(parameters), parameters, 1, &sampler);
        this->SerializeAndCreateRaytracingRootSignature(desc, &m_raytracing_global_sig);
    }

    {
        CD3DX12_DESCRIPTOR_RANGE ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); // material texture

        CD3DX12_ROOT_PARAMETER parameters[LocalRootSignatureParams::Count];
        parameters[LocalRootSignatureParams::TextureSlot].InitAsDescriptorTable(1, &ranges[0]);
        parameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cube_cb), 1);
        CD3DX12_ROOT_SIGNATURE_DESC desc(ARRAYSIZE(parameters), parameters);
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        this->SerializeAndCreateRaytracingRootSignature(desc, &m_raytracing_local_sig);
    }
}

void Renderer::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* sig)
{
    auto device = m_device->GetD3DDevice();

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), "D3D12SerializeRootSignature failed");
    ThrowIfFailed(device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*sig))));
}

void Renderer::CreateRaytracingInterfaces()
{
    auto device = m_device->GetD3DDevice();
    auto cmd = m_device->GetCommandList();

    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_dxr_device)), "Couldn't get DirectX Raytracing interface for the device.\n");
    ThrowIfFailed(cmd->QueryInterface(IID_PPV_ARGS(&m_dxr_cmd)), "Couldn't get DirectX Raytracing interface for the command list.\n");
}

void Renderer::CreateRaytracingPipelineStateObject()
{
    CD3DX12_STATE_OBJECT_DESC pipeline(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    auto lib = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*) g_pRaytracing, ARRAYSIZE(g_pRaytracing));
    lib->SetDXILLibrary(&libdxil);
    {
        lib->DefineExport(raygen_name);
        lib->DefineExport(closest_hit_name);
        lib->DefineExport(miss_name);
    }

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(closest_hit_name);
    hit_group->SetHitGroupExport(hit_group_name);
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shader_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size = sizeof(XMFLOAT4);    // float4 pixelColor
    UINT attribute_size = sizeof(XMFLOAT2);  // float2 barycentrics
    shader_config->Config(payload_size, attribute_size);

    auto local_root_sig = pipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    local_root_sig->SetRootSignature(m_raytracing_local_sig.Get());
    {
        auto sig_association = pipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        sig_association->SetSubobjectToAssociate(*local_root_sig);
        sig_association->AddExport(hit_group_name);
    }

    auto global_sig = pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_sig->SetRootSignature(m_raytracing_global_sig.Get());

    auto pipeline_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    UINT max_recursion_depth = 1; // ~ primary rays only. 
    pipeline_config->Config(max_recursion_depth);

#if _DEBUG
    PrintStateObjectDesc(pipeline);
#endif

    ThrowIfFailed(m_dxr_device->CreateStateObject(pipeline, IID_PPV_ARGS(&m_dxr_state)), "Couldn't create DirectX Raytracing state object.\n");
}

void Renderer::BuildGeometry()
{
    auto device = m_device->GetD3DDevice();

    // Cube vertices positions and corresponding triangle normals.
    Vertex vertices[] =
    {
        { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

        { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

        { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },

        { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) },
    };

    // Cube indices.
    Index indices[] =
    {
        3, 1, 0,
        2, 1, 3,

        6, 4, 5,
        7, 4, 6,

        11, 9, 8,
        10, 9, 11,

        14, 12, 13,
        15, 12, 14,

        19, 17, 16,
        18, 17, 19,

        22, 20, 21,
        23, 20, 22
    };

    AllocateUploadBuffer(device, vertices, sizeof(vertices), &m_vertex_buffer.resource);
    AllocateUploadBuffer(device, indices, sizeof(indices), &m_index_buffer.resource);

    // Vertex buffer is passed to the shader along with index buffer as a descriptor table.
    // Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
    UINT ib_index = this->CreateBufferSRV(&m_index_buffer, sizeof(indices) / 4, 0);
    UINT vb_index = this->CreateBufferSRV(&m_vertex_buffer, ARRAYSIZE(vertices), sizeof(vertices[0]));
    ThrowIfFalse(vb_index == ib_index + 1, "Vertex Buffer descriptor index must follow that of Index Buffer descriptor index!");
}

UINT Renderer::CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize)
{
    auto device = m_device->GetD3DDevice();

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.NumElements = numElements;
    if (elementSize == 0)
    {
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        desc.Buffer.StructureByteStride = 0;
    }
    else
    {
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        desc.Buffer.StructureByteStride = elementSize;
    }
    UINT desc_index = this->AllocateDescriptor(&buffer->cpu_desc);
    device->CreateShaderResourceView(buffer->resource.Get(), &desc, buffer->cpu_desc);
    buffer->gpu_desc = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), desc_index, m_descriptor_size);
    return desc_index;
}

void Renderer::CreateTexture(D3DTexture* texture, int width, int height, bool cube, const void** faces_data)
{
    auto device = m_device->GetD3DDevice();
    auto cmd = m_device->GetCommandList();

    cmd->Reset(m_device->GetCommandAllocator(), nullptr);

    D3D12_SRV_DIMENSION view_dimension = cube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
    int array_size = cube ? 6 : 1;
    int mip_levels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    int pixel_size = 4;

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC desc = { };
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = array_size;
    desc.Format = format;
    desc.MipLevels = mip_levels;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture->texture)));

    // Create the GPU upload buffer.
    const UINT64 upload_size = GetRequiredIntermediateSize(texture->texture.Get(), 0, array_size);

    ComPtr<ID3D12Resource> upload_heap;
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(upload_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload_heap)));

    std::vector<D3D12_SUBRESOURCE_DATA> datas(array_size);
    for (int i = 0; i < array_size; ++i)
    {
        datas[i].pData = faces_data[i];
        datas[i].RowPitch = width * pixel_size;
        datas[i].SlicePitch = datas[i].RowPitch * height;
    }

    UpdateSubresources(cmd, texture->texture.Get(), upload_heap.Get(), 0, 0, array_size, &datas[0]);
    cmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture->texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    D3D12_CPU_DESCRIPTOR_HANDLE desc_handle;
    texture->srv_index = this->AllocateDescriptor(&desc_handle);

    // Describe and create a SRV for the texture.
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = format;
    srv_desc.ViewDimension = view_dimension;
    if (cube)
    {
        srv_desc.TextureCube.MipLevels = mip_levels;
    }
    else
    {
        srv_desc.Texture2D.MipLevels = mip_levels;
    }
    device->CreateShaderResourceView(texture->texture.Get(), &srv_desc, desc_handle);
    texture->srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptor_heap->GetGPUDescriptorHandleForHeapStart(), texture->srv_index, m_descriptor_size);

    m_device->ExecuteCommandList();
    m_device->WaitForGpu();
}

void Renderer::BuildAccelerationStructures()
{
    auto device = m_device->GetD3DDevice();
    auto cmd = m_device->GetCommandList();

    // Reset the command list for the acceleration structure construction.
    cmd->Reset(m_device->GetCommandAllocator(), nullptr);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = { };
    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Triangles.IndexBuffer = m_index_buffer.resource->GetGPUVirtualAddress();
    geometry_desc.Triangles.IndexCount = static_cast<UINT>(m_index_buffer.resource->GetDesc().Width) / sizeof(Index);
    geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometry_desc.Triangles.Transform3x4 = 0;
    geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.VertexCount = static_cast<UINT>(m_vertex_buffer.resource->GetDesc().Width) / sizeof(Vertex);
    geometry_desc.Triangles.VertexBuffer.StartAddress = m_vertex_buffer.resource->GetGPUVirtualAddress();
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

    // Mark the geometry as opaque. 
    // PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
    // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Get required sizes for an acceleration structure.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS build_flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_level_desc = { };
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottom_inputs = bottom_level_desc.Inputs;
    bottom_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom_inputs.Flags = build_flags;
    bottom_inputs.NumDescs = 1;
    bottom_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom_inputs.pGeometryDescs = &geometry_desc;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_desc = { };
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& top_inputs = top_level_desc.Inputs;
    top_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top_inputs.Flags = build_flags;
    top_inputs.NumDescs = 1;
    top_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top_inputs.pGeometryDescs = nullptr;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_info = { };
    m_dxr_device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_inputs, &bottom_info);
    ThrowIfFalse(bottom_info.ResultDataMaxSizeInBytes > 0);

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info = { };
    m_dxr_device->GetRaytracingAccelerationStructurePrebuildInfo(&top_inputs, &top_info);
    ThrowIfFalse(top_info.ResultDataMaxSizeInBytes > 0);

    ComPtr<ID3D12Resource> scratch_resource;
    AllocateUAVBuffer(device, max(top_info.ScratchDataSizeInBytes, bottom_info.ScratchDataSizeInBytes), &scratch_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
    // Default heap is OK since the application doesn’t need CPU read/write access to them. 
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        AllocateUAVBuffer(device, bottom_info.ResultDataMaxSizeInBytes, &m_bottom_structure, init_state, L"BottomLevelAccelerationStructure");
        AllocateUAVBuffer(device, top_info.ResultDataMaxSizeInBytes, &m_top_structure, init_state, L"TopLevelAccelerationStructure");
    }

    // Create an instance desc for the bottom-level acceleration structure.
    ComPtr<ID3D12Resource> instance_descs;
    D3D12_RAYTRACING_INSTANCE_DESC instance_desc = { };
    instance_desc.Transform[0][0] = instance_desc.Transform[1][1] = instance_desc.Transform[2][2] = 1;
    instance_desc.InstanceMask = 1;
    instance_desc.AccelerationStructure = m_bottom_structure->GetGPUVirtualAddress();
    AllocateUploadBuffer(device, &instance_desc, sizeof(instance_desc), &instance_descs, L"InstanceDescs");

    // Bottom Level Acceleration Structure desc
    {
        bottom_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
        bottom_level_desc.DestAccelerationStructureData = m_bottom_structure->GetGPUVirtualAddress();
    }

    // Top Level Acceleration Structure desc
    {
        top_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
        top_level_desc.DestAccelerationStructureData = m_top_structure->GetGPUVirtualAddress();
        top_level_desc.Inputs.InstanceDescs = instance_descs->GetGPUVirtualAddress();
    }

    auto BuildAccelerationStructure = [&](auto* dxr_cmd)
    {
        dxr_cmd->BuildRaytracingAccelerationStructure(&bottom_level_desc, 0, nullptr);
        cmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bottom_structure.Get()));
        dxr_cmd->BuildRaytracingAccelerationStructure(&top_level_desc, 0, nullptr);
    };

    // Build acceleration structure.
    BuildAccelerationStructure(m_dxr_cmd.Get());

    // Kick off acceleration structure construction.
    m_device->ExecuteCommandList();

    // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
    m_device->WaitForGpu();
}

void Renderer::CreateConstantBuffers()
{
    auto device = m_device->GetD3DDevice();
    auto frame_count = m_device->GetBackBufferCount();

    // Create the constant buffer memory and map the CPU and GPU addresses
    const D3D12_HEAP_PROPERTIES heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    // Allocate one constant buffer per frame, since it gets updated every frame.
    size_t size = frame_count * sizeof(AlignedSceneConstantBuffer);
    const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(device->CreateCommittedResource(
        &heap_properties,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_frame_cb)));

    // Map the constant buffer and cache its heap pointers.
    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(m_frame_cb->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped_cb)));
}

void Renderer::BuildShaderTables()
{
    auto device = m_device->GetD3DDevice();

    void* raygen_id;
    void* miss_id;
    void* hit_group_id;

    auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
    {
        raygen_id = stateObjectProperties->GetShaderIdentifier(raygen_name);
        miss_id = stateObjectProperties->GetShaderIdentifier(miss_name);
        hit_group_id = stateObjectProperties->GetShaderIdentifier(hit_group_name);
    };

    // Get shader identifiers.
    UINT shader_id_size;
    {
        ComPtr<ID3D12StateObjectProperties> state_properties;
        ThrowIfFailed(m_dxr_state.As(&state_properties));
        GetShaderIdentifiers(state_properties.Get());
        shader_id_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    // Ray gen shader table
    {
        UINT record_count = 1;
        UINT record_size = shader_id_size;
        ShaderTable raygen_table(device, record_count, record_size, L"RayGenShaderTable");
        raygen_table.push_back(ShaderRecord(raygen_id, shader_id_size));
        m_raygen_table = raygen_table.GetResource();
    }

    // Miss shader table
    {
        UINT record_count = 1;
        UINT record_size = shader_id_size;
        ShaderTable miss_table(device, record_count, record_size, L"MissShaderTable");
        miss_table.push_back(ShaderRecord(miss_id, shader_id_size));
        m_miss_table = miss_table.GetResource();
    }

    // Hit group shader table
    {
        struct RootArguments
        {
            D3D12_GPU_DESCRIPTOR_HANDLE srv;
            CubeConstantBuffer cb;
        } arguments;
        arguments.srv = m_texture_mesh.srv;
        arguments.cb = m_cube_cb;

        UINT record_count = 1;
        UINT record_size = shader_id_size + sizeof(arguments);
        ShaderTable hit_group_table(device, record_count, record_size, L"HitGroupShaderTable");
        hit_group_table.push_back(ShaderRecord(hit_group_id, shader_id_size, &arguments, sizeof(arguments)));
        m_hit_group_table = hit_group_table.GetResource();
    }
}
