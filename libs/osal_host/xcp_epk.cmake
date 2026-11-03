# Writes xcp_epk.h with a new EPK (host_<date>_<time>), run on every build of a host controller
# with XCP: cmake -DOUT=<dir>/xcp_epk.h -P xcp_epk.cmake. The ESP builds do the same in
# libs/esp/xcpglue/xcp_epk.py.
string(TIMESTAMP stamp "%Y%m%d_%H%M%S")
file(WRITE "${OUT}" "#define XCP_EPK \"host_${stamp}\"\n")
