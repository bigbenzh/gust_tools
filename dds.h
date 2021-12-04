/*
  DDS definitions
  Copyright © 2019-2021 VitaSmith

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>

#pragma once

#define DDS_MAGIC                   0x20534444  // "DDS "

#define DDS_ALPHAPIXELS             0x00000001  // DDPF_ALPHAPIXELS
#define DDS_ALPHA                   0x00000002  // DDPF_ALPHA
#define DDS_FOURCC                  0x00000004  // DDPF_FOURCC
#define DDS_PAL4                    0x00000008  // DDPF_PALETTEINDEXED4
#define DDS_PAL8                    0x00000020  // DDPF_PALETTEINDEXED8
#define DDS_RGB                     0x00000040  // DDPF_RGB
#define DDS_RGBA                    0x00000041  // DDPF_RGB | DDPF_ALPHAPIXELS
#define DDS_PAL1                    0x00000800  // DDPF_PALETTEINDEXED1
#define DDS_PAL2                    0x00001000  // DDPF_PALETTEINDEXED2
#define DDS_PREMULTALPHA            0x00008000  // DDPF_ALPHAPREMULT
#define DDS_LUMINANCE               0x00020000  // DDPF_LUMINANCE
#define DDS_LUMINANCEA              0x00020001  // DDPF_LUMINANCE | DDPF_ALPHAPIXELS
// Custom NVTT flags.
#define DDS_SRGB                    0x40000000; // DDPF_SRGB
#define DDS_NORMAL                  0x80000000; // DDPF_NORMAL

#define DDS_HEADER_FLAGS_TEXTURE    0x00001007  // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
#define DDS_HEADER_FLAGS_MIPMAP     0x00020000  // DDSD_MIPMAPCOUNT
#define DDS_HEADER_FLAGS_VOLUME     0x00800000  // DDSD_DEPTH
#define DDS_HEADER_FLAGS_PITCH      0x00000008  // DDSD_PITCH
#define DDS_HEADER_FLAGS_LINEARSIZE 0x00080000  // DDSD_LINEARSIZE

#define DDS_HEIGHT                  0x00000002  // DDSD_HEIGHT
#define DDS_WIDTH                   0x00000004  // DDSD_WIDTH

#define DDS_SURFACE_FLAGS_TEXTURE   0x00001000  // DDSCAPS_TEXTURE
#define DDS_SURFACE_FLAGS_MIPMAP    0x00400008  // DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
#define DDS_SURFACE_FLAGS_CUBEMAP   0x00000008  // DDSCAPS_COMPLEX

#define DDS_CUBEMAP_POSITIVEX       0x00000600  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX
#define DDS_CUBEMAP_NEGATIVEX       0x00000a00  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEX
#define DDS_CUBEMAP_POSITIVEY       0x00001200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEY
#define DDS_CUBEMAP_NEGATIVEY       0x00002200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEY
#define DDS_CUBEMAP_POSITIVEZ       0x00004200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEZ
#define DDS_CUBEMAP_NEGATIVEZ       0x00008200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEZ

#define DDS_CUBEMAP_ALLFACES (DDS_CUBEMAP_POSITIVEX | DDS_CUBEMAP_NEGATIVEX |\
                              DDS_CUBEMAP_POSITIVEY | DDS_CUBEMAP_NEGATIVEY |\
                              DDS_CUBEMAP_POSITIVEZ | DDS_CUBEMAP_NEGATIVEZ)

#define DDS_CUBEMAP                 0x00000200  // DDSCAPS2_CUBEMAP

#define DDS_FLAGS_VOLUME            0x00200000  // DDSCAPS2_VOLUME

enum DDS_FORMAT {
    DDS_FORMAT_UNKNOWN,
    DDS_FORMAT_ABGR4,               // Should be the first uncompressed format with RGBA components
    DDS_FORMAT_ARGB4,               // as needed per rgba_convert() format validation.
    DDS_FORMAT_GRAB4,
    DDS_FORMAT_RGBA4,
    DDS_FORMAT_ABGR8,
    DDS_FORMAT_ARGB8,
    DDS_FORMAT_GRAB8,
    DDS_FORMAT_RGBA8,               // Should be the last uncompressed format with RGBA components
    DDS_FORMAT_RXGB8,
    DDS_FORMAT_BGR8,
    DDS_FORMAT_R8,
    DDS_FORMAT_UVER,
    DDS_FORMAT_DXT1,
    DDS_FORMAT_DXT2,
    DDS_FORMAT_DXT3,
    DDS_FORMAT_DXT4,
    DDS_FORMAT_DXT5,
    DDS_FORMAT_DX10,
    DDS_FORMAT_BC4,
    DDS_FORMAT_BC5,
    DDS_FORMAT_BC6,
    DDS_FORMAT_BC7,
    DDS_FORMAT_BC6H,
    DDS_FORMAT_BC7L,
    DDS_FORMAT_ATI1,
    DDS_FORMAT_ATI2,
    DDS_FORMAT_A2XY,
    DDS_FORMAT_DDS,
    DDS_FORMAT_NVTT,
};

// DDS format: Pixel block width/height (assumed square)
static __inline unsigned int dds_bwh(enum DDS_FORMAT f) {
    switch (f) {
    case DDS_FORMAT_DXT1:
    case DDS_FORMAT_DXT2:
    case DDS_FORMAT_DXT3:
    case DDS_FORMAT_DXT4:
    case DDS_FORMAT_DXT5:
    case DDS_FORMAT_DX10:
    case DDS_FORMAT_BC4:
    case DDS_FORMAT_BC5:
    case DDS_FORMAT_BC6:
    case DDS_FORMAT_BC6H:
    case DDS_FORMAT_BC7:
    case DDS_FORMAT_ATI1:
    case DDS_FORMAT_ATI2:
        return 4;
    default:
        return 1;
    }
}

// DDS format: Bytes per pixel block
static __inline unsigned int dds_bpb(enum DDS_FORMAT f) {
    switch (f) {
    case DDS_FORMAT_BGR8:
        return 3;
    case DDS_FORMAT_ABGR8:
    case DDS_FORMAT_ARGB8:
    case DDS_FORMAT_GRAB8:
    case DDS_FORMAT_RGBA8:
    case DDS_FORMAT_RXGB8:
        return 4;
    case DDS_FORMAT_ABGR4:
    case DDS_FORMAT_ARGB4:
    case DDS_FORMAT_GRAB4:
    case DDS_FORMAT_RGBA4:
        return 2;
    case DDS_FORMAT_R8:
        return 1;
    case DDS_FORMAT_DXT1:
    case DDS_FORMAT_BC4:
    case DDS_FORMAT_ATI1:
        return 8;
    case DDS_FORMAT_DXT2:
    case DDS_FORMAT_DXT3:
    case DDS_FORMAT_DXT4:
    case DDS_FORMAT_DXT5:
    case DDS_FORMAT_DX10:
    case DDS_FORMAT_BC5:
    case DDS_FORMAT_BC6:
    case DDS_FORMAT_BC6H:
    case DDS_FORMAT_BC7:
    case DDS_FORMAT_ATI2:
        return 16;
    default:
        // No idea, so assert and return 0
        assert(false);
        return 0;
    }
}

// DDS format: Bits per individual pixel
static __inline unsigned int dds_bpp(enum DDS_FORMAT f) {
    switch (f) {
    case DDS_FORMAT_BGR8:
        return 24;
    case DDS_FORMAT_ABGR8:
    case DDS_FORMAT_ARGB8:
    case DDS_FORMAT_GRAB8:
    case DDS_FORMAT_RGBA8:
    case DDS_FORMAT_RXGB8:
        return 32;
    case DDS_FORMAT_ABGR4:
    case DDS_FORMAT_ARGB4:
    case DDS_FORMAT_GRAB4:
    case DDS_FORMAT_RGBA4:
        return 16;
    case DDS_FORMAT_R8:
        return 8;
    case DDS_FORMAT_DXT1:
    case DDS_FORMAT_BC4:
    case DDS_FORMAT_ATI1:
        return 4;
    case DDS_FORMAT_DXT2:
    case DDS_FORMAT_DXT3:
    case DDS_FORMAT_DXT4:
    case DDS_FORMAT_DXT5:
    case DDS_FORMAT_DX10:
    case DDS_FORMAT_BC5:
    case DDS_FORMAT_BC6:
    case DDS_FORMAT_BC6H:
    case DDS_FORMAT_BC7:
    case DDS_FORMAT_ATI2:
        return 8;
    default:
        // No idea, so assert and return 0
        assert(false);
        return 0;
    }
}

// Compute the mipmap size at level l for a texture of DDS_FORMAT f of dimensions w x h
// See https://docs.microsoft.com/en-us/windows/win32/direct3ddds/dds-file-layout-for-textures
#define MIPMAP_SIZE(f, l, w, h) (max(1, (((w) / (1 << (l)) + dds_bwh(f) - 1) / dds_bwh(f))) * \
                                 max(1, (((h) / (1 << (l)) + dds_bwh(f) - 1) / dds_bwh(f))) * dds_bpb(f))

enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN,
    DXGI_FORMAT_R32G32B32A32_TYPELESS,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_UINT,
    DXGI_FORMAT_R32G32B32A32_SINT,
    DXGI_FORMAT_R32G32B32_TYPELESS,
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32_UINT,
    DXGI_FORMAT_R32G32B32_SINT,
    DXGI_FORMAT_R16G16B16A16_TYPELESS,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R16G16B16A16_UNORM,
    DXGI_FORMAT_R16G16B16A16_UINT,
    DXGI_FORMAT_R16G16B16A16_SNORM,
    DXGI_FORMAT_R16G16B16A16_SINT,
    DXGI_FORMAT_R32G32_TYPELESS,
    DXGI_FORMAT_R32G32_FLOAT,
    DXGI_FORMAT_R32G32_UINT,
    DXGI_FORMAT_R32G32_SINT,
    DXGI_FORMAT_R32G8X24_TYPELESS,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
    DXGI_FORMAT_R10G10B10A2_TYPELESS,
    DXGI_FORMAT_R10G10B10A2_UNORM,
    DXGI_FORMAT_R10G10B10A2_UINT,
    DXGI_FORMAT_R11G11B10_FLOAT,
    DXGI_FORMAT_R8G8B8A8_TYPELESS,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    DXGI_FORMAT_R8G8B8A8_UINT,
    DXGI_FORMAT_R8G8B8A8_SNORM,
    DXGI_FORMAT_R8G8B8A8_SINT,
    DXGI_FORMAT_R16G16_TYPELESS,
    DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R16G16_UNORM,
    DXGI_FORMAT_R16G16_UINT,
    DXGI_FORMAT_R16G16_SNORM,
    DXGI_FORMAT_R16G16_SINT,
    DXGI_FORMAT_R32_TYPELESS,
    DXGI_FORMAT_D32_FLOAT,
    DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R32_UINT,
    DXGI_FORMAT_R32_SINT,
    DXGI_FORMAT_R24G8_TYPELESS,
    DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
    DXGI_FORMAT_X24_TYPELESS_G8_UINT,
    DXGI_FORMAT_R8G8_TYPELESS,
    DXGI_FORMAT_R8G8_UNORM,
    DXGI_FORMAT_R8G8_UINT,
    DXGI_FORMAT_R8G8_SNORM,
    DXGI_FORMAT_R8G8_SINT,
    DXGI_FORMAT_R16_TYPELESS,
    DXGI_FORMAT_R16_FLOAT,
    DXGI_FORMAT_D16_UNORM,
    DXGI_FORMAT_R16_UNORM,
    DXGI_FORMAT_R16_UINT,
    DXGI_FORMAT_R16_SNORM,
    DXGI_FORMAT_R16_SINT,
    DXGI_FORMAT_R8_TYPELESS,
    DXGI_FORMAT_R8_UNORM,
    DXGI_FORMAT_R8_UINT,
    DXGI_FORMAT_R8_SNORM,
    DXGI_FORMAT_R8_SINT,
    DXGI_FORMAT_A8_UNORM,
    DXGI_FORMAT_R1_UNORM,
    DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
    DXGI_FORMAT_R8G8_B8G8_UNORM,
    DXGI_FORMAT_G8R8_G8B8_UNORM,
    DXGI_FORMAT_BC1_TYPELESS,
    DXGI_FORMAT_BC1_UNORM,
    DXGI_FORMAT_BC1_UNORM_SRGB,
    DXGI_FORMAT_BC2_TYPELESS,
    DXGI_FORMAT_BC2_UNORM,
    DXGI_FORMAT_BC2_UNORM_SRGB,
    DXGI_FORMAT_BC3_TYPELESS,
    DXGI_FORMAT_BC3_UNORM,
    DXGI_FORMAT_BC3_UNORM_SRGB,
    DXGI_FORMAT_BC4_TYPELESS,
    DXGI_FORMAT_BC4_UNORM,
    DXGI_FORMAT_BC4_SNORM,
    DXGI_FORMAT_BC5_TYPELESS,
    DXGI_FORMAT_BC5_UNORM,
    DXGI_FORMAT_BC5_SNORM,
    DXGI_FORMAT_B5G6R5_UNORM,
    DXGI_FORMAT_B5G5R5A1_UNORM,
    DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_B8G8R8X8_UNORM,
    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
    DXGI_FORMAT_B8G8R8A8_TYPELESS,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
    DXGI_FORMAT_B8G8R8X8_TYPELESS,
    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
    DXGI_FORMAT_BC6H_TYPELESS,
    DXGI_FORMAT_BC6H_UF16,
    DXGI_FORMAT_BC6H_SF16,
    DXGI_FORMAT_BC7_TYPELESS,
    DXGI_FORMAT_BC7_UNORM,
    DXGI_FORMAT_BC7_UNORM_SRGB,
    DXGI_FORMAT_AYUV,
    DXGI_FORMAT_Y410,
    DXGI_FORMAT_Y416,
    DXGI_FORMAT_NV12,
    DXGI_FORMAT_P010,
    DXGI_FORMAT_P016,
    DXGI_FORMAT_420_OPAQUE,
    DXGI_FORMAT_YUY2,
    DXGI_FORMAT_Y210,
    DXGI_FORMAT_Y216,
    DXGI_FORMAT_NV11,
    DXGI_FORMAT_AI44,
    DXGI_FORMAT_IA44,
    DXGI_FORMAT_P8,
    DXGI_FORMAT_A8P8,
    DXGI_FORMAT_B4G4R4A4_UNORM,
    DXGI_FORMAT_P208,
    DXGI_FORMAT_V208,
    DXGI_FORMAT_V408,
    DXGI_FORMAT_FORCE_UINT
};

enum D3D10_RESOURCE_DIMENSION {
    D3D10_RESOURCE_DIMENSION_UNKNOWN,
    D3D10_RESOURCE_DIMENSION_BUFFER,
    D3D10_RESOURCE_DIMENSION_TEXTURE1D,
    D3D10_RESOURCE_DIMENSION_TEXTURE2D,
    D3D10_RESOURCE_DIMENSION_TEXTURE3D
};

enum D3D11_RESOURCE_MISC_FLAG {
    D3D11_RESOURCE_MISC_GENERATE_MIPS = 0x1L,
    D3D11_RESOURCE_MISC_SHARED = 0x2L,
    D3D11_RESOURCE_MISC_TEXTURECUBE = 0x4L,
    D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS = 0x10L,
    D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS = 0x20L,
    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 0x40L,
    D3D11_RESOURCE_MISC_RESOURCE_CLAMP = 0x80L,
    D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX = 0x100L,
    D3D11_RESOURCE_MISC_GDI_COMPATIBLE = 0x200L,
    D3D11_RESOURCE_MISC_SHARED_NTHANDLE = 0x800L,
    D3D11_RESOURCE_MISC_RESTRICTED_CONTENT = 0x1000L,
    D3D11_RESOURCE_MISC_RESTRICT_SHARED_RESOURCE = 0x2000L,
    D3D11_RESOURCE_MISC_RESTRICT_SHARED_RESOURCE_DRIVER = 0x4000L,
    D3D11_RESOURCE_MISC_GUARDED = 0x8000L,
    D3D11_RESOURCE_MISC_TILE_POOL = 0x20000L,
    D3D11_RESOURCE_MISC_TILED = 0x40000L,
    D3D11_RESOURCE_MISC_HW_PROTECTED = 0x80000L,
    D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE = 0x100000L,
    D3D11_RESOURCE_MISC_SHARED_EXCLUSIVE_WRITER = 0x200000L
};

enum DDS_ALPHA_MODE {
    DDS_ALPHA_MODE_UNKNOWN,
    DDS_ALPHA_MODE_STRAIGHT,
    DDS_ALPHA_MODE_PREMULTIPLIED,
    DDS_ALPHA_MODE_OPAQUE,
    DDS_ALPHA_MODE_CUSTOM
};

#pragma pack(push, 1)

typedef struct
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t RGBBitCount;
    uint32_t RBitMask;
    uint32_t GBitMask;
    uint32_t BBitMask;
    uint32_t ABitMask;
} DDS_PIXELFORMAT;

typedef struct
{
    uint32_t        size;
    uint32_t        flags;
    uint32_t        height;
    uint32_t        width;
    uint32_t        pitchOrLinearSize;
    uint32_t        depth;
    uint32_t        mipMapCount;
    uint32_t        reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        caps;
    uint32_t        caps2;
    uint32_t        caps3;
    uint32_t        caps4;
    uint32_t        reserved2;
} DDS_HEADER;

typedef struct
{
    uint32_t        dxgiFormat;
    uint32_t        resourceDimension;
    uint32_t        miscFlag;
    uint32_t        arraySize;
    uint32_t        miscFlags2;
} DDS_HEADER_DXT10;

#pragma pack(pop)

#if !defined(MAKEFOURCC)
#define MAKEFOURCC(cc0, cc1, cc2, cc3)  ((uint32_t)(cc0) | ((uint32_t)(cc1) << 8) | ((uint32_t)(cc2) << 16) | ((uint32_t)(cc3) << 24))
#endif

static __inline uint32_t get_fourCC(int format)
{
    switch (format) {
    case DDS_FORMAT_DXT1:
        return MAKEFOURCC('D', 'X', 'T', '1');
    case DDS_FORMAT_DXT2:
        return MAKEFOURCC('D', 'X', 'T', '2');
    case DDS_FORMAT_DXT3:
        return MAKEFOURCC('D', 'X', 'T', '3');
    case DDS_FORMAT_DXT4:
        return MAKEFOURCC('D', 'X', 'T', '4');
    case DDS_FORMAT_DXT5:
        return MAKEFOURCC('D', 'X', 'T', '5');
    case DDS_FORMAT_ATI1:
    case DDS_FORMAT_BC4:
        return MAKEFOURCC('A', 'T', 'I', '1');
    case DDS_FORMAT_ATI2:
        return MAKEFOURCC('A', 'T', 'I', '2');
    case DDS_FORMAT_A2XY:
        return MAKEFOURCC('A', '2', 'X', 'Y');
    case DDS_FORMAT_BC7:
    case DDS_FORMAT_DX10:
        return MAKEFOURCC('D', 'X', '1', '0');
    case DDS_FORMAT_BC7L:
        return MAKEFOURCC('B', 'C', '7', 'L');
    case DDS_FORMAT_NVTT:
        return MAKEFOURCC('N', 'V', 'T', 'T');
    case DDS_FORMAT_DDS:
        return MAKEFOURCC('D', 'D', 'S', ' ');
    case DDS_FORMAT_RXGB8:
        return MAKEFOURCC('R', 'X', 'G', 'B');
    case DDS_FORMAT_UVER:
        return MAKEFOURCC('U', 'V', 'E', 'R');
    case DDS_FORMAT_BC6H:
        return MAKEFOURCC('B', 'C', '6', 'H');
    default:
        fprintf(stderr, "WARNING: Unsupported fourCC");
        return 0;
    }
}
