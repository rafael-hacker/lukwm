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
static Display *d;
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

static void spawn(const char **cmd) {
    if (fork() == 0) {
        if (d) close(ConnectionNumber(d));
        setsid();
        execvp(cmd[0], (char **)cmd);
        exit(0);
    }
}

static void set_focus_border(Window w) {
	if (focused_win != None && focused_win != w) {
		XSetWindowBorder(d, focused_win, border_unfocus);
	}
	if (w != None) {
		XSetWindowBorder(d, w, border_focus);
	}
	focused_win = w;
}

static void init_colors(void) {
	Colormap cmap = DefaultColormap(d, DefaultScreen(d));
	XColor color;

	XAllocNamedColor(d, cmap, BORDER_FOCUS, &color, &color);
	border_focus = color.pixel;

	XAllocNamedColor(d, cmap, BORDER_UNFOCUS, &color, &color);
	border_unfocus = color.pixel;
}

static void restart_wm(char *self_path) {
	XCloseDisplay(d);
	execvp(self_path, (char *[]){self_path, NULL});
	exit(1);
}

static int is_dock(Window w) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	int result = 0;

	if (XGetWindowProperty(d, w, net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
		if (data) {
			Atom type = *(Atom *)data;
			if (type == net_wm_window_type_dock) result = 1;
			XFree(data);
		}
	}
	return result;
}

static int is_desktop(Window w) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data = NULL;
	int result = 0;

	if (XGetWindowProperty(d, w, net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success) {
		if (data) {
			Atom type = *(Atom *)data;
			if (type == net_wm_window_type_desktop) result = 1;
			XFree(data);
		}
	}
	return result;
}

static void set_active_window(Window w) {
	XChangeProperty(d, root, net_active_window, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

static void init_ewmh(void) {
	net_supported = XInternAtom(d, "_NET_SUPPORTED", False);
	net_wm_name = XInternAtom(d, "_NET_WM_NAME", False);
	net_wm_check = XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False);
	net_number_of_desktops = XInternAtom(d, "_NET_NUMBER_OF_DESKTOPS", False);
	net_current_desktop = XInternAtom(d, "_NET_CURRENT_DESKTOP", False);
	net_client_list = XInternAtom(d, "_NET_CLIENT_LIST", False);
	net_active_window = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
	net_wm_window_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
	net_wm_window_type_dock = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
	net_wm_window_type_desktop = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

	utf8_string = XInternAtom(d, "UTF8_STRING", False);

	Atom supported[] = {
		net_wm_name, net_wm_check, net_number_of_desktops, net_current_desktop, net_client_list, net_active_window
	};

	XChangeProperty(d, root, net_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported, sizeof(supported) / sizeof(Atom));

	Window check_win = XCreateSimpleWindow(d, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(d, check_win, net_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
	XChangeProperty(d, root, net_wm_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);

	XChangeProperty(d, check_win, net_wm_name, utf8_string, 8, PropModeReplace, (unsigned char *)"lukwm", 5);
	XChangeProperty(d, root, net_wm_name, utf8_string, 8, PropModeReplace, (unsigned char *)"lukwm", 5);

	long ndesktops = NUM_WS;
	XChangeProperty(d, root, net_number_of_desktops, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ndesktops, 1);

	long cur = current_ws = 1;
	XChangeProperty(d, root, net_current_desktop, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&cur, 1);
}

static void switch_ws(int ws) {
	int i;
	if (ws == current_ws) return;

	for (i = 0;i < win_count;i++) {
		if (win_list[i].ws == current_ws) {
			XUnmapWindow(d, win_list[i].win);
		} else if (win_list[i].ws == ws) {
			XMapWindow(d, win_list[i].win);
		}
	}
	current_ws = ws;
	long cur = current_ws - 1;
	XChangeProperty(d, root, net_current_desktop, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&cur, 1);
}

static void toggle_fullscreen(Window w) {
	if (w == None || w == root) return;

	if (fullscreen_win == w) {
		XMoveResizeWindow(d, w, fullscreen_saved.x, fullscreen_saved.y, fullscreen_saved.width, fullscreen_saved.height);
		XSetWindowBorderWidth(d, w, BORDER_WIDTH);
		fullscreen_win = None;
	} else {
		XGetWindowAttributes(d, w, &fullscreen_saved);
		int screen = DefaultScreen(d);
		int sw = DisplayWidth(d, screen);
		int sh = DisplayHeight(d, screen);
		XMoveResizeWindow(d, w, 0, 0, sw, sh);
		XSetWindowBorderWidth(d, w, 0);
		fullscreen_win = w;
	}
}

static void toggle_maximize(Window w) {
	if (w == None || w == root) return;

	if (maximized_win == w) {
        XMoveResizeWindow(d, w, maximized_saved.x, maximized_saved.y,
                           maximized_saved.width, maximized_saved.height);
        maximized_win = None;
    } else {
        XGetWindowAttributes(d, w, &maximized_saved);
        int screen = DefaultScreen(d);
        int sw = DisplayWidth(d, screen);
        int sh = DisplayHeight(d, screen);

        int x = GAP;
        int y = BAR_HEIGHT + GAP;
        int width = sw - (GAP * 2);
        int height = sh - BAR_HEIGHT - (GAP * 2);

        XMoveResizeWindow(d, w, x, y, width, height);
        maximized_win = w;
    }
}

static int has_delete_protocol(Window w) {
	Atom *protocols;
	int count, i, found = 0;

	if (XGetWMProtocols(d, w, &protocols, &count)) {
		for (i = 0;i < count;i++) {
			if (protocols[i] == wm_delete) found = 1;
		}
		XFree(protocols);
	}
	return found;
}

static void close_window(Window w) {
	if (has_delete_protocol(w)) {
		XEvent msg;
		msg.type = ClientMessage;
		msg.xclient.window = w;
		msg.xclient.message_type = wm_protocols;
		msg.xclient.format = 32;
		msg.xclient.data.l[0] = wm_delete;
		msg.xclient.data.l[1] = CurrentTime;
		XSendEvent(d, w, False, NoEventMask, &msg);
	} else {
		XKillClient(d, w);
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

static void buttonpress(XEvent *e) {
	XButtonEvent *ev = &e->xbutton;
	if (ev->subwindow == None) return;

	drag_win = ev->subwindow;
	drag_start_x = ev->x_root;
	drag_start_y = ev->y_root;
	XGetWindowAttributes(d, drag_win, &drag_start_attr);
	is_resize = (ev->button == Button3);

	XRaiseWindow(d, drag_win);
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
		XResizeWindow(d, drag_win, new_w, new_h);
	} else {
		XMoveWindow(d, drag_win, drag_start_attr.x + dx, drag_start_attr.y + dy);
	}
}

static void buttonrelease(XEvent *e) { drag_win = None; }

static void enternotify(XEvent *e) {
	XCrossingEvent *ev = &e->xcrossing;
	XSetInputFocus(d, ev->window, RevertToPointerRoot, CurrentTime);
	set_active_window(ev->window);
	set_focus_border(ev->window);
}

static void move_to_ws(int ws) {
	Window focused;
	int revert, i;

	if (ws == current_ws) return;

	XGetInputFocus(d, &focused, &revert);

	for (i = 0;i < win_count;i++) {
		if (win_list[i].win == focused) {
			win_list[i].ws = ws;
			XUnmapWindow(d, focused);
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
    if (ev->keycode == XKeysymToKeycode(d, XK_Q) && CLEANMASK(ev->state) == MOD) {
        spawn(term_cmd);
    }

    // Super + A Opens the menu launcher (edit menu_launcher on config.h)
    if (ev->keycode == XKeysymToKeycode(d, XK_A) && CLEANMASK(ev->state) == MOD) {
    	spawn(menu_launcher);
    }
    
    // Super + C Closes the current window
    if (ev->keycode == XKeysymToKeycode(d, XK_C) && CLEANMASK(ev->state) == MOD) {
	if (focused_win != None) {
		close_window(focused_win);
	}
    }

    if (ev->keycode == XKeysymToKeycode(d, XK_X) && CLEANMASK(ev->state) == MOD) {
    	spawn(browser);
    }

    if (ev->keycode == XKeysymToKeycode(d, XK_Tab) && CLEANMASK(ev->state) == ALTMOD) {
       if (win_count == 0) return;

	    int i, start_i = -1;

	    for (i = 0; i < win_count; i++) {
		if (win_list[i].win == focused_win) { start_i = i; break; }
	    }

	    for (i = 1; i <= win_count; i++) {
		int idx = (start_i + i) % win_count;
		if (win_list[idx].ws == current_ws) {
		    XRaiseWindow(d, win_list[idx].win);
		    XSetInputFocus(d, win_list[idx].win, RevertToPointerRoot, CurrentTime);
		    set_active_window(win_list[idx].win);
		    set_focus_border(win_list[idx].win);
		    break;
		}
	    }
    }

    if (ev->keycode == XKeysymToKeycode(d, XK_F) && CLEANMASK(ev->state) == MOD) {
    	Window focused;
	int revert;
	XGetInputFocus(d, &focused, &revert);
	toggle_fullscreen(focused);
    }

    if (CLEANMASK(ev->state) == MOD) {
    	KeySym ks = XkbKeycodeToKeysym(d, ev->keycode, 0, 0);
	if (ks >= XK_1 && ks <= XK_9) {
		switch_ws(ks - XK_1 + 1);
	}
    }

    if (ev->keycode == XKeysymToKeycode(d, XK_R) && CLEANMASK(ev->state) == (MOD|ShiftMask)) {
    	restart_wm(self_path);
    }

    if (CLEANMASK(ev->state) == (MOD|ShiftMask)) {
    	KeySym ks = XkbKeycodeToKeysym(d, ev->keycode, 0, 0);
	if (ks >= XK_1 && ks <= XK_9) {
		move_to_ws(ks - XK_1 + 1);
	}
    }

    if (ev->keycode == XKeysymToKeycode(d, XK_M) && CLEANMASK(ev->state) == MOD) {
	    toggle_maximize(focused_win);
    }

    if (ev->keycode == XKeysymToKeycode(d, XF86XK_AudioRaiseVolume)) {
        spawn(vol_up);
    }
    if (ev->keycode == XKeysymToKeycode(d, XF86XK_AudioLowerVolume)) {
        spawn(vol_down);
    }
    if (ev->keycode == XKeysymToKeycode(d, XF86XK_AudioMute)) {
        spawn(vol_mute);
    }
}

static void maprequest(XEvent *e) {
    static int window_counter = 0;
    int slot = window_counter % 10;
    XMapRequestEvent *ev = &e->xmaprequest;

    if (is_dock(ev->window)) {
    	XMapWindow(d, ev->window);
	return;
    }

    if (is_desktop(ev->window)) {
	int screen = DefaultScreen(d);
        XMoveResizeWindow(d, ev->window, 0, 0, DisplayWidth(d, screen), DisplayHeight(d, screen));
        XMapWindow(d, ev->window);
        XLowerWindow(d, ev->window);
        return;
    }

    XSetWindowBorderWidth(d, ev->window, BORDER_WIDTH);
    XMoveResizeWindow(d, ev->window, (50 + slot * 30), (50 + slot * 30), 600, 400);
    XMapWindow(d, ev->window);
    XSelectInput(d, ev->window, StructureNotifyMask | EnterWindowMask);
    XRaiseWindow(d, ev->window);
    XSetInputFocus(d, ev->window, RevertToPointerRoot, CurrentTime);
    set_active_window(ev->window);
    set_focus_border(ev->window);
    if (win_count < WIN_CAPACITY) {
    	win_list[win_count].win = ev->window;
	win_list[win_count].ws = current_ws;
	win_count++;
    }
    window_counter++;
}

// Shortcuts configs
static void input_grab(Window r) {
    XGrabKey(d, XKeysymToKeycode(d, XK_Return), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_Q), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_Tab), ALTMOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_F), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_R), MOD|ShiftMask, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_A), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_X), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_Q), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_M), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XK_C), MOD, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XF86XK_AudioRaiseVolume), AnyModifier, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XF86XK_AudioLowerVolume), AnyModifier, r, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, XKeysymToKeycode(d, XF86XK_AudioMute), AnyModifier, r, True, GrabModeAsync, GrabModeAsync);

    int i;
    for (i = XK_1;i <= XK_9;i++) {
    	XGrabKey(d, XKeysymToKeycode(d, i), MOD, r, True, GrabModeAsync, GrabModeAsync);
    }
    for (i = XK_1;i <= XK_9;i++) {
    	XGrabKey(d, XKeysymToKeycode(d, i), MOD|ShiftMask, r, True, GrabModeAsync, GrabModeAsync);
    }

    XGrabButton(d, Button1, MOD, r, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(d, Button3, MOD, r, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
}

static int xerror(Display *dpy, XErrorEvent *ee) {
    fprintf(stderr, "lukwm: Error detected! Error code: %d, Request: %d\n", ee->error_code, ee->request_code);
    return 0;
}

int main(int argc, char** argv) {
    XEvent ev;

    if (!(d = XOpenDisplay(NULL))) {
        fprintf(stderr, "lukwm: Couldn't open display.\n");
        exit(1);
    }

    signal(SIGCHLD, SIG_IGN);
    XSetErrorHandler(xerror);

    win_list = mmap(NULL, sizeof(Client) * WIN_CAPACITY, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (win_list == MAP_FAILED) {
	    fprintf(stderr, "lukwm: Failed to allocate window list\n");
	    exit(1);
    }
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len != -1) {
    	self_path[len] = '\0';
    } else {
    	strncpy(self_path, argv[0], sizeof(self_path) - 1);
    }
    int s = DefaultScreen(d);
    root = RootWindow(d, s);

    XSelectInput(d, root, SubstructureRedirectMask | SubstructureNotifyMask);
    input_grab(root);
    init_ewmh();
    init_colors();
    int i;
    for (i = 0; autostart[i] != NULL;i++) {
    	spawn(autostart[i]);
    }

    wm_protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);

    while (!XNextEvent(d, &ev)) {
        if (events[ev.type]) {
            events[ev.type](&ev);
        }
    }

    XCloseDisplay(d);
    return 0;
}
