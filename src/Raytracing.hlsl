#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
StructuredBuffer<Vertex> Vertices : register(t1, space0);
ByteAddressBuffer Indices : register(t2, space0);

ConstantBuffer<SceneConstantBuffer> g_scene : register(b0);
ConstantBuffer<MeshConstantBuffer> g_mesh : register(b1);
TextureCube<float4> g_texture_global : register(t3);
Texture2D<float4> g_texture_local : register(t4);
SamplerState g_sampler : register(s0);

// Load three 16 bit indices from a byte addressed buffer.
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;    
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);
 
    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
    bool skip_shading;
    float ray_hit_t;
};

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

Vertex HitVertex(Vertex vertices[3], BuiltInTriangleIntersectionAttributes attr)
{
    Vertex v;

    v.position = vertices[0].position +
        attr.barycentrics.x * (vertices[1].position - vertices[0].position) +
        attr.barycentrics.y * (vertices[2].position - vertices[0].position);
    v.normal = vertices[0].normal +
        attr.barycentrics.x * (vertices[1].normal - vertices[0].normal) +
        attr.barycentrics.y * (vertices[2].normal - vertices[0].normal);
    v.uv = vertices[0].uv +
        attr.barycentrics.x * (vertices[1].uv - vertices[0].uv) +
        attr.barycentrics.y * (vertices[2].uv - vertices[0].uv);

    return v;
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), g_scene.projection_to_world);

    world.xyz /= world.w;
    origin = g_scene.camera_position.xyz;
    direction = normalize(world.xyz - origin);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;
    
    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    ray.TMin = 0.01;
    ray.TMax = 1000.0;
    RayPayload payload = { float4(0, 0, 0, 0), false, FLT_MAX };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.ray_hit_t = RayTCurrent();
    if (payload.skip_shading)
    {
        return;
    }

    // Get the base index of the triangle's first 16 bit index.
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(g_mesh.index_buffer_offset + baseIndex);

    const uint vertex_offset = g_mesh.vertex_buffer_offset / g_mesh.vertex_stride;
    Vertex vertices[3] = { 
        Vertices[vertex_offset + indices[0]],
        Vertices[vertex_offset + indices[1]],
        Vertices[vertex_offset + indices[2]]
    };
    Vertex vertex = HitVertex(vertices, attr);

    //float4 color = g_texture_local.SampleLevel(g_sampler, vertex.uv, 0);

    float3 hit_pos = HitWorldPosition();
    float3 normal = normalize(mul(vertex.normal, ObjectToWorld3x4()).xyz);
    float3 light_offset = g_scene.light_position.xyz - hit_pos;
    float3 light_dir = normalize(light_offset);
    float3 color = max(0.0, dot(normal, light_dir));

    float light_dis = length(light_offset);
    float light_atten_a = 1.0;
    float light_atten_b = 1.0;
    float light_atten_c = 1.0;
    float light_atten = 1.0 / (light_atten_a * light_dis * light_dis + light_atten_b * light_dis + light_atten_c);
    float light_intensity = 60.0;
    color *= light_atten * light_intensity;

    // Trace shadow ray
    float shadow = 1.0;
    RayDesc shadow_ray;
    shadow_ray.Origin = hit_pos;
    shadow_ray.Direction = light_dir;
    shadow_ray.TMin = 0.01;
    shadow_ray.TMax = 1000.0;
    RayPayload shadow_payload = { float4(0, 0, 0, 0), true, FLT_MAX };
    TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 1, 0, shadow_ray, shadow_payload);
    if (shadow_payload.ray_hit_t < FLT_MAX)
    {
        shadow = 0.0;
    }
    color *= shadow;

    // Tone mapping
    color = float3(1.0, 1.0, 1.0) - exp(-color);

    payload.color = float4(color, 1.0);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    if (payload.skip_shading)
    {
        return;
    }

    float4 background = g_texture_global.SampleLevel(g_sampler, WorldRayDirection(), 0);
    payload.color = background;
}

#endif
