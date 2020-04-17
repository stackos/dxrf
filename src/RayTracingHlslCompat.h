#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

#ifdef HLSL
#include "HlslCompat.h"
#else
using namespace DirectX;

// Shader will use byte encoding to access indices.
typedef UINT16 Index;
#endif

struct SceneConstantBuffer
{
    XMMATRIX projection_to_world;
    XMVECTOR camera_position;
};

struct MeshConstantBuffer
{
    UINT mesh_index;
    XMFLOAT3 padding;
    XMFLOAT4 color;
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
};

struct RayTraceMeshInfo
{
    UINT vertex_buffer_offset;
    UINT vertex_stride;
    UINT index_buffer_offset;
};

#endif
