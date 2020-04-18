/*
MIT License

Copyright (c) 2020 stackos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
*/

#include "Scene.h"
#include "DirectXRaytracingHelper.h"
#include <DirectXMath.h>

namespace dxrf
{
    template<class T>
    static T Read(std::ifstream& is)
    {
        T t;
        is.read((char*) &t, sizeof(T));
        return t;
    }

    static std::string ReadString(std::ifstream& is)
    {
        int size = Read<int>(is);
        std::string str(size, 0);
        is.read(&str[0], size);
        return str;
    }

    static std::weak_ptr<Mesh> ReadMesh(const std::string& path, std::unordered_map<std::string, std::shared_ptr<Mesh>>& mesh_map, std::vector<std::shared_ptr<Mesh>>& m_mesh_array)
    {
        if (mesh_map.count(path) > 0)
        {
            return mesh_map[path];
        }

        std::shared_ptr<Mesh> mesh(new Mesh());
        mesh->index = (int) m_mesh_array.size();
        mesh_map[path] = mesh;
        m_mesh_array.push_back(mesh);

        std::ifstream is(path, std::ios::binary | std::ios::in);
        if (is)
        {
            mesh->name = ReadString(is);

            int vertex_count = Read<int>(is);
            if (vertex_count > 0)
            {
                mesh->vertices.resize(vertex_count);
                is.read((char*) &mesh->vertices[0], sizeof(XMFLOAT3) * vertex_count);
            }
            
            int color_count = Read<int>(is);
            mesh->colors.resize(color_count);
            for (int i = 0; i < color_count; ++i)
            {
                float r = Read<uint8_t>(is) / 255.0f;
                float g = Read<uint8_t>(is) / 255.0f;
                float b = Read<uint8_t>(is) / 255.0f;
                float a = Read<uint8_t>(is) / 255.0f;
                mesh->colors[i] = { r, g, b, a };
            }

            int uv_count = Read<int>(is);
            if (uv_count > 0)
            {
                mesh->uv.resize(uv_count);
                is.read((char*) &mesh->uv[0], sizeof(XMFLOAT2) * uv_count);
            }

            int uv2_count = Read<int>(is);
            if (uv2_count > 0)
            {
                mesh->uv2.resize(uv2_count);
                is.read((char*) &mesh->uv2[0], sizeof(XMFLOAT2) * uv2_count);
            }
            
            int normal_count = Read<int>(is);
            if (normal_count > 0)
            {
                mesh->normals.resize(normal_count);
                is.read((char*) &mesh->normals[0], sizeof(XMFLOAT3) * normal_count);
            }

            int tangent_count = Read<int>(is);
            if (tangent_count > 0)
            {
                mesh->tangents.resize(tangent_count);
                is.read((char*) &mesh->tangents[0], sizeof(XMFLOAT4) * tangent_count);
            }

            int bone_weight_count = Read<int>(is);
            mesh->bone_weights.resize(bone_weight_count);
            mesh->bone_indices.resize(bone_weight_count);
            for (int i = 0; i < bone_weight_count; ++i)
            {
                mesh->bone_weights[i] = Read<XMFLOAT4>(is);
                float index0 = (float) Read<uint8_t>(is);
                float index1 = (float) Read<uint8_t>(is);
                float index2 = (float) Read<uint8_t>(is);
                float index3 = (float) Read<uint8_t>(is);
                mesh->bone_indices[i] = { index0, index1, index2, index3 };
            }

            int index_count = Read<int>(is);
            if (index_count > 0)
            {
                mesh->indices.resize(index_count);
                is.read((char*) &mesh->indices[0], sizeof(uint16_t) * index_count);
            }

            int submesh_count = Read<int>(is);
            if (submesh_count > 0)
            {
                mesh->submeshes.resize(submesh_count);
                is.read((char*) &mesh->submeshes[0], sizeof(Submesh) * submesh_count);
            }

            int bindpose_count = Read<int>(is);
            if (bindpose_count > 0)
            {
                mesh->bindposes.resize(bindpose_count);
                is.read((char*) &mesh->bindposes[0], sizeof(XMMATRIX) * bindpose_count);
            }

            int blend_shape_count = Read<int>(is);
            if (blend_shape_count > 0)
            {
                mesh->blend_shapes.resize(blend_shape_count);
                for (int i = 0; i < blend_shape_count; ++i)
                {
                    mesh->blend_shapes[i].name = ReadString(is);
                    int frame_count = Read<int>(is);
                    assert(frame_count == 1);

                    float weight = Read<float>(is) / 100.0f;
                    
                    if (vertex_count > 0)
                    {
                        mesh->blend_shapes[i].vertices.resize(vertex_count);
                        is.read((char*) &mesh->blend_shapes[i].vertices[0], sizeof(XMFLOAT3) * vertex_count);
                    }

                    if (normal_count > 0)
                    {
                        mesh->blend_shapes[i].normals.resize(normal_count);
                        is.read((char*) &mesh->blend_shapes[i].normals[0], sizeof(XMFLOAT3) * normal_count);
                    }

                    if (tangent_count > 0)
                    {
                        mesh->blend_shapes[i].tangents.resize(tangent_count);
                        is.read((char*) &mesh->blend_shapes[i].tangents[0], sizeof(XMFLOAT3) * tangent_count);
                    }
                }
            }

            XMFLOAT3 bounds_center = Read<XMFLOAT3>(is);
            XMFLOAT3 bounds_size = Read<XMFLOAT3>(is);

            is.close();
        }

        return mesh;
    }

    static std::unique_ptr<MeshRenderer> ReadMeshRenderer(std::ifstream& is, Scene* scene)
    {
        std::unique_ptr<MeshRenderer> renderer(new MeshRenderer());

        int lightmap_index = Read<int>(is);
        XMFLOAT4 lightmap_scale_offset = Read<XMFLOAT4>(is);
        bool cast_shadow = Read<uint8_t>(is) == 1;
        bool receive_shadow = Read<uint8_t>(is) == 1;

        int keyword_count = Read<int>(is);
        for (int i = 0; i < keyword_count; ++i)
        {
            ReadString(is);
        }

        int material_count = Read<int>(is);
        for (int i = 0; i < material_count; ++i)
        {
            ReadString(is);
        }

        std::string mesh_path = ReadString(is);
        if (mesh_path.size() > 0)
        {
            std::string path = scene->GetDataDir() + "/" + mesh_path;
            renderer->mesh_key = path;
            renderer->mesh = ReadMesh(path, scene->GetMeshMap(), scene->GetMeshArray());
            renderer->mesh_index = renderer->mesh.lock()->index;
        }
        
        return renderer;
    }

    std::shared_ptr<Object> Scene::ReadObject(std::ifstream& is)
    {
        std::shared_ptr<Object> obj(new Object());

        std::string name = ReadString(is);
        int layer = Read<int>(is);
        bool active = Read<uint8_t>(is) == 1;
        XMFLOAT3 local_pos = Read<XMFLOAT3>(is);
        XMFLOAT4 local_rot = Read<XMFLOAT4>(is);
        XMFLOAT3 local_scale = Read<XMFLOAT3>(is);

        obj->name = name;
        XMMATRIX scaling = XMMatrixScaling(local_scale.x, local_scale.y, local_scale.z);
        XMMATRIX rotation = XMMatrixRotationQuaternion(XMLoadFloat4(&local_rot));
        XMMATRIX translation = XMMatrixTranslation(local_pos.x, local_pos.y, local_pos.z);
        obj->transform = scaling * rotation * translation;

        int com_count = Read<int>(is);
        for (int i = 0; i < com_count; ++i)
        {
            std::string com_name = ReadString(is);

            if (com_name == "MeshRenderer")
            {
                obj->mesh_renderer = ReadMeshRenderer(is, this);
                m_render_objects.push_back(obj);
            }
            else
            {
                assert(false);
            }
        }

        int child_count = Read<int>(is);
        obj->children.resize(child_count);
        for (int i = 0; i < child_count; ++i)
        {
            std::shared_ptr<Object> child = this->ReadObject(is);
            // apply parent transform, convert local transform to world transform
            child->transform = child->transform * obj->transform;
            obj->children[i] = child;
        }

        return obj;
    }

    std::unique_ptr<Scene> Scene::LoadFromFile(DeviceResources* device, const std::string& data_dir, const std::string& local_path)
    {
        std::unique_ptr<Scene> scene(new Scene());
        scene->m_device = device;
        scene->m_data_dir = data_dir;

        std::ifstream is(data_dir + "/" + local_path, std::ios::binary | std::ios::in);
        if (is)
        {
            scene->m_root_object = scene->ReadObject(is);
            scene->CreateGeometryBuffer();
            scene->CreateAccelerationStructures();

            is.close();
        }

        return scene;
    }

    Scene::~Scene()
    {
        m_bottom_structures.clear();
        m_top_structure.Reset();

        m_vertex_buffer.resource.Reset();
        m_device->ReleaseDescriptor(m_vertex_buffer.heap_index);
        m_index_buffer.resource.Reset();
        m_device->ReleaseDescriptor(m_index_buffer.heap_index);
    }

    void Scene::CreateGeometryBuffer()
    {
        std::vector<Vertex>* vertices = new std::vector<Vertex>();
        std::vector<uint16_t>* indices = new std::vector<uint16_t>();

        for (int i = 0; i < m_mesh_array.size(); ++i)
        {
            auto& mesh = m_mesh_array[i];

            mesh->vertex_buffer_offset = sizeof(Vertex) * vertices->size();
            mesh->index_buffer_offset = sizeof(uint16_t) * indices->size();

            size_t vertex_count = mesh->vertices.size();
            if (vertex_count > 0)
            {
                size_t old_size = vertices->size();
                vertices->resize(old_size + vertex_count);

                for (int i = 0; i < vertex_count; ++i)
                {
                    Vertex v = { };
                    v.position = mesh->vertices[i];
                    v.normal = mesh->normals[i];
                    if (mesh->uv.size() > 0)
                    {
                        v.uv = mesh->uv[i];
                    }
                    vertices->at(old_size + i) = v;
                }
            }

            if (mesh->indices.size() > 0)
            {
                size_t old_size = indices->size();
                indices->resize(old_size + mesh->indices.size());
                memcpy(&indices->at(old_size), &mesh->indices[0], sizeof(uint16_t) * mesh->indices.size());
            }
        }

        AllocateUploadBuffer(m_device->GetD3DDevice(), &vertices->at(0), sizeof(Vertex) * vertices->size(), &m_vertex_buffer.resource);
        AllocateUploadBuffer(m_device->GetD3DDevice(), &indices->at(0), sizeof(uint16_t) * indices->size(), &m_index_buffer.resource);

        this->CreateBufferView(&m_vertex_buffer, (UINT) vertices->size(), (UINT) sizeof(Vertex));
        this->CreateBufferView(&m_index_buffer, (UINT) (indices->size() / 2), 0); // 2 uint16_t as 1 uint32_t element

        delete vertices;
        delete indices;
    }

    void Scene::CreateBufferView(D3DBuffer* buffer, UINT num_elements, UINT element_size)
    {
        auto device = m_device->GetD3DDevice();

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = { };
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = num_elements;
        if (element_size == 0)
        {
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            desc.Buffer.StructureByteStride = 0;
        }
        else
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            desc.Buffer.StructureByteStride = element_size;
        }

        buffer->heap_index = m_device->AllocateDescriptor(&buffer->cpu_handle);
        device->CreateShaderResourceView(buffer->resource.Get(), &desc, buffer->cpu_handle);
        buffer->gpu_handle = m_device->GetGPUDescriptorHandle(buffer->heap_index);
    }

    void Scene::CreateAccelerationStructures()
    {
        auto d3d = m_device->GetD3DDevice();
        auto cmd = m_device->GetCommandList();

        cmd->Reset(m_device->GetCommandAllocator(), nullptr);

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometrys(m_mesh_array.size());
        for (size_t i = 0; i < geometrys.size(); ++i)
        {
            auto& geometry = geometrys[i];
            geometry = { };
            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            geometry.Triangles.Transform3x4 = 0;
            geometry.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
            geometry.Triangles.IndexBuffer = m_index_buffer.resource->GetGPUVirtualAddress() + m_mesh_array[i]->index_buffer_offset;
            geometry.Triangles.IndexCount = (UINT) m_mesh_array[i]->indices.size();
            geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometry.Triangles.VertexBuffer.StartAddress = m_vertex_buffer.resource->GetGPUVirtualAddress() + m_mesh_array[i]->vertex_buffer_offset;
            geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
            geometry.Triangles.VertexCount = (UINT) m_mesh_array[i]->vertices.size();
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS build_flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        m_bottom_structures.resize(m_mesh_array.size());
        std::vector<ComPtr<ID3D12Resource>> scratch_resources;

        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> bottom_level_descs(m_mesh_array.size());
        for (size_t i = 0; i < bottom_level_descs.size(); ++i)
        {
            auto& bottom_level_desc = bottom_level_descs[i];
            bottom_level_desc = { };
            auto& bottom_inputs = bottom_level_descs[i].Inputs;
            bottom_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bottom_inputs.Flags = build_flags;
            bottom_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            bottom_inputs.NumDescs = 1;
            bottom_inputs.pGeometryDescs = &geometrys[i];

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_info = { };
            m_device->GetDXRDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_inputs, &bottom_info);
            ThrowIfFalse(bottom_info.ResultDataMaxSizeInBytes > 0);

            AllocateUAVBuffer(d3d, bottom_info.ResultDataMaxSizeInBytes, &m_bottom_structures[i], D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

            ComPtr<ID3D12Resource> scratch_resource;
            AllocateUAVBuffer(d3d, bottom_info.ScratchDataSizeInBytes, &scratch_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scratch_resources.push_back(scratch_resource);

            bottom_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
            bottom_level_desc.DestAccelerationStructureData = m_bottom_structures[i]->GetGPUVirtualAddress();

            m_device->GetDXRCommandList()->BuildRaytracingAccelerationStructure(&bottom_level_desc, 0, nullptr);
        }
        auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        cmd->ResourceBarrier(1, &barrier);

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instance_descs(m_render_objects.size());
        for (size_t i = 0; i < instance_descs.size(); ++i)
        {
            auto& instance = instance_descs[i];
            instance = { };

            XMFLOAT4X4 transform;
            XMStoreFloat4x4(&transform, m_render_objects[i]->transform);
            for (int j = 0; j < 3; ++j)
            {
                for (int k = 0; k < 4; ++k)
                {
                    instance.Transform[j][k] = transform.m[k][j];
                }
            }

            instance.InstanceID = i;
            instance.InstanceMask = 1;
            instance.AccelerationStructure = m_bottom_structures[m_render_objects[i]->mesh_renderer->mesh_index]->GetGPUVirtualAddress();
            instance.InstanceContributionToHitGroupIndex = m_render_objects[i]->mesh_renderer->mesh_index;
            instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
        }

        ComPtr<ID3D12Resource> instance_desc_buffer;
        AllocateUploadBuffer(d3d, &instance_descs[0], sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instance_descs.size(), &instance_desc_buffer);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_desc = { };
        {
            auto& top_inputs = top_level_desc.Inputs;
            top_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            top_inputs.Flags = build_flags;
            top_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            top_inputs.NumDescs = (UINT) instance_descs.size();

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info = { };
            m_device->GetDXRDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&top_inputs, &top_info);
            ThrowIfFalse(top_info.ResultDataMaxSizeInBytes > 0);

            AllocateUAVBuffer(d3d, top_info.ResultDataMaxSizeInBytes, &m_top_structure, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

            ComPtr<ID3D12Resource> scratch_resource;
            AllocateUAVBuffer(d3d, top_info.ScratchDataSizeInBytes, &scratch_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scratch_resources.push_back(scratch_resource);

            top_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
            top_level_desc.DestAccelerationStructureData = m_top_structure->GetGPUVirtualAddress();
            top_level_desc.Inputs.InstanceDescs = instance_desc_buffer->GetGPUVirtualAddress();
        }
        m_device->GetDXRCommandList()->BuildRaytracingAccelerationStructure(&top_level_desc, 0, nullptr);

        m_device->ExecuteCommandList();
        m_device->WaitForGpu();
    }
}
