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
    std::shared_ptr<Texture> Texture::CreateTexture2DFromData(int width, int height, DXGI_FORMAT format, const void* data, bool gen_mip_levels)
    {
        std::shared_ptr<Texture> texture(new Texture());
        texture->m_width = width;
        texture->m_height = height;
        texture->m_format = format;

        return texture;
    }

    Texture::~Texture()
    {

    }
}
