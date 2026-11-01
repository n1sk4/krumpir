#ifndef XCP_GLUE_H
#define XCP_GLUE_H

#include <stdbool.h>

/* Starts XCP for a controller: joins the Wi-Fi network from wlan.h and serves XCP on Ethernet on
 * UDP port 5555. Call it from app_main before the control task starts, since the XCP events in the
 * loop need XcpInit. Blocks up to 30 s for Wi-Fi; without Wi-Fi, XCP stays passive and the
 * controller runs anyway. name: project name for the A2L file, at most 31 characters. Returns true
 * when the server runs. */
bool xcp_glue_start(const char *name);

#endif
