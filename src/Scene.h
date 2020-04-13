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
#include <memory>
#include <string>
#include <unordered_map>

using namespace DX;

namespace dxrf
{
    struct Submesh
    {
        int index_first;
        int index_count;
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
        std::weak_ptr<Mesh> mesh;
    };

    struct Object
    {
        std::string name;
        XMMATRIX transform;
        std::vector<std::unique_ptr<Object>> children;
        std::unique_ptr<MeshRenderer> mesh_renderer;
    };

    class Scene
    {
    public:
        static std::unique_ptr<Scene> LoadFromFile(DeviceResources* device, const std::string& data_dir, const std::string& local_path);
        ~Scene();
        std::unordered_map<std::string, std::shared_ptr<Mesh>>& GetMeshes() { return m_meshes; }
        const std::string& GetDataDir() const { return m_data_dir; }

    private:
        Scene() = default;

    private:
        DeviceResources* m_device;
        std::string m_data_dir;
        std::unique_ptr<Object> m_obj;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> m_meshes;
    };
}
