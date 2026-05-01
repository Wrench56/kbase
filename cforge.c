#define CF_DISABLE_FILE_HASH

#include "cforge.h"

#include <stdio.h>

#define APP_NAME "kbase"
#define BUILD_DIR "build"

#define CC_TAG "[" CF_YELLOW "CC" CF_RESET "] "
#define LD_TAG "[" CF_CYAN "LD" CF_RESET "] "
#define RN_TAG "[" CF_GREEN "RN" CF_RESET "] "

bool was_rebuilt = false;

#if !CF_VERSION_AT_LEAST(1, 0, 0) || !CF_VERSION_BELOW(2, 0, 0)
    #error "CForge version invalid"
#endif // version check

CF_CONFIG(release) {
    CF_SET_ENV(mode, "release");

    CF_SET_ENV(cflags, "-O2");
    CF_SET_ENV(includes, "-Iincludes/");
}

CF_CONFIG(debug) {
    CF_SET_ENV(mode, "debug");
    
    CF_SET_ENV(cflags, "-g");
    CF_SET_ENV(includes, "-Iincludes/");
}

CF_TARGET(release, CF_WITH_CONFIG(release), CF_DEPENDS(build), CF_HELP_STRING("Build in release mode")) {
    CF_NOP();
}

CF_TARGET(debug, CF_WITH_CONFIG(debug), CF_DEPENDS(build), CF_HELP_STRING("Build in debug mode")) {
    CF_NOP();
}

CF_TARGET(run, CF_DEPENDS(debug), CF_HELP_STRING("Run mdprev")) {
    printf(RN_TAG "Running %s...\n", APP_NAME);
    CF_RUN("./%s/%s", BUILD_DIR, APP_NAME);
}

CF_TARGET(build, CF_DEPENDS(link), CF_HIDDEN) {
    if (!was_rebuilt) {
        return;
    }
    printf("\n=========================\nBuilt using mode: %s\n=========================\n\n", CF_ENV(mode));
}

CF_TARGET(link, CF_DEPENDS(compile), CF_HIDDEN) {
    if (CF_FILE_NOT_UTD(BUILD_DIR "/" APP_NAME) || was_rebuilt) {
        CF_BANNER(LD_TAG "Linking...");
        char* object_files = CF_JOIN_GLOB(CF_GLOB(BUILD_DIR "/*.o"), " ");
        printf(LD_TAG "  %s\n", object_files);
        CF_RUN("cc %s -o %s/%s", object_files, BUILD_DIR, APP_NAME);
        CF_FILE_MARK_UTD(BUILD_DIR "/" APP_NAME);
    }
}

CF_TARGET(compile, CF_HIDDEN) {
    CF_MKDIR(BUILD_DIR);
    for CF_GLOBS_EACH("src/*.c", file) {
        char* output = CF_MAP(file, CF_MAP_EXT("o"), CF_MAP_PARENT(BUILD_DIR));
        if (CF_FILE_NOT_UTD(file) || CF_FILE_NOT_UTD(output)) {
            was_rebuilt = true;
            CF_BANNER(CC_TAG "Compiling...");
            printf(CC_TAG "  %s\n", file);
            CF_RUNP("cc %s %s -c %s -o %s",
                CF_ENV(cflags),
                CF_ENV(includes),
                file,
                output
            );
            CF_FILE_MARK_UTDP(file);
            CF_FILE_MARK_UTDP(output);
        }
    }
}

