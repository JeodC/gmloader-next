#include <string>
#include <filesystem>
#include <math.h>
#include <stdlib.h>
#include "platform.h"
#include "so_util.h"
#include "io_util.h"
#include "libyoyo.h"
#include "configuration.h"

#define STB_ONLY_PNG
#include "stb_image.h"
#include "io_util.h"
#include "thunks/khronos/glad.h"

namespace fs = std::filesystem;

int image_preload_idx = 0;
int setup_ended = 0;

extern bool override_apk;

typedef struct pvrtc_file {
    uint32_t Version = 0x03525650;
    uint32_t Flags = 0;
    uint64_t Format = 0;
    uint32_t ColourSpace = 0;
    uint32_t ChannelType = 0;
    uint32_t Height = 0;
    uint32_t Width = 0;
    uint32_t Depth = 1;
    uint32_t NumSurfaces = 1;
    uint32_t NumFaces = 1;
    uint32_t MipCount = 1;
    uint32_t MetadataSize = 0;
    uint8_t Data[];
} __attribute__((packed)) pvrtc_file;

// Probe save_dir/textures/ for any .pvr file. The texhack only matters when
// the port externalized textures during patching.
bool has_externalized_pvrs() {
    fs::path dir = fs::path(gmloader_config.save_dir) / "textures";
    if (override_apk) {
        std::string tex_path = gmloader_config.apk_path;
        if (tex_path.rfind("assets/", 0) == 0)
            tex_path = tex_path.substr(7);
        dir /= tex_path;
    }
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return false;
    // Recurse
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) && it->path().extension() == ".pvr")
            return true;
    }
    return false;
}

// Build the path for the externalized PVR for texture index `idx`.
static fs::path ext_pvr_path_for(uint32_t idx) {
    if (override_apk) {
        std::string tex_path = gmloader_config.apk_path;
        if (tex_path.rfind("assets/", 0) == 0)
            tex_path = tex_path.substr(7);
        return fs::path(gmloader_config.save_dir) / "textures" / tex_path / (std::to_string(idx) + ".pvr");
    }
    return fs::path(gmloader_config.save_dir) / "textures" / (std::to_string(idx) + ".pvr");
}

static void upload_external_pvr(uint32_t idx, int *width, int *height) {
    pvrtc_file *ext_data = NULL;
    size_t ext_data_sz = 0;
    fs::path pvr_path = ext_pvr_path_for(idx);
    int ret = io_load_file(pvr_path.c_str(), (void **)&ext_data, &ext_data_sz);
    if (ret != 1 || !ext_data) {
        fatal_error("Failed to load '%s'!\n", pvr_path.c_str());
        exit(1);
    }

    uintptr_t texture_data = ((uintptr_t)&ext_data->Data) + ext_data->MetadataSize;
    size_t texture_data_size = ((uintptr_t)ext_data) + ext_data_sz - texture_data;
    *width = ext_data->Width;
    *height = ext_data->Height;

    switch (ext_data->Format) {
    case 0x00: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG,  *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x01: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG, *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x02: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG,  *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x03: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x04: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG, *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x05: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG, *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x06: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,                   *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x07: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,   *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x09: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,   *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x18: // Older gmloaders wrote 0x18; current is 0x1B. Accept both.
    case 0x1B: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_ASTC_4x4_KHR,    *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x1D: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_ASTC_5x5_KHR,    *width, *height, 0, texture_data_size, (void*)texture_data); break;
    case 0x1F: glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_ASTC_6x6_KHR,    *width, *height, 0, texture_data_size, (void*)texture_data); break;
    default:
        warning("Unknown file format %08lX\n", ext_data->Format);
    }
    GLint err = glGetError();
    if (err != GL_NO_ERROR)
        fatal_error("Failed to upload texture, 0x%04X\n", err);
    free(ext_data);
}

// Write the post-upload texture descriptor fields the GMS runtime expects.
static void finalize_texture_descriptor(uint32_t *flags, uint32_t *texture, int width, int height) {
    *flags |= 0x40;
    texture[0] = 0x06;
    if (flags != &texture[2]) {
        texture[1] = width;
        texture[2] = height;
    } else {
        texture[1] = ((width * *g_TextureScale - 1) | texture[1] & 0xFFFFE000) & 0xFC001FFF | ((height * *g_TextureScale - 1) << 13);
    }
}

void LoadTextureFromPNG_generic(void *arg1, int arg2, uint32_t *flags, uint32_t *tex_id, uint32_t *texture) {
    int width = 0, height = 0;
    uint32_t *data = ReadPNGFile(arg1, arg2, &width, &height, (*flags & 2) == 0);
    if (!data) {
        fatal_error("ERROR: Failed to load a PNG texture!\n");
        return;
    }
    InvalidateTextureState();
    glGenTextures(1, tex_id);
    glBindTexture(GL_TEXTURE_2D, *tex_id);

    if (width == 2 && height == 1 && data[0] == 0xFFBEADDE) {
        uint32_t idx = (data[1] << 8) >> 8;
        upload_external_pvr(idx, &width, &height);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }
    FreePNGFile();
    finalize_texture_descriptor(flags, texture, width, height);
}

void LoadTextureFromQOIF_generic(void *arg1, int arg2, uint32_t *flags, uint32_t *tex_id, uint32_t *texture) {
    if (!ReadQOIFFile || !FreeQOIFFile) {
        fatal_error("ERROR: QOIF texhack invoked but ReadQOIFFile/FreeQOIFFile symbol missing.\n");
        return;
    }
    int width = 0, height = 0;
    uint32_t *data = ReadQOIFFile(arg1, arg2, &width, &height, (*flags & 2) == 0);
    if (!data) {
        fatal_error("ERROR: Failed to load a QOIF texture!\n");
        return;
    }
    InvalidateTextureState();
    glGenTextures(1, tex_id);
    glBindTexture(GL_TEXTURE_2D, *tex_id);

    if (width == 2 && height == 1 && data[0] == 0xFFBEADDE) {
        uint32_t idx = (data[1] << 8) >> 8;
        upload_external_pvr(idx, &width, &height);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }
    FreeQOIFFile((unsigned char*)data);
    finalize_texture_descriptor(flags, texture, width, height);
}

typedef struct png_color
{
   unsigned char red;
   unsigned char green;
   unsigned char blue;
} png_color;

typedef struct png_info
{
   uint32_t width;
   uint32_t height;
   uint32_t valid;

   size_t rowbytes;
   png_color palette;
   uint16_t num_palette;
   uint16_t num_trans;
   unsigned char bit_depth;
   unsigned char color_type;

   unsigned char compression_type;
   unsigned char filter_type;
   unsigned char interlace_type;
   unsigned char channels;

   unsigned char pixel_depth;
   unsigned char spare_byte;

   unsigned char signature[8];
} png_info;

// During PreLoadTexture the runtime asks for each texture's dimensions before
// any pixels are uploaded. When it sees our 2x1 stub, sniff the matching PVR
// and overwrite W/H so GL state gets sized for the real texture.
static void preload_dims_from_pvr(uint32_t *width, uint32_t *height) {
    fs::path path = ext_pvr_path_for(image_preload_idx);
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        fatal_error("Texture %d metadata preload failure.\n", image_preload_idx);
        exit(-1);
    }
    fseek(f, 0x18, SEEK_SET);
    fread(height, 1, 4, f);
    fread(width,  1, 4, f);
    fclose(f);
    image_preload_idx++;
}

uint32_t png_get_IHDR_hook(struct png_struct *png_ptr, png_info *info_ptr, uint32_t *width, uint32_t *height, int *bit_depth, int *color_type, int *interlace_type, int *compression_type, int *filter_type)
{
    if (!png_ptr || !info_ptr || !width || !height)
        return 0;

    *width = info_ptr->width;
    *height = info_ptr->height;
    if (bit_depth)        *bit_depth = info_ptr->bit_depth;
    if (color_type)       *color_type = info_ptr->color_type;
    if (compression_type) *compression_type = info_ptr->compression_type;
    if (filter_type)      *filter_type = info_ptr->filter_type;
    if (interlace_type)   *interlace_type = info_ptr->interlace_type;

    if (!setup_ended && *width == 2 && *height == 1)
        preload_dims_from_pvr(width, height);
    return 1;
}

uint32_t qoif_read_header_hook(void *data, int size, int *width, int *height, char alpha)
{
    if (!data || !width || !height)
        return 0;
    uint8_t *p = (uint8_t *)data;
    *width  = (uint32_t)(p[4] | (p[5] << 8));
    *height = (uint32_t)(p[6] | (p[7] << 8));
    if (!setup_ended && *width == 2 && *height == 1)
        preload_dims_from_pvr((uint32_t*)width, (uint32_t*)height);
    return 1;
}

