/* XCP on UDP for a controller on the PC (XCPlite, Linux), started by main_host.c with --xcp PORT.
 * Built only into a controller's host build with -DKRUMPIR_XCP=ON, so the hil_host library itself
 * does not need XCPlite. The A2L comes from the program's ELF (controllers/README.md, "XCP on the
 * PC build"). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "xcp_epk.h" /* XCP_EPK, new for every build, so an A2L of another build is refused */
#include "xcplib.h"

#ifndef CTRL_NAME
#define CTRL_NAME "controller" /* the host build sets <name>_host */
#endif

bool xcp_host_start(uint16_t port)
{
  XcpSetLogLevel(2);
  XcpCreateEpk(XCP_EPK);
  if (!XcpInit(CTRL_NAME, XCP_EPK, XCP_MODE_LOCAL)) {
    fprintf(stderr, "xcp: XcpInit failed\n");
    return false;
  }
  const uint8_t any[4] = {0, 0, 0, 0};
  if (!XcpEthServerInit(any, port, false, 64 * 1024)) { /* the measurement queue, in bytes */
    fprintf(stderr, "xcp: no XCP server on UDP port %u\n", port);
    return false;
  }
  fprintf(stderr, "xcp: on UDP port %u, EPK %s\n", port, XCP_EPK);
  return true;
}
