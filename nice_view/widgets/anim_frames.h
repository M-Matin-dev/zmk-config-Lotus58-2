#pragma once

#include <stdint.h>

/* Animation drawn over the top 136 px of the nice!view (see anim.c). Generated
 * data lives in anim_frames.c; rebuild it with tools/nice_view_anim.py. */

#define ANIM_W 136 /* landscape, as LVGL sees the screen */
#define ANIM_H 68
#define ANIM_STRIDE ((ANIM_W + 7) / 8)
#define ANIM_FRAME_BYTES (ANIM_STRIDE * ANIM_H)

extern const uint16_t anim_frame_count;
extern const uint8_t anim_first_frame[ANIM_FRAME_BYTES];
/* Frame k = frame k-1 with anim_deltas[anim_delta_offsets[k] .. [k+1]) applied. */
extern const uint8_t anim_deltas[];
extern const uint32_t anim_delta_offsets[];
extern const uint16_t anim_frame_ms[];
