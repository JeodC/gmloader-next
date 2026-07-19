#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "platform.h"
#include "so_util.h"
#include "gamedata_mmap.h"

#define GD_MAX_MAPS 4
#define GD_MAX_CDIR (64u * 1024u * 1024u)

// Buffers handed to the runner
static struct gd_mapping {
    void *user;
    void *base;
    size_t len;
} g_maps[GD_MAX_MAPS];
static int g_map_count = 0;

// Stored game.droid's byte range
static char g_apk_path[PATH_MAX];
static uint64_t g_entry_off;
static uint64_t g_entry_size;

typedef void *(ABI_ATTR *readfile_t)(const char *name, unsigned int *size);
static ReentrantHook REHReadFile = {};
static readfile_t Original_ReadFile = NULL;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static bool is_game_droid(const char *name)
{
    const char *base = strrchr(name, '/');
    return strcmp(base ? base + 1 : name, "game.droid") == 0;
}

static bool entry_is_game_droid(const uint8_t *name, uint16_t nl)
{
    if (nl == 0 || nl >= 256 || name[nl - 1] == '/')
        return false;

    char buf[256];
    memcpy(buf, name, nl);
    buf[nl] = '\0';
    return is_game_droid(buf);
}

// Replace whichever 32-bit fields overflowed with
// the real values from the entry's Zip64 extra field
static void read_zip64_extra(const uint8_t *extra, uint16_t extra_len,
                             uint64_t *usz, uint64_t *csz, uint64_t *lho)
{
    for (const uint8_t *x = extra, *xe = extra + extra_len; x + 4 <= xe; x += 4 + rd16(x + 2)) {
        if (rd16(x) != 0x0001)
            continue;

        const uint8_t *q = x + 4;
        if (*usz == 0xFFFFFFFF) { *usz = rd64(q); q += 8; }
        if (*csz == 0xFFFFFFFF) { *csz = rd64(q); q += 8; }
        if (*lho == 0xFFFFFFFF) *lho = rd64(q);
        return;
    }
}

// Payload starts past the local file header, whose name/extra lengths can differ
// from the central directory's
static bool read_local_data_offset(FILE *f, uint64_t lho, uint64_t *data_off)
{
    uint8_t lfh[30];
    if (fseeko(f, (off_t)lho, SEEK_SET) != 0 || fread(lfh, 1, 30, f) != 30 || rd32(lfh) != 0x04034b50)
        return false;

    *data_off = lho + 30 + rd16(lfh + 26) + rd16(lfh + 28);
    return true;
}

// Find the End-Of-Central-Directory (or its Zip64 twin) and report where the
// central directory lives
static bool read_eocd(FILE *f, off_t fsz, uint64_t *cd_off, uint64_t *cd_size, uint64_t *cd_count)
{
    size_t tail_len = fsz < 65557 ? (size_t)fsz : 65557;
    uint8_t *tail = (uint8_t *)malloc(tail_len);
    if (!tail)
        return false;

    bool read_ok = fseeko(f, fsz - (off_t)tail_len, SEEK_SET) == 0 && fread(tail, 1, tail_len, f) == tail_len;

    // Search back for the signature whose comment length lands exactly on EOF
    const uint8_t *eocd = NULL;
    off_t eocd_pos = 0;
    for (ssize_t i = (ssize_t)tail_len - 22; read_ok && i >= 0 && !eocd; i--) {
        if (rd32(tail + i) != 0x06054b50 || fsz - (off_t)tail_len + i + 22 + rd16(tail + i + 20) != fsz)
            continue;
        eocd = tail + i;
        eocd_pos = fsz - (off_t)tail_len + i;
    }
    if (eocd) {
        *cd_count = rd16(eocd + 10);
        *cd_size  = rd32(eocd + 12);
        *cd_off   = rd32(eocd + 16);
    }
    free(tail);
    if (!eocd)
        return false;

    // Plain zip: the record we just read is complete
    if (*cd_count != 0xFFFF && *cd_size != 0xFFFFFFFF && *cd_off != 0xFFFFFFFF)
        return true;

    // Zip64: the locator precedes the EOCD and points at the real record
    uint8_t loc[20], eocd64[56];
    if (eocd_pos < 20 || fseeko(f, eocd_pos - 20, SEEK_SET) != 0 || fread(loc, 1, 20, f) != 20 || rd32(loc) != 0x07064b50)
        return false;
    if (fseeko(f, (off_t)rd64(loc + 8), SEEK_SET) != 0 || fread(eocd64, 1, 56, f) != 56 || rd32(eocd64) != 0x06064b50)
        return false;

    *cd_count = rd64(eocd64 + 32);
    *cd_size  = rd64(eocd64 + 40);
    *cd_off   = rd64(eocd64 + 48);
    return true;
}

// Read the whole central directory into a freshly malloc'd buffer (caller frees)
static uint8_t *read_central_directory(FILE *f, off_t fsz, uint64_t *out_size, uint64_t *out_count)
{
    uint64_t cd_off, cd_size, cd_count;
    if (!read_eocd(f, fsz, &cd_off, &cd_size, &cd_count) || cd_size == 0 || cd_size > GD_MAX_CDIR)
        return NULL;

    uint8_t *cdir = (uint8_t *)malloc(cd_size);
    if (!cdir)
        return NULL;
    if (fseeko(f, (off_t)cd_off, SEEK_SET) != 0 || fread(cdir, 1, cd_size, f) != cd_size) {
        free(cdir);
        return NULL;
    }

    *out_size = cd_size;
    *out_count = cd_count;
    return cdir;
}

// Find an uncompressed game.droid in the central directory and resolve its payload offset
static bool find_gamedroid_entry(FILE *f, const uint8_t *cdir, uint64_t cd_size, uint64_t cd_count,
                                 off_t fsz, uint64_t *out_off, uint64_t *out_size)
{
    const uint8_t *p = cdir, *end = cdir + cd_size;
    for (uint64_t n = 0; n < cd_count; n++) {
        if (p + 46 > end || rd32(p) != 0x02014b50)
            break;

        uint16_t comp = rd16(p + 10);
        uint64_t csz  = rd32(p + 20);
        uint64_t usz  = rd32(p + 24);
        uint16_t nl   = rd16(p + 28);
        uint16_t el   = rd16(p + 30);
        uint16_t cl   = rd16(p + 32);
        uint64_t lho  = rd32(p + 42);
        const uint8_t *name = p + 46;
        if (name + nl + el + cl > end)
            break;
        p = name + nl + el + cl;

        if (!entry_is_game_droid(name, nl))
            continue;

        if (usz == 0xFFFFFFFF || csz == 0xFFFFFFFF || lho == 0xFFFFFFFF)
            read_zip64_extra(name + nl, el, &usz, &csz, &lho);

        if (comp != 0) {
            warning("mmap: game.droid is compressed (method %u); using runner read.\n", comp);
            return false;
        }

        uint64_t data_off;
        if (!read_local_data_offset(f, lho, &data_off))
            return false;

        // buf[size] gets a NUL from the runner, so that byte must be mappable too
        if (csz != usz || data_off + usz + 1 > (uint64_t)fsz)
            return false;

        *out_off = data_off;
        *out_size = usz;
        return true;
    }
    return false;
}

// Find where an uncompressed game.droid's bytes live inside the archive
static bool find_stored_gamedroid(const char *zip_path, uint64_t *out_off, uint64_t *out_size)
{
    FILE *f = fopen(zip_path, "rb");
    if (!f)
        return false;

    fseeko(f, 0, SEEK_END);
    off_t fsz = ftello(f);
    if (fsz < 22) {
        fclose(f);
        return false;
    }

    uint64_t cd_size, cd_count;
    uint8_t *cdir = read_central_directory(f, fsz, &cd_size, &cd_count);
    bool found = cdir && find_gamedroid_entry(f, cdir, cd_size, cd_count, fsz, out_off, out_size);

    free(cdir);
    fclose(f);
    return found;
}

// Callers check capacity before mapping, so this just records and publishes
static void register_mapping(void *user, void *base, size_t len)
{
    g_maps[g_map_count] = { user, base, len };
    __atomic_store_n(&g_map_count, g_map_count + 1, __ATOMIC_RELEASE);
}

bool gamedata_mmap_release(void *ptr)
{
    if (!ptr)
        return false;

    int n = __atomic_load_n(&g_map_count, __ATOMIC_ACQUIRE);
    for (int i = 0; i < n; i++) {
        if (g_maps[i].user != ptr)
            continue;
        munmap(g_maps[i].base, g_maps[i].len);
        g_maps[i].user = NULL;    // one-shot; never rematch
        warning("gamedata_mmap: released mapping %p\n", ptr);
        return true;
    }
    return false;
}

// Map the stored entry
static void *serve_stored_entry(size_t *out_size)
{
    if (g_entry_size == 0 || g_map_count >= GD_MAX_MAPS)
        return NULL;

    int fd = open(g_apk_path, O_RDONLY);
    if (fd < 0)
        return NULL;

    long pagesz = sysconf(_SC_PAGESIZE);
    uint64_t page_off = g_entry_off & ~(uint64_t)(pagesz - 1);
    size_t delta = (size_t)(g_entry_off - page_off);
    size_t map_len = delta + (size_t)g_entry_size + 1;

    void *base = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, (off_t)page_off);
    close(fd);
    if (base == MAP_FAILED)
        return NULL;

    char *user = (char *)base + delta;
    user[g_entry_size] = '\0';

    register_mapping(user, base, map_len);
    *out_size = (size_t)g_entry_size;
    return user;
}

// Map a loose game.droid directly
static void *serve_loose_file(const char *name, size_t *out_size)
{
    struct stat st;
    if (g_map_count >= GD_MAX_MAPS || stat(name, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
        return NULL;

    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL;

    long pagesz = sysconf(_SC_PAGESIZE);
    size_t sz = (size_t)st.st_size;
    size_t total = ALIGN_MEM(sz, (size_t)pagesz) + (size_t)pagesz;

    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    if (mmap(base, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0) == MAP_FAILED) {
        munmap(base, total);
        close(fd);
        return NULL;
    }
    close(fd);

    ((char *)base)[sz] = '\0';

    register_mapping(base, base, total);
    *out_size = sz;
    return base;
}

ABI_ATTR static void *ReadFile_hook(const char *name, unsigned int *size)
{
    void *ptr = NULL;
    size_t sz = 0;
    if (name && is_game_droid(name)) {
        ptr = serve_stored_entry(&sz);
        if (!ptr)
            ptr = serve_loose_file(name, &sz);
    }

    if (ptr) {
        warning("gamedata_mmap: serving '%s' via mmap (%zu bytes at %p)\n", name, sz, ptr);
        if (size)
            *size = (unsigned int)sz;
        return ptr;
    }

    // Not game.droid, or nothing to map
    rehook_unhook(&REHReadFile);
    ptr = Original_ReadFile(name, size);
    rehook_hook(&REHReadFile);
    return ptr;
}

void patch_gamedata(so_module *mod, const char *apk_path)
{
    snprintf(g_apk_path, sizeof(g_apk_path), "%s", apk_path);
    if (find_stored_gamedroid(g_apk_path, &g_entry_off, &g_entry_size))
        warning("mmap: game.droid stored in '%s' at %llu (%llu bytes)\n",
                g_apk_path, (unsigned long long)g_entry_off, (unsigned long long)g_entry_size);

    uintptr_t addr = so_symbol(mod, "_ZN8LoadSave9_ReadFileEPKcPj");
    if (!addr)
        addr = so_symbol(mod, "_ZN8LoadSave9_ReadFileEPKcPi");
    if (!addr) {
        warning("mmap: LoadSave::_ReadFile not found; runner reads unchanged.\n");
        return;
    }

    Original_ReadFile = (readfile_t)addr;
    rehook_new(mod, &REHReadFile, addr, (uintptr_t)&ReadFile_hook);
    rehook_hook(&REHReadFile);
}
