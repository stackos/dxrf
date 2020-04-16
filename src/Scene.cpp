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

            is.close();
        }

        return scene;
    }

    Scene::~Scene()
    {
        m_vertex_buffer.resource.Reset();
        m_device->ReleaseDescriptor(m_vertex_buffer.heap_index);
        m_index_buffer.resource.Reset();
        m_device->ReleaseDescriptor(m_index_buffer.heap_index);
    }

    void Scene::CreateGeometryBuffer()
    {
        std::vector<XMFLOAT3>* vertices = new std::vector<XMFLOAT3>();
        std::vector<uint16_t>* indices = new std::vector<uint16_t>();

        for (int i = 0; i < m_mesh_array.size(); ++i)
        {
            auto& mesh = m_mesh_array[i];

            mesh->vertex_offset = sizeof(XMFLOAT3) * vertices->size();
            mesh->vertex_stride = sizeof(XMFLOAT3) * 2;
            mesh->index_offset = sizeof(uint16_t) * indices->size();

            if (mesh->vertices.size() > 0)
            {
                size_t old_size = vertices->size();
                vertices->resize(old_size + mesh->vertices.size());
                memcpy(&vertices->at(old_size), &mesh->vertices[0], sizeof(XMFLOAT3) * mesh->vertices.size());
            }
            
            if (mesh->normals.size() > 0)
            {
                size_t old_size = vertices->size();
                vertices->resize(old_size + mesh->normals.size());
                memcpy(&vertices->at(old_size), &mesh->normals[0], sizeof(XMFLOAT3) * mesh->normals.size());
            }

            if (mesh->indices.size() > 0)
            {
                size_t old_size = indices->size();
                indices->resize(old_size + mesh->indices.size());
                memcpy(&indices->at(old_size), &mesh->indices[0], sizeof(uint16_t) * mesh->indices.size());
            }
        }

        AllocateUploadBuffer(m_device->GetD3DDevice(), &vertices->at(0), sizeof(XMFLOAT3) * vertices->size(), &m_vertex_buffer.resource);
        AllocateUploadBuffer(m_device->GetD3DDevice(), &indices->at(0), sizeof(uint16_t) * indices->size(), &m_index_buffer.resource);

        this->CreateBufferView(&m_vertex_buffer, UINT(vertices->size() / 2), UINT(sizeof(XMFLOAT3) * 2)); // position + normal
        this->CreateBufferView(&m_index_buffer, UINT(indices->size() / 2), 0); // 2 uint16_t as 1 uint32_t element

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
}
