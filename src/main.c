// SPDX-License-Identifier: MIT
#include "core.h"
#include "debug.h"

#if defined(ANDROID) || defined(BOX64LIB)
EXPORT int box64_main(int argc, const char **argv, char **env) {
    x64emu_t* emu = NULL;
    elfheader_t* elf_header = NULL;
    if (initialize(argc, argv, env, &emu, &elf_header, 1)) {
        return -1;
    }

    return emulate(emu, elf_header);
}
#else
int main(int argc, const char **argv, char **env) {

    x64emu_t* emu = NULL;
    elfheader_t* elf_header = NULL;
    if (initialize(argc, argv, env, &emu, &elf_header, 1)) {
        return -1;
    }

    return emulate(emu, elf_header);
}
#endif
