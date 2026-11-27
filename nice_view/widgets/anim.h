#pragma once

#include <lvgl.h>

/* Animation over the top of the status screen; see anim.c. Display thread only. */
void anim_init(lv_obj_t *parent);
/* Re-evaluate whether to show it now (e.g. right after a layer change). */
void anim_check(void);
