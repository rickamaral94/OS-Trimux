/* coreprobe: dlopen()s libretro cores and prints retro_get_system_info().
 * Run under qemu-aarch64 with the firmware's libraries to prove every core
 * resolves its symbols against firmware v1.1.1 (glibc 2.33, libstdc++ GCC 10).
 * It does not run emulation. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

int main(int argc, char **argv)
{
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        void *h = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            printf("FAIL %s: %s\n", argv[i], dlerror());
            bad++;
            continue;
        }
        void (*info)(struct retro_system_info *) = (void (*)(struct retro_system_info *))dlsym(h, "retro_get_system_info");
        unsigned (*api)(void) = (unsigned (*)(void))dlsym(h, "retro_api_version");
        if (!info || !api) {
            printf("FAIL %s: not a libretro core\n", argv[i]);
            bad++;
            dlclose(h);
            continue;
        }
        struct retro_system_info si = {0};
        info(&si);
        printf("ok   %-28s api=%u name=\"%s\" version=\"%s\" ext=\"%s\"\n", argv[i], api(),
               si.library_name ? si.library_name : "", si.library_version ? si.library_version : "",
               si.valid_extensions ? si.valid_extensions : "");
        dlclose(h);
    }
    return bad ? 1 : 0;
}
