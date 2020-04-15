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

using namespace DX;

namespace dxrf
{
    class Texture
    {
    private:
        struct D3DTexture
        {
            ComPtr<ID3D12Resource> resource;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { };
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = { };
            UINT heap_index = UINT_MAX;
        };

    public:
        static std::unique_ptr<Texture> CreateTextureFromData(DeviceResources* device, int width, int height, DXGI_FORMAT format, bool cube, void** faces_data);
        ~Texture();
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_texture.gpu_handle; }

    private:
        Texture() = default;

    private:
        DeviceResources* m_device;
        int m_width = 0;
        int m_height = 0;
        DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
        D3DTexture m_texture;
    };
}
