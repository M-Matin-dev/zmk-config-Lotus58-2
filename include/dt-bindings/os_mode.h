/*
 * Parameters for the &os_mode behavior (see src/os_mode.c).
 * Shared by the keymap (devicetree) and the C code.
 */
#pragma once

#define OS_WIN 0 /* Use Windows/Linux mods on the current connection */
#define OS_MAC 1 /* Use Mac mods on the current connection */
#define OS_TOG 2 /* Flip between the two */
