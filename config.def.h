#pragma once

#define MOD Mod4Mask
#define ALTMOD Mod1Mask
#define NUM_WS 9
#define WIN_CAPACITY 10000
#define BORDER_FOCUS "#88c0d0"
#define BORDER_UNFOCUS "#444444"
#define BORDER_WIDTH 	3
#define BAR_HEIGHT 24 // Set to 0 if you don't use any bar
#define GAP 8

static const char *term_cmd[] = {"kitty", NULL};

// Volume commands
static const char *vol_up[] = {"wpctl", "set-volume","@DEFAULT_AUDIO_SINK@","5%+",NULL};
static const char *vol_down[] = {"wpctl","set-volume","@DEFAULT_AUDIO_SINK@","5%-",NULL};
static const char *vol_mute[] = {"wpctl","set-mute","@DEFAULT_AUDIO_SINK@","toggle",NULL};

// autostart commands
static const char *autostart_nitrogen[] = {"nitrogen", "--restore",NULL};
static const char *autostart_polybar[] = {"polybar",NULL};

static const char **autostart[] = {
	autostart_nitrogen,
	autostart_polybar,
	NULL
};
