/**
 * BetterMouseYoke - X-Plane 11 Plugin
 *
 * Does away with X-Plane's idiotic centered little box for mouse steering and
 * replaces it with a more sane system for those who, for whatever reason,
 * want to or have to use the mouse for flying.
 *
 * Copyright 2019 Torben K�nke.
 */
#include "plugin.h"

#define PLUGIN_NAME         "BetterMouseYoke"
#define PLUGIN_SIG          "S22.BetterMouseYoke"
#define PLUGIN_DESCRIPTION  "Does away with X-Plane's idiotic centered little box " \
                            "for mouse steering that has caused much grieve and "   \
                            "countless loss of virtual lives."
#define PLUGIN_VERSION      "1.95"

#define RUDDER_DEFL_DIST    200
#define RUDDER_RET_SPEED    2.0f

static XPLMCommandRef toggle_yoke_control;
static XPLMCommandRef rudder_left;
static XPLMCommandRef rudder_right;
static XPLMDataRef yoke_pitch_ratio;
static XPLMDataRef yoke_roll_ratio;
static XPLMDataRef yoke_heading_ratio;
static XPLMDataRef eq_pfc_yoke;
static XPLMFlightLoopID loop_id;
static int screen_width;
static int screen_height;
static int yoke_control_enabled;
static int rudder_control;
static int centre_control;
static int bind_rudder;
static float magenta[] = { 1.0f, 0, 1.0f };
static float green[] = { 0, 1.0f, 0 };
static int set_pos;
static int change_cursor;
static int cursor_pos[2];
static int set_rudder_pos;
static int rudder_return;
static int rudder_defl_dist;
static bool kbd_rudder_on;
static float kbd_rudder_speed;
static float yaw_ratio;
static float rudder_ret_spd;
static float yoke_nz;
static long long _last_time;
#ifdef IBM
static HWND xp_hwnd;
static HCURSOR yoke_cursor;
static HCURSOR rudder_cursor;
static HCURSOR arrow_cursor;
static HCURSOR(WINAPI *true_set_cursor) (HCURSOR cursor) = SetCursor;
#endif

/**
 * X-Plane 11 Plugin Entry Point.
 *
 * Called when a plugin is initially loaded into X-Plane 11. If 0 is returned,
 * the plugin will be unloaded immediately with no further calls to any of
 * its callbacks.
 */
PLUGIN_API int XPluginStart(char *name, char *sig, char *desc) {
    /* SDK docs state buffers are at least 256 bytes. */
    sprintf(name, "%s (v%s)", PLUGIN_NAME, PLUGIN_VERSION);
    strcpy(sig, PLUGIN_SIG);
    strcpy(desc, PLUGIN_DESCRIPTION);
    toggle_yoke_control = XPLMCreateCommand("BetterMouseYoke/ToggleYokeControl",
        "Toggle mouse yoke control");
	rudder_left = XPLMCreateCommand("BetterMouseYoke/RudderLeft",
		"Move Rudder Left");
	rudder_right = XPLMCreateCommand("BetterMouseYoke/RudderRight",
		"Move Rudder Right");
    yoke_pitch_ratio = XPLMFindDataRef("sim/cockpit2/controls/yoke_pitch_ratio");
    if (yoke_pitch_ratio == NULL) {
        _log("init fail: could not find yoke_pitch_ratio dataref");
        return 0;
    }
    yoke_roll_ratio = XPLMFindDataRef("sim/cockpit2/controls/yoke_roll_ratio");
    if (yoke_roll_ratio == NULL) {
        _log("init fail: could not find yoke_roll_ratio dataref");
        return 0;
    }
    yoke_heading_ratio = XPLMFindDataRef(
        "sim/cockpit2/controls/yoke_heading_ratio");
    if (yoke_heading_ratio == NULL) {
        _log("init fail: could not find yoke_heading_ratio dataref");
        return 0;
    }
    eq_pfc_yoke = XPLMFindDataRef("sim/joystick/eq_pfc_yoke");
    if (eq_pfc_yoke == NULL) {
        _log("init fail: could not find eq_pfc_yoke dataref");
        return 0;
    }
    XPLMDataRef has_joystick = XPLMFindDataRef("sim/joystick/has_joystick");
    if (XPLMGetDatai(has_joystick)) {
        _log("init: joystick detected, unloading plugin");
        return 0;
    }
    if (!init_menu()) {
        _log("init: could not init menu");
        return 0;
    }
    rudder_defl_dist = ini_geti("rudder_deflection_distance", RUDDER_DEFL_DIST);
    rudder_ret_spd = ini_getf("rudder_return_speed", RUDDER_RET_SPEED);
	yoke_nz = ini_getf("yoke_null_zone", 0.05);
	centre_control = ini_geti("centre_control", 0);
	kbd_rudder_speed = ini_getf("rudder_dfl_speed", 0.15);
#ifdef IBM
    xp_hwnd = FindWindowA("X-System", "X-System");
    if (!xp_hwnd) {
        _log("could not find X-Plane 11 window");
        return 0;
    }
    if (!hook_set_cursor(1)) {
        _log("could not hook SetCursor function");
        return 0;
    }
    yoke_cursor = LoadCursor(NULL, IDC_SIZEALL);
    if (!yoke_cursor) {
        _log("could not load yoke_cursor");
        return 0;
    }
    rudder_cursor = LoadCursor(NULL, IDC_SIZEWE);
    if (!rudder_cursor) {
        _log("could not load rudder_cursor");
        return 0;
    }
    arrow_cursor = LoadCursor(NULL, IDC_ARROW);
    if (!arrow_cursor) {
        _log("could not load arrow_cursor");
        return 0;
    }
#endif
    return 1;
}

/**
 * X-Plane 11 Plugin Callback
 *
 * Called when the plugin is about to be unloaded from X-Plane 11.
 */
PLUGIN_API void XPluginStop(void) {
#ifdef IBM
    if (!hook_set_cursor(0)) {
        _log("could not unhook SetCursor function");
    }
#endif
}

/**
 * X-Plane 11 Plugin Callback
 *
 * Called when the plugin is about to be enabled. Return 1 if the plugin
 * started successfully, otherwise 0.
 */
PLUGIN_API int XPluginEnable(void) {
    XPLMRegisterCommandHandler(toggle_yoke_control, toggle_yoke_control_cb,
        0, NULL);
	XPLMRegisterCommandHandler(rudder_left, rudder_left_cb,
		0, NULL);
	XPLMRegisterCommandHandler(rudder_right, rudder_right_cb,
		0, NULL);
    XPLMRegisterDrawCallback(draw_cb, xplm_Phase_Window, 0, NULL);
    XPLMCreateFlightLoop_t params = {
        .structSize = sizeof(XPLMCreateFlightLoop_t),
        .phase = xplm_FlightLoop_Phase_BeforeFlightModel,
        .refcon = NULL,
        .callbackFunc = loop_cb
    };
    loop_id = XPLMCreateFlightLoop(&params);
    return 1;
}

/**
 * X-Plane 11 Plugin Callback
 *
 * Called when the plugin is about to be disabled.
 */
PLUGIN_API void XPluginDisable(void) {
    XPLMUnregisterCommandHandler(toggle_yoke_control, toggle_yoke_control_cb,
        0, NULL);
	XPLMUnregisterCommandHandler(rudder_left, rudder_left_cb,
		0, NULL);
	XPLMUnregisterCommandHandler(rudder_right, rudder_right_cb,
		0, NULL);
    XPLMSetDatai(eq_pfc_yoke, 0);
    XPLMUnregisterDrawCallback(draw_cb, xplm_Phase_Window, 0, NULL);
    if (loop_id)
        XPLMDestroyFlightLoop(loop_id);
    loop_id = NULL;
    menu_deinit();
}

/**
 * X-Plane 11 Plugin Callback
 *
 * Called when a message is sent to the plugin by X-Plane 11 or another plugin.
 */
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int msg, void *param) {
    if (from != XPLM_PLUGIN_XPLANE)
        return;
    if (msg == XPLM_MSG_PLANE_LOADED) {
        int index = (int)param;
        /* user's plane */
        if (index == XPLM_USER_AIRCRAFT) {
            /* This will hide the clickable yoke control box. */
            XPLMSetDatai(eq_pfc_yoke, 1);
        }
    }
}

int init_menu() {
    menu_item_t items[] = {
		{ "Version 1.8" },
        { "Set Yoke Cursor", "set_pos", &set_pos, 1 },
        { "Set Rudder Cursor", "set_rudder_pos", &set_rudder_pos, 1 },
        { "Change Cursor Icon", "change_cursor", &change_cursor, 1 },
        { "Rudder Center", "rudder_return", &rudder_return, 1 },
		{ "Yoke Center", "centre_control", &centre_control, 0},
		{ "Bind Kbd Rudder to Yoke", "bind_rudder", &bind_rudder, 0}		
    };
    int num = sizeof(items) / sizeof(items[0]);

    return menu_init(PLUGIN_NAME, items, num);
}

int toggle_yoke_control_cb(XPLMCommandRef cmd, XPLMCommandPhase phase, void *ref) {
    if (phase != xplm_CommandBegin)
        return 1;
    if (yoke_control_enabled) {
        if (change_cursor)
            set_cursor_bmp(CURSOR_ARROW);
        yoke_control_enabled = 0;
        rudder_control = 0;
    } else {
        /* Fetch screen dimensions here because doing it from XPluginEnable
           give unrealiable results. Also the screen size may be changed by
           the user at any time. */
        XPLMGetScreenSize(&screen_width, &screen_height);
        /* Set cursor position to align with current deflection of yoke. */
        if (set_pos)
            set_cursor_from_yoke();
        if (change_cursor)
            set_cursor_bmp(CURSOR_YOKE);
        yoke_control_enabled = 1;
        XPLMScheduleFlightLoop(loop_id, -1.0f, 0);
    }
    return 1;
}

int rudder_left_cb(XPLMCommandRef cmd, XPLMCommandPhase phase, void *ref) {
	if (yoke_control_enabled == 0 && bind_rudder == 1)
		return 1;
	if (rudder_control == 1) // ignore kbd input if mouse rudder control active
		return 1;

	// set the rudder position from kbd
	if (phase == xplm_CommandBegin) {
		yaw_ratio = -(kbd_rudder_speed);
		XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
		kbd_rudder_on = true;
	}
	else if (phase == xplm_CommandContinue)	{
		yaw_ratio = yaw_ratio - kbd_rudder_speed; //smooth deflection
		yaw_ratio < -1 ? yaw_ratio = -1 : 0;
		XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
	}
	else if (yoke_control_enabled == 0)	{ // this runs when loop call is not enabled
		//yaw_ratio = 0;
		//XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
		kbd_rudder_on = false;
		_last_time = get_time_ms();
		XPLMScheduleFlightLoop(loop_id, -1.0f, 0);
	}
	else {
		kbd_rudder_on = false;
		_last_time = get_time_ms();
	}
	return 1;
}

int rudder_right_cb(XPLMCommandRef cmd, XPLMCommandPhase phase, void *ref) {
	if (yoke_control_enabled == 0 && bind_rudder == 1)
		return 1;
	if (rudder_control == 1) // ignore kbd input if mouse rudder control active
		return 1;

	// set the rudder position from kbd
	if (phase == xplm_CommandBegin) {
		yaw_ratio = kbd_rudder_speed;
		XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
		kbd_rudder_on = true;
	}
	else if (phase == xplm_CommandContinue) {
		yaw_ratio = yaw_ratio + kbd_rudder_speed; // smooth deflection
		yaw_ratio > 1 ? yaw_ratio = 1 : 0;
		XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
	}
	else if (yoke_control_enabled == 0) { // this runs when loop call is not enabled
		//yaw_ratio = 0;
		//XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
		kbd_rudder_on = false;
		_last_time = get_time_ms();
		XPLMScheduleFlightLoop(loop_id, -1.0f, 0);
	}
	else {
		kbd_rudder_on = false;
		_last_time = get_time_ms();
	}
	return 1;
}

int draw_cb(XPLMDrawingPhase phase, int before, void *ref) {
    /* Show a little text indication in top left corner of screen. */
    if (yoke_control_enabled) {
        XPLMDrawString(magenta, 20, screen_height - 10, rudder_control ?
            "MOUSE RUDDER CONTROL" : "MOUSE YOKE CONTROL",
            NULL, xplmFont_Proportional);
        if (rudder_control) {
            /* Draw little bars to indicate maximum rudder deflection. */
            for (int i = 1; i < 3; i++) {
                XPLMDrawString(green, cursor_pos[0] - rudder_defl_dist,
                    cursor_pos[1] + 4 - 7 * i, "|", NULL, xplmFont_Basic);
                XPLMDrawString(green, cursor_pos[0] + rudder_defl_dist,
                    cursor_pos[1] + 4 - 7 * i, "|", NULL, xplmFont_Basic);
            }
        }
		else {
			/* Draw cross to indicate control centre */
			XPLMDrawString(green, screen_width/2,
				screen_height/2, "+", NULL, xplmFont_Basic);
		}
    }
    return 1;
}

float loop_cb(float last_call, float last_loop, int count, void *ref) {
    
    /* If user has disabled mouse yoke control, suspend loop. */
    if (yoke_control_enabled == 0) {

		/* Centre controls if selected */
		if (centre_control) {
			XPLMSetDataf(yoke_roll_ratio, 0);
			XPLMSetDataf(yoke_pitch_ratio, 0);
		}

        /* If rudder is still deflected, move it gradually back to zero. */
        if (yaw_ratio != 0 && rudder_return) {
            long long now = get_time_ms();
            float dt = (now - _last_time) / 1000.0f;
            _last_time = now;
            yaw_ratio = yaw_ratio > 0 ? max(0, yaw_ratio - dt * rudder_ret_spd) :
                min(0, yaw_ratio + dt * rudder_ret_spd);
            XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
            /* Call us again next frame until zero. */
            return yaw_ratio ? -1.0f : 0;
        }
        /* Don't call us anymore. */
        return 0;
    }
    int m_x, m_y;
    get_cursor_pos(&m_x, &m_y);
    if (controlling_rudder(&m_x, &m_y)) {
        int dist = min(max(m_x - cursor_pos[0], -rudder_defl_dist),
            rudder_defl_dist);
        _last_time = get_time_ms();
        /* Save value so we don't have to continuously query the dr above. */
        yaw_ratio = dist / (float)rudder_defl_dist;
        XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
    } 
	else {
        float yoke_roll = 2 * (m_x / (float)screen_width) - 1;
        float yoke_pitch = 1 - 2 * (m_y / (float)screen_height);

		// ignore if within null zone (default 0.05)
		if (yoke_roll > yoke_nz || yoke_roll < -yoke_nz) {
			XPLMSetDataf(yoke_roll_ratio, yoke_roll);
		} else {
			XPLMSetDataf(yoke_roll_ratio, 0);
		}
		if (yoke_pitch > yoke_nz || yoke_pitch < -yoke_nz) {
			XPLMSetDataf(yoke_pitch_ratio, yoke_pitch);
		} else {
			XPLMSetDataf(yoke_pitch_ratio, 0);
		}

        /* If rudder is still deflected, move it gradually back to zero. Ignore if keyboard rudder on. */
        if (yaw_ratio != 0 && rudder_return && !kbd_rudder_on) {
            long long now = get_time_ms();
            float dt = (now - _last_time) / 1000.0f;
            _last_time = now;
            yaw_ratio = yaw_ratio > 0 ? max(0, yaw_ratio - dt * rudder_ret_spd) :
                min(0, yaw_ratio + dt * rudder_ret_spd);
            XPLMSetDataf(yoke_heading_ratio, yaw_ratio);
        }
    }
    /* Call us again next frame. */
    return -1.0f;
}

int left_mouse_down() {
#ifdef IBM
    /* Most significant bit is set if button is being held. */
    return GetAsyncKeyState(VK_LBUTTON) >> 15;
#elif APL
    /* Apparently you can use this also outside of the context of an event. */
    return CGEventSourceButtonState(
        kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
#endif
}

int controlling_rudder(int *x, int *y) {
    if (left_mouse_down()) {
        /* Transitioning into rudder control */
        if (!rudder_control) {
            if (change_cursor)
                set_cursor_bmp(CURSOR_RUDDER);
            /* Remember current cursor position. */
            XPLMGetMouseLocationGlobal(cursor_pos, cursor_pos + 1);
            /* Set rudder cursor position, if enabled. */
            if (set_rudder_pos) {
                *x = *x + yaw_ratio * rudder_defl_dist;
                set_cursor_pos(*x, *y);
            }
            rudder_control = 1;
        }
    } else {
        /* Transitioning out of rudder control. */
        if (rudder_control) {
            if (change_cursor)
                set_cursor_bmp(CURSOR_YOKE);
            /* Restore previous cursor position */
            set_cursor_pos(cursor_pos[0], cursor_pos[1]);
            *x = cursor_pos[0];
            *y = cursor_pos[1];
            rudder_control = 0;
        }
    }
    return rudder_control;
}

void get_cursor_pos(int *x, int *y) {
#ifdef APL
    /* On OSX, XPLMGetMouseLocationGlobal still returns old cursor location after
        setting its position for whatever reason, so we query the cursor position
        ourselves. */
    CGEventRef ev = CGEventCreate(NULL);
    CGPoint pt = CGEventGetLocation(ev);
    CFRelease(ev);
    *x = pt.x;
    *y = screen_height - pt.y;
#else
    XPLMGetMouseLocationGlobal(x, y);
#endif
}

void set_cursor_from_yoke() {
    set_cursor_pos(
        0.5 * screen_width  * (XPLMGetDataf(yoke_roll_ratio) + 1),
        0.5 * screen_height * (1 - XPLMGetDataf(yoke_pitch_ratio))
    );
}

void set_cursor_pos(int x, int y) {
#ifdef IBM
    POINT pt = {
        .x = x,
        /* On windows (0,0) is the upper-left corner. */
        .y = screen_height - y
    };
    ClientToScreen(xp_hwnd, &pt);
    SetCursorPos(pt.x, pt.y);
#elif APL
    CGPoint pt = {
        .x = x,
        .y = screen_height - y
    };
    /* CGWarpMouseCursorPosition and CGDisplayMoveCursorToPoint don't generate a mouse
        movement event so they're not a good fit here. */
    CGEventRef ev = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, pt, 0);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
#endif
}

void set_cursor_bmp(cursor_t cursor) {
#ifdef IBM
    HCURSOR c = arrow_cursor;
    switch (cursor) {
    case CURSOR_YOKE:
        c = yoke_cursor;
        break;
    case CURSOR_RUDDER:
        c = rudder_cursor;
        break;
    }
    true_set_cursor(c);
#elif APL
    /* TODO */
    /* Can probably use NSCursor::set for this but not sure we can hook
       that under OSX to prevent XP from constantly overriding our cursor...*/
#endif
}

#ifdef IBM
HCURSOR WINAPI set_cursor(HCURSOR cursor) {
    if (!yoke_control_enabled)
        return true_set_cursor(cursor);
    return cursor;
}

int hook_set_cursor(int attach) {
    long err;
    if ((err = DetourTransactionBegin())) {
        _log("DetourTransactionBegin error (%i)", err);
        return 0;
    }
    if ((err = DetourUpdateThread(GetCurrentThread()))) {
        _log("DetourUpdateThread error (%i)", err);
        return 0;
    }
    if (attach) {
        if ((err = DetourAttach((void**)&true_set_cursor, set_cursor))) {
            _log("DetourAttach error (%i)", err);
            return 0;
        }
    } else {
        if ((err = DetourDetach((void**)&true_set_cursor, set_cursor))) {
            _log("DetourDetach error (%i)", err);
            return 0;
        }
    }
    if ((err = DetourTransactionCommit())) {
        _log("DetourTransactionCommit error (%i)", err);
        return 0;
    }
    return 1;
}
#endif
