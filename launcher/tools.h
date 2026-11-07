/* What tools.c and tools_install.c share. */
#pragma once
#include "launcher.h"

enum { VIEWER, EDITOR, TOOLS_COUNT };
enum { P_DOWNLOAD, P_UNPACK, P_SETUP };
typedef struct { const char *repo, *folder, *exe, *dev; } ToolInfo;
extern const ToolInfo TOOLS[TOOLS_COUNT];
extern SDL_AtomicInt s_stop, s_percent, s_phase;
extern bool s_faked; /* --fake-tool-update: the release check leaves the viewer's offer alone */
extern char s_update[TOOLS_COUNT][32]; /* the newer version on offer, written by the release check */
int fetch_unpack(const char *url, const char *tmp); /* the zip into tmp, unpacked: S_T_READY or why not */
int tool_install(int i); /* blocking: S_T_READY or the string that says why not */
