/*
  gust_g1t - DDS texture unpacker for Gust (Koei/Tecmo) .g1t files
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "utf8.h"
#include "util.h"
#include "parson.h"
#include "dds.h"

#define JSON_VERSION            1
#define GT1G_MAGIC              0x47315447  // 'G1TG'

// G1T texture flags
#define G1T_FLAG_SRGB           0x02000000  // Not sure if this one is correct...
#define G1T_FLAG_EXTRA_CONTENT  0x10000000

// Known platforms
#define SONY_PS2                0x00
#define SONY_PS3                0x01        // Big Endian => unsupported!
#define MICROSOFT_X360          0x02
#define NINTENDO_WII            0x03
#define NINTENDO_DS             0x04
#define NINTENDO_3DS            0x05
#define SONY_PSV                0x06
#define GOOGLE_ANDROID          0x07
#define APPLE_IOS               0x08
#define NINTENDO_WIIU           0x09
#define MICROSOFT_WINDOWS       0x0A
#define SONY_PS4                0x0B
#define MICROSOFT_XONE          0x0C
#define NINTENDO_SWITCH         0x10

#pragma pack(push, 1)
typedef struct {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    total_size;
    uint32_t    header_size;
    uint32_t    nb_textures;
    uint32_t    platform;       // See the platforms list above
    uint32_t    extra_size;
} g1t_header;

// This is followed by uint32_t extra_flags[nb_textures]

typedef struct {
    uint8_t     zero : 4;       // Always 0x0
    uint8_t     mipmaps : 4;
    uint8_t     type;
    uint8_t     dx : 4;
    uint8_t     dy : 4;
    uint8_t     unused;         // Always 0x00
    uint32_t    flags;          // 0x10211000 or 0x00211000 or 0x12222000 for FT SRGB
} g1t_tex_header;

// May be followed by extra_data[]

#pragma pack(pop)

typedef struct {
    const char* in;
    const char* out;
} swizzle_t;

swizzle_t swizzle_op[] = {
    { NULL, NULL },
    { "ARGB", "ABGR" },
    { "ARGB", "RGBA" },
    { "ARGB", "GRAB" },
};

#define NO_SWIZZLE      0
#define ARGB_TO_ABGR    1
#define ARGB_TO_RGBA    2
#define ARGB_TO_GRAB    3

static size_t write_dds_header(FILE* fd, int format, uint32_t width, uint32_t height,
                               uint32_t bpp, uint32_t mipmaps, uint32_t flags)
{
    if ((fd == NULL) || (width == 0) || (height == 0))
        return 0;

    DDS_HEADER header = { 0 };
    header.size = 124;
    header.flags = DDS_HEADER_FLAGS_TEXTURE;
    header.height = height;
    header.width = width;
    header.ddspf.size = 32;
    if (format == DDS_FORMAT_BGR) {
        header.ddspf.flags = DDS_RGB;
        header.ddspf.RGBBitCount = bpp;
        switch (bpp) {
        case 24:
            header.ddspf.RBitMask = 0x00ff0000;
            header.ddspf.GBitMask = 0x0000ff00;
            header.ddspf.BBitMask = 0x000000ff;
            break;
        default:
            fprintf(stderr, "ERROR: Unsupported bits-per-pixel value %d\n", bpp);
            return 0;
        }
    } else if (format >= DDS_FORMAT_ABGR && format <= DDS_FORMAT_RGBA) {
        header.ddspf.flags = DDS_RGBA;
        header.ddspf.RGBBitCount = bpp;
        // Always save as ARGB, to keep VS and PhotoShop happy
        switch (bpp) {
        case 32:
            header.ddspf.RBitMask = 0x00ff0000;
            header.ddspf.GBitMask = 0x0000ff00;
            header.ddspf.BBitMask = 0x000000ff;
            header.ddspf.ABitMask = 0xff000000;
            break;
        // I have absolutely no idea if the following will work...
        case 64:
            header.ddspf.RBitMask = 0x0000ffff;
            header.ddspf.GBitMask = 0xffff0000;
            header.ddspf.BBitMask = 0x0000ffff;
            header.ddspf.ABitMask = 0xffff0000;
            break;
        case 128:
            header.ddspf.RBitMask = 0xffffffff;
            header.ddspf.GBitMask = 0xffffffff;
            header.ddspf.BBitMask = 0xffffffff;
            header.ddspf.ABitMask = 0xffffffff;
            break;
        default:
            fprintf(stderr, "ERROR: Unsupported bits-per-pixel value %d\n", bpp);
            return 0;
        }
    } else if (format == DDS_FORMAT_R) {
        header.ddspf.flags = DDS_RGBA;
        header.ddspf.RGBBitCount = bpp;
        header.ddspf.RBitMask = (uint32_t)((1ULL << bpp) - 1);
    } else {
        header.ddspf.flags = DDS_FOURCC;
        header.ddspf.fourCC = get_fourCC(format);
    }
    header.caps = DDS_SURFACE_FLAGS_TEXTURE;
    if (mipmaps != 0) {
        header.mipMapCount = mipmaps;
        header.flags |= DDS_HEADER_FLAGS_MIPMAP;
        header.caps |= DDS_SURFACE_FLAGS_MIPMAP;
    }
    size_t r = fwrite(&header, sizeof(DDS_HEADER), 1, fd);
    if (r != 1)
        return r;
    if (format == DDS_FORMAT_BC7) {
        DDS_HEADER_DXT10 dxt10_hdr = { 0 };
        dxt10_hdr.dxgiFormat = (flags & G1T_FLAG_SRGB) ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
        dxt10_hdr.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
        dxt10_hdr.miscFlags2 = DDS_ALPHA_MODE_STRAIGHT;
        dxt10_hdr.arraySize = 1; // Must be set to 1 for 3D texture
        r = fwrite(&dxt10_hdr, sizeof(DDS_HEADER_DXT10), 1, fd);
    }
    return r;
}

static void swizzle(const uint32_t bits_per_pixel, const char* in,
                    const char* out, uint8_t* buf, const uint32_t size)
{
    assert(bits_per_pixel % 8 == 0);

    const int rgba[4] = { 'R', 'G', 'B', 'A' };
    if (strcmp(in, out) == 0)
        return;

    uint32_t mask[4];
    int rot[4];
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t pos_in = 3 - (uint32_t)((uintptr_t)strchr(in, rgba[i]) - (uintptr_t)in);
        uint32_t pos_out = 3 - (uint32_t)((uintptr_t)strchr(out, rgba[i]) - (uintptr_t)out);
        mask[i] = ((1 << bits_per_pixel / 4) - 1) << (pos_in * 8);
        rot[i] = (pos_out - pos_in) * 8;
    }

    for (uint32_t j = 0; j < size; j += 4) {
        uint32_t s;
        switch (bits_per_pixel) {
        case 16: s = getbe16(&buf[j]); break;
        case 24: s = getbe24(&buf[j]); break;
        default: s = getbe32(&buf[j]); break;
        }
        uint32_t d = 0;
        for (uint32_t i = 0; i < 4; i++)
            d |= (rot[i] > 0) ? ((s & mask[i]) << rot[i]) : ((s & mask[i]) >> -rot[i]);
        switch (bits_per_pixel) {
        case 16: setbe16(&buf[j], (uint16_t)d); break;
        case 24: setbe24(&buf[j], d); break;
        default: setbe32(&buf[j], d); break;
        }
    }
}

// "Inflate" a 32 bit value by interleaving 0 bits at odd positions.
static __inline uint32_t inflate_bits(uint32_t x)
{
    x &= 0x0000FFFF;
    x = (x | (x << 8))  & 0x00FF00FF;
    x = (x | (x << 4))  & 0x0F0F0F0F;
    x = (x | (x << 2))  & 0x33333333;
    x = (x | (x << 1))  & 0x55555555;
    return x;
}

// "Deflate" a 32-bit value by deinterleaving all odd bits.
static __inline uint32_t deflate_bits(uint32_t x)
{
    x &= 0x55555555;
    x = (x | (x >> 1))  & 0x33333333;
    x = (x | (x >> 2))  & 0x0F0F0F0F;
    x = (x | (x >> 4))  & 0x00FF00FF;
    x = (x | (x >> 8))  & 0x0000FFFF;
    return x;
}

// From two 32-bit (x,y) coordinates, compute the 64-bit Morton (or Z-order) value.
// If invert is true, x and y are inverted.
static __inline uint32_t xy_to_morton(uint32_t x, uint32_t y, bool invert)
{
    return (inflate_bits(x) << (invert ? 1 : 0)) |
           (inflate_bits(y) << (invert ? 0 : 1));
}

// Fom a 32-bit Morton (Z-order) value, recover two 32-bit (x,y) coordinates.
// If invert is true, x and y are inverted.
static __inline void morton_to_xy(uint32_t z, uint32_t* x, uint32_t* y, bool invert)
{
    *x = deflate_bits(z >> (invert ? 1 : 0));
    *y = deflate_bits(z >> (invert ? 0 : 1));
}

// Apply (reverse = false) or remove (reverse = true) a Morton transformation
// a.k.a. a Z-order curve to a texture.
// The Morton order can be negative, in which case the effective Morton order
// is the absolute value and the odd/even bit positions for x and y are inverted.
static void mortonizer(const uint32_t bits_per_element, const int16_t morton_order,
                       const uint32_t width, const uint32_t height,
                       uint8_t* buf, const uint32_t size, bool reverse)
{
    const uint32_t bytes_per_element = bits_per_element / 8;
    uint32_t nblocks = size / bytes_per_element;
    uint16_t k = (int16_t)abs(morton_order);
    bool invert = (morton_order != (int16_t)k);

    // Only deal with elements that are multiple of one byte in size
    assert(bits_per_element % 8 == 0);
    // Validate that the size of the buffer matches the dimensions provided
    assert(bytes_per_element * width * height == size);
    // Only deal with texture that are smaller than 64k*64k
    assert((width < 0x10000) && (height < 0x10000));
    // Ensure that we won't produce x or y that are larger than width or height
    assert(k <= log2(min(width, height)));
    // Help masks to isolate the mortonized and unmortonized part of our source indices
    uint32_t low_mask = (1 << (2 * k)) - 1;
    uint32_t high_mask = ~low_mask;
    uint8_t* tmp_buf = (uint8_t*)malloc(size);
    for (uint32_t i = 0; i < nblocks; i++) {
        uint32_t x, y;  // Coordinates in each 2^k by 2^k square
        // Note that we don't need to mask x or y with (2^k - 1)
        // since we apply low_mask to the Morton value.
        uint32_t j = i & high_mask; // Destination index, high part
        if (!reverse) { // Morton value from an (x,y) pair
            x = i; y = i >> k;
            j |= xy_to_morton(x, y, invert) & low_mask;
        } else {        // (x,y) pair from a Morton value
            morton_to_xy(i & low_mask, &x, &y, invert);
            j |= (y << k) | x;
        }
        memcpy(&tmp_buf[j * bytes_per_element], &buf[i * bytes_per_element], bytes_per_element);
    }
    memcpy(buf, tmp_buf, size);
    free(tmp_buf);
}

#define mortonize(bpe, k, w, h, buf, size) mortonizer(bpe, k, w, h, buf, size, false)
#define unmortonize(bpe, k, w, h, buf, size) mortonizer(bpe, k, w, h, buf, size, true)

// An element can be a pixel (for uncompressed textures) or a group of pixels (for DXT1, DXT5...)
// A positive tile_size performs tiling. A negative tile_size performs untiling.
static void tiler(const uint32_t bits_per_element, uint32_t tile_size, uint32_t width,
                 uint8_t* buf, const uint32_t size, bool untile)
{
    assert(bits_per_element % 8 == 0);
    const uint32_t bytes_per_element = bits_per_element / 8;
    assert((bytes_per_element >= 1) && (bytes_per_element <= 8));
    assert(size % bytes_per_element == 0);
    assert(size % (tile_size * tile_size) == 0);

    uint8_t* tmp_buf = (uint8_t*)malloc(size);

    for (uint32_t i = 0; i < size / bytes_per_element / tile_size / tile_size; i++) {
        uint32_t tile_row = i / (width / tile_size);
        uint32_t tile_column = i % (width / tile_size);
        uint32_t tile_start = tile_row * width * tile_size + tile_column * tile_size;
        for (uint32_t j = 0; j < tile_size; j++) {
            uint32_t addr[2];
            addr[0] = bytes_per_element * (tile_start + j * width);
            addr[1] = bytes_per_element * (i * tile_size * tile_size + j * tile_size);
            memcpy(&tmp_buf[addr[untile ? 0 : 1]], &buf[addr[untile ? 1 : 0]],
                   (size_t)tile_size * bytes_per_element);
        }
    }

    memcpy(buf, tmp_buf, size);
    free(tmp_buf);
}

#define tile(bpe, ts, w, buf, size) tiler(bpe, ts, w, buf, size, false)
#define untile(bpe, ts, w, buf, size) tiler(bpe, ts, w, buf, size, true)

static void flip(uint32_t bits_per_pixel, uint8_t* buf, const uint32_t size, uint32_t width)
{
    assert(bits_per_pixel % 8 == 0);
    const uint32_t line_size = width * (bits_per_pixel / 8);
    assert(size % line_size == 0);
    const uint32_t max_line = (size / line_size) - 1;

    uint8_t* tmp_buf = (uint8_t*)malloc(size);

    for (uint32_t i = 0; i <= max_line; i++)
        memcpy(&tmp_buf[i * line_size], &buf[(max_line - i) * line_size], line_size);

    memcpy(buf, tmp_buf, size);
    free(tmp_buf);
}

int main_utf8(int argc, char** argv)
{
    int r = -1;
    FILE *file = NULL;
    uint8_t* buf = NULL;
    uint32_t* offset_table = NULL;
    uint32_t magic;
    char path[256], *dir = NULL;
    JSON_Value* json = NULL;
    bool list_only = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'l');
    bool flip_image = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'f');

    if ((argc != 2) && !list_only && !flip_image) {
        printf("%s %s (c) 2019-2021 VitaSmith\n\n"
            "Usage: %s [-l] [-f] <file or directory>\n\n"
            "Extracts (file) or recreates (directory) a Gust .g1t texture archive.\n\n"
            "Note: A backup (.bak) of the original is automatically created, when the target\n"
            "is being overwritten for the first time.\n",
            appname(argv[0]), GUST_TOOLS_VERSION_STR, appname(argv[0]));
        return 0;
    }

    if (is_directory(argv[argc - 1])) {
        if (list_only) {
            fprintf(stderr, "ERROR: Option -l is not supported when creating an archive\n");
            goto out;
        }
        snprintf(path, sizeof(path), "%s%cg1t.json", argv[argc - 1], PATH_SEP);
        if (!is_file(path)) {
            fprintf(stderr, "ERROR: '%s' does not exist\n", path);
            goto out;
        }
        json = json_parse_file_with_comments(path);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
            goto out;
        }
        const uint32_t json_version = json_object_get_uint32(json_object(json), "json_version");
        if (json_version != JSON_VERSION) {
            fprintf(stderr, "ERROR: This utility is not compatible with the JSON file provided.\n"
                "You need to (re)extract the '.g1t' using this application.\n");
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        const char* version = json_object_get_string(json_object(json), "version");
        if ((filename == NULL) || (version == NULL))
            goto out;
        if (!flip_image)
            flip_image = json_object_get_boolean(json_object(json), "flip");
        strcpy(path, argv[argc - 1]);
        if (get_trailing_slash(path) != 0)
            path[get_trailing_slash(path)] = 0;
        else
            path[0] = 0;
        strcat(path, filename);
        path[sizeof(path) - 1] = 0;
        printf("Creating '%s'...\n", path);
        create_backup(path);
        file = fopen_utf8(path, "wb+");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
            goto out;
        }
        g1t_header hdr = { 0 };
        hdr.magic = GT1G_MAGIC;
        hdr.version = getbe32(version);
        hdr.total_size = 0;  // To be rewritten when we're done
        hdr.nb_textures = json_object_get_uint32(json_object(json), "nb_textures");
        hdr.platform = json_object_get_uint32(json_object(json), "platform");
        hdr.extra_size = json_object_get_uint32(json_object(json), "extra_size");
        hdr.header_size = sizeof(hdr) + hdr.nb_textures * sizeof(uint32_t);
        if (fwrite(&hdr, sizeof(hdr), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write header\n");
            goto out;
        }

        JSON_Array* extra_flags_array = json_object_get_array(json_object(json), "extra_flags");
        if (json_array_get_count(extra_flags_array) != hdr.nb_textures) {
            fprintf(stderr, "ERROR: number of extra flags doesn't match number of textures\n");
            goto out;
        }
        for (uint32_t i = 0; i < hdr.nb_textures; i++) {
            uint32_t extra_flag = (uint32_t)json_array_get_number(extra_flags_array, i);
            if (fwrite(&extra_flag, sizeof(uint32_t), 1, file) != 1) {
                fprintf(stderr, "ERROR: Can't write extra flags\n");
                goto out;
            }
        }

        offset_table = calloc(hdr.nb_textures, sizeof(uint32_t));
        offset_table[0] = hdr.nb_textures * sizeof(uint32_t);
        if (fwrite(offset_table, hdr.nb_textures * sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write texture offsets\n");
            goto out;
        }

        JSON_Array* textures_array = json_object_get_array(json_object(json), "textures");
        if (json_array_get_count(textures_array) != hdr.nb_textures) {
            fprintf(stderr, "ERROR: number of textures in array doesn't match\n");
            goto out;
        }

        printf("TYPE OFFSET     SIZE       NAME");
        for (size_t i = 0; i < strlen(basename(argv[argc - 1])); i++)
            putchar(' ');
        printf("     DIMENSIONS MIPMAPS SUPPORTED?\n");
        for (uint32_t i = 0; i < hdr.nb_textures; i++) {
            offset_table[i] = ftell(file) - hdr.header_size;
            JSON_Object* texture_entry = json_array_get_object(textures_array, i);
            g1t_tex_header tex = { 0 };
            tex.type = json_object_get_uint8(texture_entry, "type");
            tex.flags = json_object_get_uint32(texture_entry, "flags");
            // Read the DDS file
            snprintf(path, sizeof(path), "%s%c%s", basename(argv[argc - 1]), PATH_SEP,
                json_object_get_string(texture_entry, "name"));
            uint32_t dds_size = read_file(path, &buf);
            if (dds_size <= sizeof(DDS_HEADER)) {
                fprintf(stderr, "ERROR: '%s' is too small\n", path);
                goto out;
            }
            if (*((uint32_t*)buf) != DDS_MAGIC) {
                fprintf(stderr, "ERROR: '%s' is not a DDS file\n", path);
                goto out;
            }
            DDS_HEADER* dds_header = (DDS_HEADER*)&buf[sizeof(uint32_t)];
            dds_size -= sizeof(uint32_t) + sizeof(DDS_HEADER);
            uint8_t* dds_payload = (uint8_t*)&buf[sizeof(uint32_t) + sizeof(DDS_HEADER)];
            // We may have a DXT10 additional header
            if (dds_header->ddspf.fourCC == get_fourCC(DDS_FORMAT_DX10)) {
                dds_size -= sizeof(DDS_HEADER_DXT10);
                dds_payload = &dds_payload[sizeof(DDS_HEADER_DXT10)];
            }
            tex.mipmaps = (uint8_t)dds_header->mipMapCount;
            // Are both width and height a power of two?
            bool po2_sizes = is_power_of_2(dds_header->width) && is_power_of_2(dds_header->height);
            if (po2_sizes) {
                tex.dx = (uint8_t)find_msb(dds_header->width);
                tex.dy = (uint8_t)find_msb(dds_header->height);
            }
            // Write texture header
            if (fwrite(&tex, sizeof(tex), 1, file) != 1) {
                fprintf(stderr, "ERROR: Can't write texture header\n");
                goto out;
            }
            // Write extra data
            if (tex.flags & G1T_FLAG_EXTRA_CONTENT) {
                JSON_Array* extra_data_array = json_object_get_array(texture_entry, "extra_data");
                uint32_t extra_data_size = (uint32_t)(json_array_get_count(extra_data_array) + 1) * sizeof(uint32_t);
                if (!po2_sizes && extra_data_size < 4 * sizeof(uint32_t)) {
                    fprintf(stderr, "ERROR: Non power-of-two width or height is missing from extra data\n");
                    goto out;
                }
                if (fwrite(&extra_data_size, sizeof(uint32_t), 1, file) != 1) {
                    fprintf(stderr, "ERROR: Can't write extra data size\n");
                    goto out;
                }
                for (size_t j = 0; j < json_array_get_count(extra_data_array); j++) {
                    uint32_t extra_data = (uint32_t)json_array_get_number(extra_data_array, j);
                    if ((j == 2) && (!po2_sizes) && (extra_data != dds_header->width)) {
                        fprintf(stderr, "ERROR: DDS width and extra data width don't match\n");
                        goto out;
                    }
                    if ((j == 3) && (!po2_sizes) && (extra_data != dds_header->height)) {
                        fprintf(stderr, "ERROR: DDS height and extra data height don't match\n");
                        goto out;
                    }
                    if (fwrite(&extra_data, sizeof(uint32_t), 1, file) != 1) {
                        fprintf(stderr, "ERROR: Can't write extra data\n");
                        goto out;
                    }
                }
            }

            // Set the default swizzle for the platform
            uint32_t platform_sw;
            switch (hdr.platform) {
            case NINTENDO_DS:
            case NINTENDO_3DS:
            case SONY_PS4:
                platform_sw = ARGB_TO_GRAB;
                break;
            case SONY_PSV:
            case NINTENDO_SWITCH:
                platform_sw = NO_SWIZZLE;    // Already ARGB
                break;
            default:    // PC and other platforms
                platform_sw = ARGB_TO_RGBA;
                break;
            }
            // See other similar section below for more details on these
            uint32_t bpp = 0, sw = NO_SWIZZLE, wf = 1, hf = 1, ts = 0;
            int16_t mo = 0;
            bool supported = true;
            switch (tex.type) {
            case 0x00: bpp = 32; sw = platform_sw; break;
            case 0x01: bpp = 32; sw = platform_sw; break;
            case 0x02: bpp = 32; sw = platform_sw; break;
            case 0x03: bpp = 64; sw = platform_sw; supported = false; break;    // UNSUPPORTED!!
            case 0x04: bpp = 128; sw = platform_sw; supported = false; break;   // UNSUPPORTED!!
            case 0x06: bpp = 4; break;
            case 0x07: bpp = 8; supported = false; break;                       // UNSUPPORTED!!
            case 0x08: bpp = 8; break;
            case 0x09: bpp = 32; sw = platform_sw; mo = 3; ts = 8; break;
            case 0x0A: bpp = 32; sw = platform_sw; mo = 3; ts = 8; supported = false; break; // UNSUPPORTED!!
            case 0x10: bpp = 4; bpp = 4; mo = -8; wf = 4; hf = 4; break;
            case 0x12: bpp = 8; supported = false; break;                       // UNSUPPORTED!!
            case 0x21: bpp = 32; break;
            case 0x3C: bpp = 16; supported = false; break;                      // UNSUPPORTED!!
            case 0x3D: bpp = 16; supported = false; break;                      // UNSUPPORTED!!
            case 0x45: bpp = 24; mo = 3; ts = 8; break;
            case 0x59: bpp = 4; break;
            case 0x5B: bpp = 8; break;
            case 0x5C: bpp = 4; break;
            case 0x5F: bpp = 8; break;
            case 0x60: bpp = 4; supported = false; break;                       // UNSUPPORTED!!
            case 0x62: bpp = 8; mo = 3; ts = 8; wf = 8; hf = 4; break;
            default:
                fprintf(stderr, "ERROR: Unhandled texture type 0x%02x\n", tex.type);
                goto out;
            }

            if ((dds_size * 8) % bpp != 0) {
                fprintf(stderr, "ERROR: Texture size should be a multiple of %d bits\n", bpp);
                goto out;
            }

            switch (dds_header->ddspf.flags) {
            case DDS_RGBA:
                if ((dds_header->ddspf.RGBBitCount != 32) && (dds_header->ddspf.RGBBitCount != 64) &&
                    (dds_header->ddspf.RGBBitCount != 128)) {
                    fprintf(stderr, "ERROR: '%s' is not an ARGB texture we support\n", path);
                    goto out;
                }
                break;
            case DDS_RGB:
                if ((dds_header->ddspf.RGBBitCount != 24) ||
                    (dds_header->ddspf.RBitMask != 0x00ff0000) || (dds_header->ddspf.GBitMask != 0x0000ff00) ||
                    (dds_header->ddspf.BBitMask != 0x000000ff) || (dds_header->ddspf.ABitMask != 0x00000000)) {
                    fprintf(stderr, "ERROR: '%s' is not an RGB texture we support\n", path);
                    goto out;
                }
            case DDS_FOURCC:
                break;
            default:
                fprintf(stderr, "ERROR: '%s' is not a texture we support\n", path);
                goto out;
            }

            if (flip_image || ((hdr.platform == NINTENDO_3DS) && (tex.type == 0x45)))
                flip(bpp, dds_payload, dds_size, dds_header->width);
            if (ts != 0)
                tile(bpp * hf, ts, dds_header->width, dds_payload, dds_size);
            if (mo != 0)
                mortonize(bpp * wf * hf, mo, dds_header->width / wf,
                          dds_header->height / hf, dds_payload, dds_size);
            if (sw != NO_SWIZZLE)
                swizzle(bpp, swizzle_op[sw].in, swizzle_op[sw].out, dds_payload, dds_size);

            // Write texture
            if (fwrite(dds_payload, 1, dds_size, file) != dds_size) {
                fprintf(stderr, "ERROR: Can't write texture data\n");
                goto out;
            }
            char dims[16];
            snprintf(dims, sizeof(dims), "%dx%d", dds_header->width, dds_header->height);
            printf("0x%02x 0x%08x 0x%08x %s %-10s %-7d %s\n", tex.type, hdr.header_size + offset_table[i],
                (uint32_t)ftell(file) - offset_table[i] - hdr.header_size, path,
                dims, dds_header->mipMapCount, supported ? "Y" : "N");
            free(buf);
            buf = NULL;
        }
        // Update total size
        uint32_t total_size = ftell(file);
        fseek(file, 2 * sizeof(uint32_t), SEEK_SET);
        if (fwrite(&total_size, sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't update total size\n");
            goto out;
        }
        // Update offset table
        fseek(file, sizeof(hdr) + hdr.nb_textures * sizeof(uint32_t), SEEK_SET);
        if (fwrite(offset_table, sizeof(uint32_t), hdr.nb_textures, file) != hdr.nb_textures) {
            fprintf(stderr, "ERROR: Can't update texture offsets\n");
            goto out;
        }
        r = 0;
    } else {
        printf("%s '%s'...\n", list_only ? "Listing" : "Extracting", argv[argc - 1]);
        size_t len = strlen(argv[argc - 1]);
        if ((len < 4) || (argv[argc - 1][len - 4] != '.') || (argv[argc - 1][len - 3] != 'g') ||
            ((argv[argc - 1][len - 2] != '1') && (argv[argc - 1][len - 2] != 't')) ||
            ((argv[argc - 1][len - 1] != '1') && (argv[argc - 1][len - 1] != 't')) ) {
            fprintf(stderr, "ERROR: File should have a '.g1t' or 'gt1' extension\n");
            goto out;
        }
        char* g1t_pos = &argv[argc - 1][len - 4];
        file = fopen_utf8(argv[argc - 1], "rb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't open file '%s'", argv[argc - 1]);
            goto out;
        }

        if (fread(&magic, sizeof(magic), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't read from '%s'", argv[argc - 1]);
            goto out;
        }
        if (magic != GT1G_MAGIC) {
            fprintf(stderr, "ERROR: Not a G1T file (bad magic) or unsupported platform");
            goto out;
        }
        fseek(file, 0L, SEEK_END);
        uint32_t g1t_size = (uint32_t)ftell(file);
        fseek(file, 0L, SEEK_SET);

        buf = malloc(g1t_size);
        if (buf == NULL)
            goto out;
        if (fread(buf, 1, g1t_size, file) != g1t_size) {
            fprintf(stderr, "ERROR: Can't read file");
            goto out;
        }

        g1t_header* hdr = (g1t_header*)buf;
        if (hdr->total_size != g1t_size) {
            fprintf(stderr, "ERROR: File size mismatch\n");
            goto out;
        }
        char version[5];
        setbe32(version, hdr->version);
        version[4] = 0;
        if (hdr->version >> 16 != 0x3030)
            fprintf(stderr, "WARNING: Potentially unsupported G1T version %s\n", version);
        if (hdr->extra_size != 0) {
            fprintf(stderr, "ERROR: Can't handle G1T files with extra content\n");
            goto out;
        }

        uint32_t* x_offset_table = (uint32_t*)&buf[hdr->header_size];

        // Keep the information required to recreate the archive in a JSON file
        json = json_value_init_object();
        json_object_set_number(json_object(json), "json_version", JSON_VERSION);
        json_object_set_string(json_object(json), "name", basename(argv[argc - 1]));
        json_object_set_string(json_object(json), "version", version);
        json_object_set_number(json_object(json), "nb_textures", hdr->nb_textures);
        json_object_set_number(json_object(json), "platform", hdr->platform);
        json_object_set_number(json_object(json), "extra_size", hdr->extra_size);
        json_object_set_boolean(json_object(json), "flip", flip_image);

        g1t_pos[0] = 0;
        if (!list_only && !create_path(argv[argc - 1]))
            goto out;

        JSON_Value* json_flags_array = json_value_init_array();
        JSON_Value* json_textures_array = json_value_init_array();

        printf("TYPE OFFSET     SIZE       NAME");
        for (size_t i = 0; i < strlen(basename(argv[argc - 1])); i++)
            putchar(' ');
        printf("     DIMENSIONS MIPMAPS SUPPORTED?\n");
        dir = strdup(argv[argc - 1]);
        if (dir == NULL) {
            fprintf(stderr, "ERROR: Alloc error\n");
            goto out;
        }
        dir[get_trailing_slash(dir)] = 0;

        // Set the default RGBA texture format for the platform
        uint32_t default_texture_format;
        switch (hdr->platform) {
        case NINTENDO_DS:
        case NINTENDO_3DS:
        case SONY_PS4:
            default_texture_format = DDS_FORMAT_GRAB;
            break;
        case SONY_PSV:
        case NINTENDO_SWITCH:
            default_texture_format = DDS_FORMAT_ARGB;
            break;
        default:    // PC and other platforms
            default_texture_format = DDS_FORMAT_RGBA;
            break;
        }

        for (uint32_t i = 0; i < hdr->nb_textures; i++) {
            // There's an array of flags after the hdr
            json_array_append_number(json_array(json_flags_array), getle32(&buf[(uint32_t)sizeof(g1t_header) + 4 * i]));
            uint32_t pos = hdr->header_size + x_offset_table[i];
            g1t_tex_header* tex = (g1t_tex_header*)&buf[pos];
            pos += sizeof(g1t_tex_header);
            uint32_t width = 1 << tex->dx;
            uint32_t height = 1 << tex->dy;
            uint32_t extra_size = (tex->flags & G1T_FLAG_EXTRA_CONTENT) ? getle32(&buf[pos]) : 0;
            // Non power-of-two width and height may be provided in the extra data
            if (extra_size >= 0x14) {
                if (width == 1)
                    width = getle32(&buf[pos + 0x0c]);
                if (height == 1)
                    height = getle32(&buf[pos + 0x10]);
            }

            JSON_Value* json_texture = json_value_init_object();
            snprintf(path, sizeof(path), "%03d.dds", i);
            json_object_set_string(json_object(json_texture), "name", path);
            json_object_set_number(json_object(json_texture), "type", tex->type);
            json_object_set_number(json_object(json_texture), "flags", tex->flags);
            uint32_t texture_format = default_texture_format;
            int16_t mo = 0;     // Morton order
            uint32_t ts = 0;    // Tile size
            uint32_t bpp = 0;   // Bits per pixel
            uint32_t wf = 1;    // Width factor (for compressed textures)
            uint32_t hf = 1;    // Height factor (for compressed textures)
            bool supported = true;
            switch (tex->type) {
            case 0x00: bpp = 32; break;
            case 0x01: bpp = 32; break;
            case 0x02: bpp = 32; break;
            case 0x03: bpp = 64; supported = false; break;                                  // UNSUPPORTED!!
            case 0x04: bpp = 128; supported = false; break;                                 // UNSUPPORTED!!
            case 0x06: texture_format = DDS_FORMAT_DXT1; bpp = 4; break;
            case 0x07: texture_format = DDS_FORMAT_DXT3; bpp = 8; supported = false; break; // UNSUPPORTED!!
            case 0x08: texture_format = DDS_FORMAT_DXT5; bpp = 8; break;
            case 0x09: bpp = 32; mo = 3; ts = 8; break;
            case 0x0A: bpp = 32; mo = 3; ts = 8; supported = false; break;                  // UNSUPPORTED!!
            case 0x10: texture_format = DDS_FORMAT_DXT1; bpp = 4; mo = -8; wf = 4; hf = 4; break;
            case 0x12: texture_format = DDS_FORMAT_DXT5; bpp = 8; supported = false; break; // UNSUPPORTED!!
            case 0x21: bpp = 32; break;
            case 0x3C: texture_format = DDS_FORMAT_DXT1; bpp = 16; supported = false; break; // UNSUPPORTED!!
            case 0x3D: texture_format = DDS_FORMAT_DXT1; bpp = 16; supported = false; break; // UNSUPPORTED!!
            case 0x45: texture_format = DDS_FORMAT_BGR; bpp = 24; mo = 3; ts = 8; break;
            case 0x59: texture_format = DDS_FORMAT_DXT1; bpp = 4; break;
            case 0x5B: texture_format = DDS_FORMAT_DXT5; bpp = 8; break;
            case 0x5C: texture_format = DDS_FORMAT_BC4; bpp = 4; break;
//            case 0x5D: texture_format = DDS_FORMAT_BC5; bits_per_pixel = ?; break;
//            case 0x5E: texture_format = DDS_FORMAT_BC6; bits_per_pixel = ?; break;
            case 0x5F: texture_format = DDS_FORMAT_BC7; bpp = 8; break;
            case 0x60: texture_format = DDS_FORMAT_DXT1; bpp = 4; supported = false; break;  // UNSUPPORTED!!
            case 0x62: texture_format = DDS_FORMAT_DXT5; bpp = 8; mo = 3; ts = 8; wf = 8; hf = 4; break;
            default:
                fprintf(stderr, "ERROR: Unsupported texture type (0x%02X)\n", tex->type);
                continue;
            }
            if (mo != 0 && tex->mipmaps != 1) {
                fprintf(stderr, "ERROR: Swizzled textures with multiple mipmaps are not supported\n");
                continue;
            }
            uint32_t highest_mipmap_size = (width * height * bpp) / 8;
            uint32_t texture_size = highest_mipmap_size;
            for (int j = 0; j < tex->mipmaps - 1; j++)
                texture_size += highest_mipmap_size / (4 << (j * 2));
            uint32_t expected_size = ((i + 1 == hdr->nb_textures) ? g1t_size :
                x_offset_table[i + 1]) - x_offset_table[i];
            assert(expected_size >= texture_size + (uint32_t)sizeof(g1t_tex_header));
            if (texture_size + (uint32_t)sizeof(g1t_tex_header) > expected_size) {
                fprintf(stderr, "ERROR: Computed texture size is larger than actual size\n");
                continue;
            }
            snprintf(path, sizeof(path), "%s%s%c%03d.dds", dir, basename(argv[argc - 1]), PATH_SEP, i);
            char dims[16];
            snprintf(dims, sizeof(dims), "%dx%d", width, height);
            printf("0x%02x 0x%08x 0x%08x %s %-10s %-7d %s\n", tex->type, hdr->header_size + x_offset_table[i],
                expected_size, &path[strlen(dir)], dims, tex->mipmaps, supported ? "Y" : "N");
            if (list_only)
                continue;
            FILE* dst = fopen_utf8(path, "wb");
            if (dst == NULL) {
                fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
                continue;
            }
            uint32_t dds_magic = DDS_MAGIC;
            if (fwrite(&dds_magic, sizeof(dds_magic), 1, dst) != 1) {
                fprintf(stderr, "ERROR: Can't write magic\n");
                fclose(dst);
                continue;
            }
            if (write_dds_header(dst, texture_format, width, height, bpp,
                                 tex->mipmaps, tex->flags) != 1) {
                fprintf(stderr, "ERROR: Can't write DDS header\n");
                fclose(dst);
                continue;
            }
            if (tex->flags & G1T_FLAG_EXTRA_CONTENT) {
                assert(pos + extra_size < g1t_size);
                if ((extra_size < 8) || (extra_size % 4 != 0)) {
                    fprintf(stderr, "ERROR: Can't handle extra_data of size 0x%08x\n", extra_size);
                } else {
                    JSON_Value* json_extra_array_val = json_value_init_array();
                    JSON_Array* json_extra_array_obj = json_array(json_extra_array_val);
                    for (uint32_t j = 4; j < extra_size; j += 4)
                        json_array_append_number(json_extra_array_obj, getle32(&buf[pos + j]));
                    json_object_set_value(json_object(json_texture), "extra_data", json_extra_array_val);
                }
                pos += extra_size;
            }
            // Non ARGB textures require swizzling to be applied, since
            // tools like Visual Studio or PhotoShop can't be bothered
            // to honour the swizzling from the DDS header and instead
            // insist on using ARGB always...
            switch (texture_format) {
            case DDS_FORMAT_RGBA:
                swizzle(bpp, "RGBA", "ARGB", &buf[pos], texture_size);
                break;
            case DDS_FORMAT_ABGR:
                swizzle(bpp, "ABGR", "ARGB", &buf[pos], texture_size);
                break;
            case DDS_FORMAT_GRAB:
                swizzle(bpp, "GRAB", "ARGB", &buf[pos], texture_size);
                break;
            default:
                break;
            }
            if (mo != 0)
                unmortonize(bpp * wf * hf, mo, width / wf, height / hf, &buf[pos], texture_size);
            if (ts != 0)
                untile(bpp * hf, ts, width, &buf[pos], texture_size);
            if (flip_image || ((hdr->platform == NINTENDO_3DS) && (tex->type == 0x45)))
                flip(bpp, &buf[pos], texture_size, width);
            if (fwrite(&buf[pos], texture_size, 1, dst) != 1) {
                fprintf(stderr, "ERROR: Can't write DDS data\n");
                fclose(dst);
                continue;
            }
            fclose(dst);
            json_array_append_value(json_array(json_textures_array), json_texture);
        }

        json_object_set_value(json_object(json), "extra_flags", json_flags_array);
        json_object_set_value(json_object(json), "textures", json_textures_array);
        snprintf(path, sizeof(path), "%s%cg1t.json", argv[argc - 1], PATH_SEP);
        if (!list_only)
            json_serialize_to_file_pretty(json, path);

        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(dir);
    free(offset_table);
    if (file != NULL)
        fclose(file);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

    return r;
}

CALL_MAIN
