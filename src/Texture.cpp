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

#include "Texture.h"

namespace dxrf
{
    std::unique_ptr<Texture> Texture::CreateTextureFromData(DeviceResources* device, int width, int height, DXGI_FORMAT format, bool cube, void** faces_data)
    {
        auto d3d = device->GetD3DDevice();
        auto cmd = device->GetCommandList();

        std::unique_ptr<Texture> texture(new Texture());
        texture->m_device = device;
        texture->m_width = width;
        texture->m_height = height;
        texture->m_format = format;

        cmd->Reset(device->GetCommandAllocator(), nullptr);

        D3D12_SRV_DIMENSION view_dimension = cube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
        int array_size = cube ? 6 : 1;
        int mip_levels = 1;
        int pixel_size = 0;

        switch (format)
        {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                pixel_size = 4;
                break;
            default:
                assert(false);
                break;
        }

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

        ThrowIfFailed(d3d->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture->m_resource)));

        // Create the GPU upload buffer.
        const UINT64 upload_size = GetRequiredIntermediateSize(texture->m_resource.Get(), 0, array_size);

        ComPtr<ID3D12Resource> upload_heap;
        ThrowIfFailed(d3d->CreateCommittedResource(
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

        UpdateSubresources(cmd, texture->m_resource.Get(), upload_heap.Get(), 0, 0, array_size, &datas[0]);
        cmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture->m_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

        D3D12_CPU_DESCRIPTOR_HANDLE desc_handle;
        texture->m_srv_index = device->AllocateDescriptor(&desc_handle);

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
        d3d->CreateShaderResourceView(texture->m_resource.Get(), &srv_desc, desc_handle);
        texture->m_srv = device->GetGPUDescriptorHandle(texture->m_srv_index);

        device->ExecuteCommandList();
        device->WaitForGpu();

        return texture;
    }

    Texture::~Texture()
    {
        m_resource.Reset();
        m_device->ReleaseDescriptor(m_srv_index);
    }
}
