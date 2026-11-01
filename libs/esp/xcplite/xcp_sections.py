"""PlatformIO extra script (post:) for ESP-IDF builds with XCPlite.

XCPlite registers its events and calibration segments in the linker sections xcp_evts and xcp_cals
and its A2L metadata in xcp_epk and xcp_meta. Left alone, the linker places them between
.flash.appdesc and .flash.rodata, and ESP-IDF refuses to build the image. Right before linking, this
patches the sections.ld that ESP-IDF generated in the build folder (the same way as XCPlite's own
ESP32 example):

- xcp_cals and xcp_evts go to the start of .flash.rodata, with the __start_/__stop_ symbols XCPlite
  reads at XcpInit;
- xcp_epk and xcp_meta follow .flash.rodata as named sections (xcpclient finds them by name). Their
  trailing ALIGN(8) keeps the flash rodata in one DROM segment; a gap splits it, and the bootloader
  then maps only the last part and the firmware crashes at boot.

The patch is idempotent, so a sections.ld that was not regenerated is left as it is.
"""
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO)

RODATA_START = "    _flash_rodata_start = ABSOLUTE(.);\n"
RODATA_END = "  _flash_rodata_align = ALIGNOF(.flash.rodata);\n"
MARK = "/* xcplite sections */"

IN_RODATA = """    %s
    __start_xcp_cals = ABSOLUTE(.);
    KEEP(*(xcp_cals))
    __stop_xcp_cals = ABSOLUTE(.);
    __start_xcp_evts = ABSOLUTE(.);
    KEEP(*(xcp_evts))
    __stop_xcp_evts = ABSOLUTE(.);
""" % MARK

AFTER_RODATA = """  xcp_epk : ALIGN(1)
  {
    KEEP(*(xcp_epk))
    . = ALIGN(8);
  } > default_rodata_seg
  xcp_meta : ALIGN(8)
  {
    KEEP(*(xcp_meta))
    . = ALIGN(8);
  } > default_rodata_seg
"""


def patch_sections(target, source, env):
    ld = Path(env.subst("$BUILD_DIR")) / "sections.ld"
    text = ld.read_text()
    if MARK in text:
        return
    if text.count(RODATA_START) != 1 or text.count(RODATA_END) != 1:
        raise RuntimeError("xcp_sections.py: unexpected layout of %s" % ld)
    text = text.replace(RODATA_START, RODATA_START + IN_RODATA)
    text = text.replace(RODATA_END, AFTER_RODATA + RODATA_END)
    ld.write_text(text)
    print("xcp_sections.py: patched %s" % ld)


env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", patch_sections)  # noqa: F821
