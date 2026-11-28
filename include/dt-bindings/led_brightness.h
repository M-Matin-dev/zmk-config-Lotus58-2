/*
 * Parameters for the &led_brt behavior (see src/behavior_led_brightness.c).
 * Shared by the keymap (devicetree) and the C code.
 *
 *   &led_brt LED_BRI   brightness up:   +1% below 2%, otherwise +2%
 *   &led_brt LED_BRD   brightness down: -2% down to 2%, then -1%
 */
#pragma once

#define LED_BRT_UP 1
#define LED_BRT_DOWN 2
#define LED_BRT_SET 3 /* internal: absolute colour in param2, sent to both halves */

#define LED_BRI LED_BRT_UP 0
#define LED_BRD LED_BRT_DOWN 0
