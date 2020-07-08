/**
 * BetterMouseYoke - X-Plane 11 Plugin
 *
 * Does away with X-Plane's idiotic centered little box for mouse steering and
 * replaces it with a more sane system for those who, for whatever reason,
 * want to or have to use the mouse for flying.
 *
 * Copyright 2019 Torben K�nke.
 */
#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include "../Util/util.h"
#include "../XP/XPLMDisplay.h"
#include "../XP/XPLMGraphics.h"
#include "../XP/XPLMProcessing.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#ifdef IBM
#pragma comment(lib, "detours.lib")
#include "detours.h"
int hook_set_cursor(int attach);
#endif

typedef enum {
    CURSOR_ARROW,
    CURSOR_YOKE,
    CURSOR_RUDDER
} cursor_t;

int init_menu();
int toggle_yoke_control_cb(XPLMCommandRef cmd, XPLMCommandPhase phase, void *ref);
int rudder_left_cb(XPLMCommandRef cmd, XPLMCommandPhase phase, void *ref);
int rudder_right_cb(XPLMCommandRef cmd, XPLMCommandPhase phase, void *ref);
int draw_cb(XPLMDrawingPhase phase, int before, void *ref);
float loop_cb(float last_call, float last_loop, int count, void *ref);
void get_cursor_pos(int *x, int *y);
void set_cursor_from_yoke();
void set_cursor_pos(int x, int y);
void set_cursor_bmp(cursor_t cursor);
int controlling_rudder(int *x, int *y);
#endif /* _PLUGIN_H_ */
