#ifndef KRUMPIR_SERVE_H
#define KRUMPIR_SERVE_H

/* The model-access service: one session driven over TCP, one JSON request per line and one JSON
 * reply per line (docs/service-protocol.md). One client at a time; the run outlives a client.
 * Returns when SIGINT or SIGTERM arrives (the run is stopped and unloaded), or on a setup error.
 * Prints "listening on <host>:<port>" once it accepts connections (port 0 picks a free port). */
int serve(const char *host, int port);

#endif
