#pragma once

#include "so_util.h"

void patch_gamedata(so_module *mod, const char *apk_path);
bool gamedata_mmap_release(void *ptr);
