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
        IndexBuffersSlot,
        TextureSlot,
        Count
    };
}

namespace LocalRootSignatureParams
{
    enum Value
    {
        MeshConstantSlot = 0,
        TextureSlot,
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

    m_eye = { -5.5f, 5.12f, -6.0f, 1.0f };
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
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fov), m_aspect, 0.01f, 1000.0f);
    XMMATRIX view_proj = view * proj;

    m_scene_cb[frame_index].camera_position = m_eye;
    m_scene_cb[frame_index].projection_to_world = XMMatrixInverse(nullptr, view_proj);
}

void Renderer::CreateDeviceDependentResources()
{
    {
        int w, h, c;

        char path[MAX_PATH];
        sprintf(path, "%s/assets/720x1280.png", m_work_dir);
        void* data = stbi_load(path, &w, &h, &c, 4);
        if (data)
        {
            m_texture_mesh = Texture::CreateTextureFromData(m_device.get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false, &data);

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

        m_texture_bg = Texture::CreateTextureFromData(m_device.get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true, &datas[0]);

        for (int i = 0; i < 6; ++i)
        {
            stbi_image_free(datas[i]);
        }
    }

    this->CreateRootSignatures();
    this->CreateRaytracingPipelineStateObject();
    this->LoadScene();
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
    m_texture_bg.reset();
    m_texture_mesh.reset();

    m_raygen_table.Reset();
    m_miss_table.Reset();
    m_hit_group_table.Reset();

    m_frame_cb.Reset();

    m_scene.reset();

    m_dxr_state.Reset();

    m_raytracing_global_sig.Reset();
    m_raytracing_local_sig.Reset();  
}

void Renderer::ReleaseWindowSizeDependentResources()
{
    m_raytracing_output.Reset();
    m_device->ReleaseDescriptor(m_raytracing_output_descriptor_index);
    m_raytracing_output_descriptor_index = UINT_MAX;
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
    m_raytracing_output_descriptor_index = m_device->AllocateDescriptor(&desc_handle, m_raytracing_output_descriptor_index);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_raytracing_output.Get(), nullptr, &uav_desc, desc_handle);
    m_raytracing_output_descriptor = m_device->GetGPUDescriptorHandle(m_raytracing_output_descriptor_index);
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
        desc->HitGroupTable.StrideInBytes = m_hit_group_stride;
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
        auto descriptor_heap = m_device->GetDescriptorHeap();
        desc_cmd->SetDescriptorHeaps(1, &descriptor_heap);
        // Set index and successive vertex buffer decriptor tables
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, m_scene->GetVertexBuffer().gpu_handle);
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::IndexBuffersSlot, m_scene->GetIndexBuffer().gpu_handle);
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, m_raytracing_output_descriptor);
        cmd->SetComputeRootDescriptorTable(GlobalRootSignatureParams::TextureSlot, m_texture_bg->GetGpuHandle());
    };

    cmd->SetComputeRootSignature(m_raytracing_global_sig.Get());

    // Copy the updated scene constant buffer to GPU.
    memcpy(&m_mapped_cb[frame_index].constants, &m_scene_cb[frame_index], sizeof(m_scene_cb[frame_index]));
    auto cb_gpu_address = m_frame_cb->GetGPUVirtualAddress() + frame_index * sizeof(m_mapped_cb[0]);
    cmd->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cb_gpu_address);

    // Bind the heaps, acceleration structure and dispatch rays.
    D3D12_DISPATCH_RAYS_DESC dispatch_sesc = { };
    SetCommonPipelineState(cmd);
    cmd->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, m_scene->GetTopLevelStructure()->GetGPUVirtualAddress());
    DispatchRays(m_device->GetDXRCommandList(), m_dxr_state.Get(), &dispatch_sesc);
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

        CD3DX12_DESCRIPTOR_RANGE ranges[4]; // Perfomance TIP: Order from most frequent to least frequent.
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // output texture
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // vertex buffers
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // index buffers
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // material texture

        CD3DX12_ROOT_PARAMETER parameters[GlobalRootSignatureParams::Count];
        parameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]); // u0
        parameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0); // t0
        parameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0); // b0
        parameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]); // t1
        parameters[GlobalRootSignatureParams::IndexBuffersSlot].InitAsDescriptorTable(1, &ranges[2]); // t2
        parameters[GlobalRootSignatureParams::TextureSlot].InitAsDescriptorTable(1, &ranges[3]); // t3
        CD3DX12_ROOT_SIGNATURE_DESC desc(ARRAYSIZE(parameters), parameters, 1, &sampler);
        this->SerializeAndCreateRaytracingRootSignature(desc, &m_raytracing_global_sig);
    }

    {
        CD3DX12_DESCRIPTOR_RANGE ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); // material texture

        CD3DX12_ROOT_PARAMETER parameters[LocalRootSignatureParams::Count];
        parameters[LocalRootSignatureParams::MeshConstantSlot].InitAsConstants(SizeOfInUint32(MeshConstantBuffer), 1); // b1
        parameters[LocalRootSignatureParams::TextureSlot].InitAsDescriptorTable(1, &ranges[0]); // t4
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

    ThrowIfFailed(m_device->GetDXRDevice()->CreateStateObject(pipeline, IID_PPV_ARGS(&m_dxr_state)), "Couldn't create DirectX Raytracing state object.\n");
}

void Renderer::LoadScene()
{
    char data_dir[MAX_PATH];
    sprintf(data_dir, "%s/assets/scene", m_work_dir);

    m_scene = Scene::LoadFromFile(m_device.get(), data_dir, "objects.go");
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
            MeshConstantBuffer mesh_cb;
            D3D12_GPU_DESCRIPTOR_HANDLE srv;
        };
        const auto& meshes = m_scene->GetMeshArray();
        std::vector<RootArguments> arguments(meshes.size());
        for (size_t i = 0; i < arguments.size(); ++i)
        {
            arguments[i].mesh_cb.mesh_index = (UINT) i;
            arguments[i].mesh_cb.vertex_buffer_offset = (UINT) meshes[i]->vertex_buffer_offset;
            arguments[i].mesh_cb.vertex_stride = sizeof(Vertex);
            arguments[i].mesh_cb.index_buffer_offset = (UINT) meshes[i]->index_buffer_offset;
            arguments[i].mesh_cb.color = { 1, 1, 1, 1 };
            arguments[i].srv = m_texture_mesh->GetGpuHandle();
        }
        arguments[1].mesh_cb.color = { 1, 0, 0, 1 };
        arguments[2].mesh_cb.color = { 0, 1, 0, 1 };
        arguments[3].mesh_cb.color = { 0, 0, 1, 1 };
        arguments[4].mesh_cb.color = { 1, 1, 0, 1 };

        UINT record_count = (UINT) arguments.size();
        UINT record_size = shader_id_size + sizeof(RootArguments);
        ShaderTable hit_group_table(device, record_count, record_size, L"HitGroupShaderTable");
        for (size_t i = 0; i < arguments.size(); ++i)
        {
            hit_group_table.push_back(ShaderRecord(hit_group_id, shader_id_size, &arguments[i], sizeof(RootArguments)));
        }
        m_hit_group_table = hit_group_table.GetResource();
        m_hit_group_stride = hit_group_table.GetShaderRecordSize();
    }
}
