
#ifdef __aarch64__
#include <string>
#include <filesystem>
#include <math.h>
#include <stdlib.h>
#include "so_util.h"
#include "platform.h"
#include "libyoyo.h"
#include "configuration.h"
#include "texture.h"

ABI_ATTR void LoadTextureFromPNG_1(uintptr_t texture, int has_mips)
{
    void *arg1 = *(void**)(texture + 0x70);       /* Likely PNG payload address */
    int arg2 = *(int*)(texture + 0x78);           /* Likely PNG payload size */
    uint32_t *arg3 = (uint32_t*)(texture + 0x14); /* Address to Flags */
    uint32_t *arg4 = (uint32_t*)(texture + 0x18); /* Address to OpenGL Texture Id */

    LoadTextureFromPNG_generic(arg1, arg2, arg3, arg4, (uint32_t*)texture);
}

ABI_ATTR void LoadTextureFromPNG_2(uintptr_t texture, int has_mips)
{
    void *arg1 = *(void**)(texture + 0x78);       /* Likely PNG payload address */
    int arg2 = *(int*)(texture + 0x80);           /* Likely PNG payload size */
    uint32_t *arg3 = (uint32_t*)(texture + 0x18); /* Address to Flags */
    uint32_t *arg4 = (uint32_t*)(texture + 0x20); /* Address to OpenGL Texture Id */

    LoadTextureFromPNG_generic(arg1, arg2, arg3, arg4, (uint32_t*)texture);
}

// Older libyoyo builds where the Texture struct is tighter.
// Flags at +0x10, OpenGL TexID at +0x18, PNG payload at +0x48, size at +0x50.
ABI_ATTR void LoadTextureFromPNG_3(uintptr_t texture, int has_mips)
{
    void *arg1 = *(void**)(texture + 0x48);       /* PNG payload address */
    int arg2 = *(int*)(texture + 0x50);           /* PNG payload size */
    uint32_t *arg3 = (uint32_t*)(texture + 0x10); /* Address to Flags */
    uint32_t *arg4 = (uint32_t*)(texture + 0x18); /* Address to OpenGL Texture Id */

    LoadTextureFromPNG_generic(arg1, arg2, arg3, arg4, (uint32_t*)texture);
}

ABI_ATTR void LoadTextureFromQOIF_1(uintptr_t texture, int has_mips)
{
    void *arg1 = *(void**)(texture + 0x70);
    int arg2 = *(int*)(texture + 0x78);
    uint32_t *arg3 = (uint32_t*)(texture + 0x14);
    uint32_t *arg4 = (uint32_t*)(texture + 0x18);
    LoadTextureFromQOIF_generic(arg1, arg2, arg3, arg4, (uint32_t*)texture);
}

ABI_ATTR void LoadTextureFromQOIF_2(uintptr_t texture, int has_mips)
{
    void *arg1 = *(void**)(texture + 0x78);
    int arg2 = *(int*)(texture + 0x80);
    uint32_t *arg3 = (uint32_t*)(texture + 0x18);
    uint32_t *arg4 = (uint32_t*)(texture + 0x20);
    LoadTextureFromQOIF_generic(arg1, arg2, arg3, arg4, (uint32_t*)texture);
}

ABI_ATTR void LoadTextureFromQOIF_3(uintptr_t texture, int has_mips)
{
    void *arg1 = *(void**)(texture + 0x48);
    int arg2 = *(int*)(texture + 0x50);
    uint32_t *arg3 = (uint32_t*)(texture + 0x10);
    uint32_t *arg4 = (uint32_t*)(texture + 0x18);
    LoadTextureFromQOIF_generic(arg1, arg2, arg3, arg4, (uint32_t*)texture);
}

// Detect which texture-struct variant LoadTextureFromXXX expects by scanning
// its first 40 instructions for the LDR (X-form, immediate) that pulls the
// payload pointer out of `texture + imm`.
static int hook_load_texture_variant(so_module *mod, uint32_t *entry,
                                     uintptr_t v1_fn, uintptr_t v2_fn,
                                     uintptr_t v3_fn, const char *tag)
{
    for (uint32_t *cursor = entry; (cursor - entry) < 40; cursor++) {
        uint32_t inst = *cursor;
        if ((inst & 0xFFC0001F) != 0xF9400000) continue; // not LDR Xn, [Xm, #imm12]
        uint32_t imm12 = ((inst >> 10) & 0xFFF) << 3;
        if (imm12 == 0x70) {
            warning("Using Texture Hack %s_1.\n", tag);
            hook_address(mod, (uintptr_t)entry, v1_fn);
            return 1;
        }
        if (imm12 == 0x78) {
            warning("Using Texture Hack %s_2.\n", tag);
            hook_address(mod, (uintptr_t)entry, v2_fn);
            return 1;
        }
        if (imm12 == 0x48) {
            warning("Using Texture Hack %s_3.\n", tag);
            hook_address(mod, (uintptr_t)entry, v3_fn);
            return 1;
        }
    }
    return 0;
}

void patch_texture(so_module *mod)
{
    if (gmloader_config.disable_texhack) {
        warning("Texture compression hack NOT ENABLED!!!\n");
        return;
    }

    // No externalized PVRs to load
    if (!has_externalized_pvrs()) {
        warning("Texture hack: no externalized PVRs detected, skipping.\n");
        return;
    }

    uint32_t *LoadTextureFromPNG = (uint32_t *)so_symbol(mod, "_Z18LoadTextureFromPNGP7Texture10eMipEnable");
    if (!hook_load_texture_variant(mod, LoadTextureFromPNG,
                                   (uintptr_t)&LoadTextureFromPNG_1,
                                   (uintptr_t)&LoadTextureFromPNG_2,
                                   (uintptr_t)&LoadTextureFromPNG_3,
                                   "LoadTextureFromPNG"))
    {
        fatal_error(" -- Requested texture_hack, but could not find PNG signature.\n");
        return;
    }
    hook_symbol(mod, "png_get_IHDR", (uintptr_t)&png_get_IHDR_hook, 0);

    // QOIF path only exists on GMS 2022.x+. Hook it when the symbol is present.
    uint32_t *LoadTextureFromQOIF = (uint32_t *)so_symbol(mod, "_Z19LoadTextureFromQOIFP7Texture10eMipEnable");
    if (LoadTextureFromQOIF) {
        if (!hook_load_texture_variant(mod, LoadTextureFromQOIF,
                                       (uintptr_t)&LoadTextureFromQOIF_1,
                                       (uintptr_t)&LoadTextureFromQOIF_2,
                                       (uintptr_t)&LoadTextureFromQOIF_3,
                                       "LoadTextureFromQOIF"))
        {
            warning(" -- LoadTextureFromQOIF found but no recognized signature; QOIF texhack disabled.\n");
            return;
        }
        hook_symbol(mod, "_Z18ReadQOIFFileHeaderPviPiS0_b", (uintptr_t)&qoif_read_header_hook, 0);
    }
}

#endif