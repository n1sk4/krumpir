/* XCPlite for a controller on the PC (XCPLITE_CFG_OVERRIDE of its host build): the default Linux
 * configuration, plus link-time registration of the calibration page and the events
 * (CalSegDecl, DaqCreateEvent), as in the board's configuration, so control_task.c is the same. */
#define OPTION_SECTION_REGISTRATION
/* The EPK stays in calibration segment 0 (the default), so an A2L made offline needs
 * tools/hil/fix_host_a2l.py. */
