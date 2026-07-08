#include <3ds.h>
#include "core/Engine.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    poly::Engine engine;
    if (!engine.init()) {
        // Renderer or a core subsystem failed to come up. There is no
        // graphics output available to report this on-screen (gfx may
        // not have initialized), so fail fast and let 3dslink/console
        // output (if attached) show the failure.
        return 1;
    }

    const int status = engine.run();
    engine.shutdown();
    return status;
}
