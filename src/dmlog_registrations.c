/*
 * Registers dmlog's Built-in API in the .dmod.inputs section so that a
 * system-side binary statically linking libdmlog.a (via dmod_link_builtin)
 * is recognised by the loader as providing the "dmlog" system module (see
 * Dmod_Mgr_IsSystemModule() in dmod's src/system/private/dmod_mgr.c).
 *
 * This must live in its own translation unit, separate from dmlog.c: only
 * dmlog.h is included here (which itself only pulls in dmod_types.h, not
 * the full dmod.h), so defining DMOD_ENABLE_REGISTRATION only materialises
 * dmlog's own registration structs - it can't accidentally re-register
 * dmod's kernel Built-in APIs the way including the full dmod.h would.
 */
#define DMOD_ENABLE_REGISTRATION ON
#include "dmlog.h"
