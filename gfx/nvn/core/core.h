// gfx/nvn/core: the replay's decisions in plain C, without Metal (plan item 264), so that a second backend (Vulkan) can share them. Each module here
// is its own translation unit with a header that declares all the rest of the graphics layer may use of it; what a module needs from the backend
// (gfx/nvn/mtl, the Metal one) its header declares under "The backend provides". The modules build into the host and the iOS app (host/Makefile
// GFX) and into the graphics tests; each has a unit test that needs no GPU (gfx/tests/t_core_<module>.c); tools/tests/t_core.py checks that every
// file here compiles as plain C and that the build takes them.
// This header: the log lines and the red lines printed once a tag, shared with the backend (the same prefix, so whoever reads the log sees the same
// lines as before the move).
#pragma once
#include <stdio.h>
#include <stdlib.h>

#define MLOG(...) do { fprintf(stderr, "[dm] [nvn-mtl] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define MDIE(...) do { MLOG("FATAL " __VA_ARGS__); abort(); } while (0)
// A red line printed once per tag (plan item 125; tools/tests/t_red_once.py: one message a tag, every tag within the array)
extern int g_red_once[120];
#define RED_ONCE(k, ...) do { if (!g_red_once[k]++) MLOG("[red] " __VA_ARGS__); } while (0)
