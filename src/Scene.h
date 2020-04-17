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

#pragma once

#include "DeviceResources.h"
#include "RaytracingHlslCompat.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <fstream>

using namespace DX;

namespace dxrf
{
    struct D3DBuffer
    {
        ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { };
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = { };
        UINT heap_index = UINT_MAX;
    };

    struct Submesh
    {
        int index_first = -1;
        int index_count = 0;
    };

    struct BlendShape
    {
        std::string name;
        std::vector<XMFLOAT3> vertices;
        std::vector<XMFLOAT3> normals;
        std::vector<XMFLOAT3> tangents;
    };

    struct Mesh
    {
        int index = -1;
        size_t vertex_buffer_offset = 0;
        size_t vertex_stride = 0;
        size_t index_buffer_offset = 0;
        std::string name;
        std::vector<XMFLOAT3> vertices;
        std::vector<XMFLOAT4> colors;
        std::vector<XMFLOAT2> uv;
        std::vector<XMFLOAT2> uv2;
        std::vector<XMFLOAT3> normals;
        std::vector<XMFLOAT4> tangents;
        std::vector<XMFLOAT4> bone_weights;
        std::vector<XMFLOAT4> bone_indices;
        std::vector<uint16_t> indices;
        std::vector<Submesh> submeshes;
        std::vector<XMMATRIX> bindposes;
        std::vector<BlendShape> blend_shapes;
    };

    struct MeshRenderer
    {
        int mesh_index = -1;
        std::string mesh_key;
        std::weak_ptr<Mesh> mesh;
    };

    struct Object
    {
        std::string name;
        XMMATRIX transform;
        std::vector<std::shared_ptr<Object>> children;
        std::unique_ptr<MeshRenderer> mesh_renderer;
    };

    class Scene
    {
    public:
        static std::unique_ptr<Scene> LoadFromFile(DeviceResources* device, const std::string& data_dir, const std::string& local_path);
        ~Scene();
        const std::string& GetDataDir() const { return m_data_dir; }
        std::unordered_map<std::string, std::shared_ptr<Mesh>>& GetMeshMap() { return m_mesh_map; }
        std::vector<std::shared_ptr<Mesh>>& GetMeshArray() { return m_mesh_array; }
        const std::vector<std::shared_ptr<Object>>& GetRenderObjects() const { return m_render_objects; }

    private:
        Scene() = default;
        std::shared_ptr<Object> ReadObject(std::ifstream& is);
        void CreateGeometryBuffer();
        void CreateBufferView(D3DBuffer* buffer, UINT num_elements, UINT element_size);

    private:
        DeviceResources* m_device;
        std::string m_data_dir;
        std::shared_ptr<Object> m_root_object;
        std::vector<std::shared_ptr<Object>> m_render_objects;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> m_mesh_map;
        std::vector<std::shared_ptr<Mesh>> m_mesh_array;
        D3DBuffer m_vertex_buffer;
        D3DBuffer m_index_buffer;
    };
}
