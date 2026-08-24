#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "config.h"

#define CLEANMASK(mask) (mask & ~(LockMask|Mod2Mask))

typedef struct {
	Window win;
	int ws;
} Client;

// Global Variables
static Display *display;
static Window root;
static Window fullscreen_win = None;
static XWindowAttributes fullscreen_saved;
static Window drag_win = None;
static int drag_start_x, drag_start_y;
static XWindowAttributes drag_start_attr;
static int is_resize = 0;
static Atom wm_protocols, wm_delete;
static Atom net_supported, net_wm_name, net_wm_check;
static Atom net_number_of_desktops, net_current_desktop, net_client_list;
static Atom net_active_window;
static Atom net_wm_window_type, net_wm_window_type_dock;
static Atom net_wm_window_type_desktop;
static Atom net_wm_window_type_dialog;
static Atom utf8_string;
static Client *win_list;
static int win_count = 0;
static int current_ws = 1;
static char self_path[1024];
static unsigned long border_focus, border_unfocus;
static Window focused_win = None;
static Window maximized_win = None;
static XWindowAttributes maximized_saved;

static void keypress(XEvent *e);
static void maprequest(XEvent *e);
static void enternotify(XEvent *e);
static void buttonpress(XEvent *e);
static void motionnotify(XEvent *e);
static void buttonrelease(XEvent *e);
static void destroynotify(XEvent *e);

static void (*events[LASTEvent])(XEvent *e) = {
    [KeyPress] = keypress,
    [MapRequest] = maprequest,
    [EnterNotify] = enternotify,
    [ButtonPress] = buttonpress,
    [MotionNotify] = motionnotify,
    [ButtonRelease] = buttonrelease,
    [DestroyNotify] = destroynotify,
};

typedef enum {
  SNAP_LEFT, SNAP_RIGHT, SNAP_TOP, SNAP_BOTTOM,
  SNAP_TOPLEFT, SNAP_TOPRIGHT, SNAP_BOTTOMLEFT, SNAP_BOTTOMRIGHT
} SnapPosition;

static void spawn(const char **cmd) {
    if (fork() == 0) {
        if (display) close(ConnectionNumber(display));
        setsid();
        execvp(cmd[0], (char **)cmd);
        exit(0);
    }
}

static void set_focus_border(Window window) {
	if (focused_win != None && focused_win != window) {
		XSetWindowBorder(display, focused_win, border_unfocus);
	}
	if (window != None) {
		XSetWindowBorder(display, window, border_focus);
	}
	focused_win = window;
}

static void init_colors(void) {
	Colormap cmap = DefaultColormap(display, DefaultScreen(display));
	XColor color;

	XAllocNamedColor(display, cmap, BORDER_FOCUS, &color, &color);
	border_focus = color.pixel;

	XAllocNamedColor(display, cmap, BORDER_UNFOCUS, &color, &color);
	border_unfocus = color.pixel;
}

static void restart_wm(char *self_path) {
	XCloseDisplay(display);
	execvp(self_path, (char *[]){self_path, NULL});
	exit(1);
}

static int is_dock(Window window) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	int result = 0;

	if (XGetWindowProperty(display, window, net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
		if (data) {
			Atom type = *(Atom *)data;
			if (type == net_wm_window_type_dock) result = 1;
			XFree(data);
		}
	}
	return result;
}

static int is_desktop(Window window) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	int result = 0;

	if (XGetWindowProperty(display, window, net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
		if (data) {
			Atom type = *(Atom *)data;
			if (type == net_wm_window_type_desktop) result = 1;
			XFree(data);
		}
	}
	return result;
}

static int is_dialog(Window window) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	int result = 0;

	if (XGetWindowProperty(display, window, net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
		if (data) {
			Atom type = *(Atom *)data;
			if (type == net_wm_window_type_dialog) result = 1;
			XFree(data);
		}
	}
	return result;
}

static void set_active_window(Window window) {
	XChangeProperty(display, root, net_active_window, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&window, 1);
}

static void init_ewmh(void) {
	net_supported = XInternAtom(display, "_NET_SUPPORTED", False);
	net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
	net_wm_check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
	net_number_of_desktops = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
	net_current_desktop = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
	net_client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
	net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
	net_wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
	net_wm_window_type_dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
	net_wm_window_type_desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	net_wm_window_type_dialog = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);

	utf8_string = XInternAtom(display, "UTF8_STRING", False);

	Atom supported[] = {
		net_wm_name, net_wm_check, net_number_of_desktops, net_current_desktop, net_client_list, net_active_window
	};

	XChangeProperty(display, root, net_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported, sizeof(supported) / sizeof(Atom));

	Window check_win = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(display, check_win, net_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
	XChangeProperty(display, root, net_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);

	XChangeProperty(display, check_win, net_wm_name, utf8_string, 8, PropModeReplace, (unsigned char *)"lukwm", 5);
	XChangeProperty(display, root, net_wm_name, utf8_string, 8, PropModeReplace, (unsigned char *)"lukwm", 5);

	long ndesktops = NUM_WS;
	XChangeProperty(display, root, net_number_of_desktops, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ndesktops, 1);

	long cur = current_ws = 1;
	XChangeProperty(display, root, net_current_desktop, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&cur, 1);
}

static void switch_ws(int ws) {
	int i;
	if (ws == current_ws) return;

	for (i = 0;i < win_count;i++) {
		if (win_list[i].ws == current_ws) {
			XUnmapWindow(display, win_list[i].win);
		} else if (win_list[i].ws == ws) {
			XMapWindow(display, win_list[i].win);
		}
	}
	current_ws = ws;
	long cur = current_ws - 1;
	XChangeProperty(display, root, net_current_desktop, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&cur, 1);
	focused_win = None;
}

static void toggle_fullscreen(Window window) {
	if (window == None || window == root) return;

	if (fullscreen_win == window) {
		XMoveResizeWindow(display, window, fullscreen_saved.x, fullscreen_saved.y, fullscreen_saved.width, fullscreen_saved.height);
		XSetWindowBorderWidth(display, window, BORDER_WIDTH);
		fullscreen_win = None;
	} else {
		XGetWindowAttributes(display, window, &fullscreen_saved);
		int screen = DefaultScreen(display);
		int sw = DisplayWidth(display, screen);
		int sh = DisplayHeight(display, screen);
		XMoveResizeWindow(display, window, 0, 0, sw, sh);
		XSetWindowBorderWidth(display, window, 0);
		fullscreen_win = window;
	}
}

static void toggle_maximize(Window window) {
	if (window == None || window == root) return;

	if (maximized_win == window) {
        XMoveResizeWindow(display, window, maximized_saved.x, maximized_saved.y,
                           maximized_saved.width, maximized_saved.height);
        maximized_win = None;
    } else {
        XGetWindowAttributes(display, window, &maximized_saved);
        int screen = DefaultScreen(display);
        int sw = DisplayWidth(display, screen);
        int sh = DisplayHeight(display, screen);

        int x = GAP;
        int y = BAR_HEIGHT + GAP;
        int width = sw - (GAP * 2);
        int height = sh - BAR_HEIGHT - (GAP * 2);

        XMoveResizeWindow(display, window, x, y, width, height);
        maximized_win = window;
    }
}

static void snap_window(Window window, SnapPosition pos) {
  if (window == None || window == root) return;

  int screen = DefaultScreen(display);
  int sw = DisplayWidth(display, screen);
  int sh = DisplayHeight(display, screen);

  int usable_x = GAP;
  int usable_y = BAR_HEIGHT + GAP;
  int usable_w = sw - (GAP * 2);
  int usable_h = sh - BAR_HEIGHT - (GAP * 2);

  int half_w = (usable_w - GAP) / 2;
  int half_h = (usable_h - GAP) / 2;

  int x = usable_x, y = usable_y, width = usable_w, height = usable_h;
  switch (pos) {
        case SNAP_LEFT:        width = half_w; height = usable_h; break;
        case SNAP_RIGHT:       x = usable_x + half_w + GAP; width = half_w; height = usable_h; break;
        case SNAP_TOP:         width = usable_w; height = half_h; break;
        case SNAP_BOTTOM:      y = usable_y + half_h + GAP; width = usable_w; height = half_h; break;
        case SNAP_TOPLEFT:     width = half_w; height = half_h; break;
        case SNAP_TOPRIGHT:    x = usable_x + half_w + GAP; width = half_w; height = half_h; break;
        case SNAP_BOTTOMLEFT:  y = usable_y + half_h + GAP; width = half_w; height = half_h; break;
        case SNAP_BOTTOMRIGHT: x = usable_x + half_w + GAP; y = usable_y + half_h + GAP; width = half_w; height = half_h; break;
    }

    XMoveResizeWindow(display, window, x, y, width, height);
}

static int has_delete_protocol(Window window) {
	Atom *protocols;
	int count, i, found = 0;

	if (XGetWMProtocols(display, window, &protocols, &count)) {
		for (i = 0;i < count;i++) {
			if (protocols[i] == wm_delete) found = 1;
		}
		XFree(protocols);
	}
	return found;
}

static void close_window(Window window) {
	if (has_delete_protocol(window)) {
		XEvent msg;
		msg.type = ClientMessage;
		msg.xclient.window = window;
		msg.xclient.message_type = wm_protocols;
		msg.xclient.format = 32;
		msg.xclient.data.l[0] = wm_delete;
		msg.xclient.data.l[1] = CurrentTime;
		XSendEvent(display, window, False, NoEventMask, &msg);
	} else {
		XKillClient(display, window);
	}
}

static void destroynotify(XEvent *e) {
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	int i;

	for (i = 0;i < win_count;i++) {
		if (win_list[i].win == ev->window) {
			for (; i < win_count - 1;i++) {
				win_list[i] = win_list[i + 1];
			}
			win_count--;
			if (ev->window == focused_win) {
				set_active_window(None);
				set_focus_border(None);
			}
			if (ev->window == fullscreen_win) fullscreen_win = None;
			if (ev->window == maximized_win) maximized_win = None;
			break;
		}
	}
}

static void buttonpress(XEvent *e) 
{
  XButtonEvent *ev = &e->xbutton;
  if (ev->subwindow == None) return;

  XWindowAttributes wa;
  if (XGetWindowAttributes(display, ev->subwindow, &wa) && wa.override_redirect) return;

  if (is_dock(ev->subwindow) || is_desktop(ev->subwindow) || is_dialog(ev->subwindow)) return;

  drag_win = ev->subwindow;
  drag_start_x = ev->x_root;
  drag_start_y = ev->y_root;
  XGetWindowAttributes(display, drag_win, &drag_start_attr);
  is_resize = (ev->button == Button3);

  XRaiseWindow(display, drag_win);
}

static void motionnotify(XEvent *e)
{
	if (drag_win == None) return;

	XMotionEvent *ev = &e->xmotion;
	int dx = ev->x_root - drag_start_x;
	int dy = ev->y_root - drag_start_y;

	if (is_resize) {
		int new_w = drag_start_attr.width + dx;
		int new_h = drag_start_attr.height + dy;
		if (new_w < 40) new_w = 40;
		if (new_h < 40) new_h = 40;
		XResizeWindow(display, drag_win, new_w, new_h);
	} else {
		XMoveWindow(display, drag_win, drag_start_attr.x + dx, drag_start_attr.y + dy);
	}
}

static void buttonrelease(XEvent *e) {
    if (drag_win != None && !is_resize) {
        int screen = DefaultScreen(display);
        int sw = DisplayWidth(display, screen);
        int sh = DisplayHeight(display, screen);

        Window root_ret, child_ret;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(display, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask);

        int at_left   = root_x <= SNAP_THRESHOLD;
        int at_right  = root_x >= sw - SNAP_THRESHOLD;
        int at_top    = root_y <= SNAP_THRESHOLD;
        int at_bottom = root_y >= sh - SNAP_THRESHOLD;

        if (at_top && at_left)          snap_window(drag_win, SNAP_TOPLEFT);
        else if (at_top && at_right)    snap_window(drag_win, SNAP_TOPRIGHT);
        else if (at_bottom && at_left)  snap_window(drag_win, SNAP_BOTTOMLEFT);
        else if (at_bottom && at_right) snap_window(drag_win, SNAP_BOTTOMRIGHT);
        else if (at_left)               snap_window(drag_win, SNAP_LEFT);
        else if (at_right)              snap_window(drag_win, SNAP_RIGHT);
        else if (at_top)                snap_window(drag_win, SNAP_TOP);
        else if (at_bottom)             snap_window(drag_win, SNAP_BOTTOM);
    }

    drag_win = None;
}

static void enternotify(XEvent *e) {
	XCrossingEvent *ev = &e->xcrossing;
	XSetInputFocus(display, ev->window, RevertToPointerRoot, CurrentTime);
	set_active_window(ev->window);
	set_focus_border(ev->window);
}

static void move_to_ws(int ws) {
	Window focused;
	int revert, i;

	if (ws == current_ws) return;

	XGetInputFocus(display, &focused, &revert);

	for (i = 0;i < win_count;i++) {
		if (win_list[i].win == focused) {
			win_list[i].ws = ws;
			XUnmapWindow(display, focused);
			set_active_window(None);
			set_focus_border(None);
			break;
		}
	}
	switch_ws(ws);
}

static void keypress(XEvent *e) {
    XKeyPressedEvent *ev = &e->xkey;

    // Super + Q opens terminal (edit term_cmd on config.h)
    if (ev->keycode == XKeysymToKeycode(display, XK_Q) && CLEANMASK(ev->state) == MOD) {
        spawn(term_cmd);
    }

    // Super + A Opens the menu launcher (edit menu_launcher on config.h)
    if (ev->keycode == XKeysymToKeycode(display, XK_A) && CLEANMASK(ev->state) == MOD) {
    	spawn(menu_launcher);
    }
    
    // Super + C Closes the current window
    if (ev->keycode == XKeysymToKeycode(display, XK_C) && CLEANMASK(ev->state) == MOD) {
	if (focused_win != None) {
		close_window(focused_win);
	}
    }

    if (ev->keycode == XKeysymToKeycode(display, XK_X) && CLEANMASK(ev->state) == MOD) {
    	spawn(browser);
    }

    if (ev->keycode == XKeysymToKeycode(display, XK_Tab) && CLEANMASK(ev->state) == ALTMOD) {
       if (win_count == 0) return;

	    int i, start_i = -1;

	    for (i = 0; i < win_count; i++) {
		if (win_list[i].win == focused_win) { start_i = i; break; }
	    }

	    for (i = 1; i <= win_count; i++) {
		int idx = (start_i + i) % win_count;
		if (win_list[idx].ws == current_ws) {
		    XRaiseWindow(display, win_list[idx].win);
		    XSetInputFocus(display, win_list[idx].win, RevertToPointerRoot, CurrentTime);
		    set_active_window(win_list[idx].win);
		    set_focus_border(win_list[idx].win);
		    break;
		}
	    }
    }

    if (ev->keycode == XKeysymToKeycode(display, XK_F) && CLEANMASK(ev->state) == MOD) {
    Window focused;
	int revert;
	XGetInputFocus(display, &focused, &revert);
	toggle_fullscreen(focused);
    }

    if (CLEANMASK(ev->state) == MOD) {
    	KeySym ks = XkbKeycodeToKeysym(display, ev->keycode, 0, 0);
	if (ks >= XK_1 && ks <= XK_9) {
		switch_ws(ks - XK_1 + 1);
	}
    }

    if (ev->keycode == XKeysymToKeycode(display, XK_R) && CLEANMASK(ev->state) == (MOD|ShiftMask)) {
    	restart_wm(self_path);
    }

    if (CLEANMASK(ev->state) == (MOD|ShiftMask)) {
    	KeySym ks = XkbKeycodeToKeysym(display, ev->keycode, 0, 0);
      if (ks >= XK_1 && ks <= XK_9) {
        move_to_ws(ks - XK_1 + 1);
      }
    }

    if (CLEANMASK(ev->state) == MOD) {
      KeySym ks = XkbKeycodeToKeysym(display, ev->keycode, 0, 0);
      switch (ks) {
        case XK_Left:  snap_window(focused_win, SNAP_LEFT); break;
        case XK_Right: snap_window(focused_win, SNAP_RIGHT); break;
        case XK_Up:    snap_window(focused_win, SNAP_TOP); break;
        case XK_Down:  snap_window(focused_win, SNAP_BOTTOM); break;
        case XK_Home:  snap_window(focused_win, SNAP_TOPLEFT); break;
        case XK_Prior: snap_window(focused_win, SNAP_TOPRIGHT); break;
        case XK_End:   snap_window(focused_win, SNAP_BOTTOMLEFT); break;
        case XK_Next:  snap_window(focused_win, SNAP_BOTTOMRIGHT); break;
      }
    }

    if (ev->keycode == XKeysymToKeycode(display, XK_M) && CLEANMASK(ev->state) == MOD) {
	    toggle_maximize(focused_win);
    }

    if (ev->keycode == XKeysymToKeycode(display, XF86XK_AudioRaiseVolume)) {
        spawn(vol_up);
    }
    if (ev->keycode == XKeysymToKeycode(display, XF86XK_AudioLowerVolume)) {
        spawn(vol_down);
    }
    if (ev->keycode == XKeysymToKeycode(display, XF86XK_AudioMute)) {
        spawn(vol_mute);
    }

    if (ev->keycode == XKeysymToKeycode(display, XF86XK_MonBrightnessUp)) {
        spawn(bright_up);
    }

    if (ev->keycode == XKeysymToKeycode(display, XF86XK_MonBrightnessDown)) {
        spawn(bright_down);
    }
}

static void maprequest(XEvent *e) {
    static int window_counter = 0;
    int slot = window_counter % 10;
    XMapRequestEvent *ev = &e->xmaprequest;

    if (is_dock(ev->window)) {
    	XMapWindow(display, ev->window);
	return;
    }

    if (is_desktop(ev->window)) {
	int screen = DefaultScreen(display);
        XMoveResizeWindow(display, ev->window, 0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen));
        XMapWindow(display, ev->window);
        XLowerWindow(display, ev->window);
        return;
    }

    if (is_dialog(ev->window)) {
	    XSetWindowBorderWidth(display, ev->window, 0);
	    XMapWindow(display, ev->window);
	    XSelectInput(display, ev->window, StructureNotifyMask | EnterWindowMask);
	    XRaiseWindow(display, ev->window);
	    XSetInputFocus(display, ev->window, RevertToPointerRoot, CurrentTime);
	    set_active_window(ev->window);
	    set_focus_border(ev->window);
	    if (win_count < WIN_CAPACITY) {
		win_list[win_count].win = ev->window;
		win_list[win_count].ws = current_ws;
		win_count++;
	    }
	    window_counter++;
	    return;
    }

    XSetWindowBorderWidth(display, ev->window, BORDER_WIDTH);
    XMoveResizeWindow(display, ev->window, (50 + slot * 30), (50 + slot * 30), 600, 400);
    XMapWindow(display, ev->window);
    XSelectInput(display, ev->window, StructureNotifyMask | EnterWindowMask);
    XRaiseWindow(display, ev->window);
    XSetInputFocus(display, ev->window, RevertToPointerRoot, CurrentTime);
    set_active_window(ev->window);
    set_focus_border(ev->window);
    if (win_count < WIN_CAPACITY) {
    	win_list[win_count].win = ev->window;
	win_list[win_count].ws = current_ws;
	win_count++;
    }
    window_counter++;
}

static void grab_key(KeySym keysym, unsigned int mod, Window window) {
  unsigned int modifiers[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask };
  KeyCode code = XKeysymToKeycode(display, keysym);

  if (code == 0) return;

  for (int i = 0;i < 4;i++) {
    XGrabKey(display, code, mod | modifiers[i], window, True, GrabModeAsync, GrabModeAsync);
  }
}

// Shortcuts configs
static void input_grab(Window window) {
    grab_key(XK_Return, MOD, window);
    grab_key(XK_Q, MOD, window);
    grab_key(XK_Tab, ALTMOD, window);
    grab_key(XK_F, MOD, window);
    grab_key(XK_R, MOD | ShiftMask, window);
    grab_key(XK_A, MOD, window);
    grab_key(XK_X, MOD, window);
    grab_key(XK_M, MOD, window);
    grab_key(XK_C, MOD, window);
    
    XGrabKey(display, XKeysymToKeycode(display, XF86XK_AudioRaiseVolume), AnyModifier, window, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XF86XK_AudioLowerVolume), AnyModifier, window, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XF86XK_AudioMute), AnyModifier, window, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XF86XK_MonBrightnessUp), AnyModifier, window, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XF86XK_MonBrightnessDown), AnyModifier, window, True, GrabModeAsync, GrabModeAsync);
    
    grab_key(XK_Left, MOD, window);
    grab_key(XK_Right, MOD, window);
    grab_key(XK_Up, MOD, window);
    grab_key(XK_Down, MOD, window);
    grab_key(XK_Home, MOD, window);
    grab_key(XK_Prior, MOD, window);
    grab_key(XK_End, MOD, window);
    grab_key(XK_Next, MOD, window);

    int i;
    for (i = XK_1; i <= XK_9; i++) {
        grab_key(i, MOD, window);
        grab_key(i, MOD | ShiftMask, window);
    }

    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
    for (i = 0; i < 4; i++) {
        XGrabButton(display, Button1, MOD | modifiers[i], window, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(display, Button3, MOD | modifiers[i], window, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    }
}

static int xerror(Display *dpy, XErrorEvent *ee) {
    fprintf(stderr, "lukwm: Error detected! Error code: %d, Request: %d\n", ee->error_code, ee->request_code);
    return 0;
}

int main(int argc, char **argv) {
    XEvent ev;

    if (!(display = XOpenDisplay(NULL))) {
        fprintf(stderr, "lukwm: Couldn't open display.\n");
        exit(1);
    }

    signal(SIGCHLD, SIG_IGN);
    XSetErrorHandler(xerror);

    win_list = malloc(sizeof(Client) * WIN_CAPACITY);
    if (win_list == NULL) {
	    fprintf(stderr, "lukwm: Failed to allocate window list\n");
	    exit(1);
    }
    if (win_count > WIN_CAPACITY){
       Client *tmp =  realloc(win_list, win_count * sizeof(Client));
       if (tmp == NULL){
            fprintf(stderr, "lukwm: Failed to reallocate window list");
            exit(1);
        }
        win_list = tmp;
    }
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len != -1) {
    	self_path[len] = '\0';
    } else {
    	strncpy(self_path, argv[0], sizeof(self_path) - 1);
    }
    int screen = DefaultScreen(display);
    root = RootWindow(display, screen);

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask);
    input_grab(root);
    init_ewmh();
    init_colors();
    int i;
    for (i = 0; autostart[i] != NULL;i++) {
    	spawn(autostart[i]);
    }

    wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);

    while (!XNextEvent(display, &ev)) {
        if (events[ev.type]) {
            events[ev.type](&ev);
        }
    }

    XCloseDisplay(display);
    return 0;
}
