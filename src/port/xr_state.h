/* Save states: all of xrick's variables (and xr_sys.c's) + its memory. */
#ifndef XR_STATE_H
#define XR_STATE_H

#include <stdio.h>

int xr_state_save(FILE *f);	/* 0 if done */
int xr_state_load(FILE *f);	/* 0 if done; -1: not this build's state */

#endif
