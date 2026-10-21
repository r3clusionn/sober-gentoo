/* sober-pointer: right mouse speed, screen edges and shift-lock for Sober's
 * HiDPI mode on X11.
 *
 * With "enable_hidpi" on, Sober draws Roblox at the display scale (Xft.dpi/96,
 * 2 here) but X11 hands it pointer positions in real pixels, which it scales
 * again: Roblox's cursor moves twice as fast as the desktop's and runs off the
 * right and bottom of the window. While Sober's game window has focus, this
 *  - divides the pointers' libinput speed factor by the scale (only motion is
 *    scaled; a Coordinate Transformation Matrix would also rescale the
 *    position the first time a pointer moves after any warp),
 *  - keeps the pointer inside the part of the window Sober maps onto the
 *    whole of it, with XFixes pointer barriers,
 *  - maps the pointer into that part when focus arrives and back out when it
 *    leaves, so Roblox's cursor and the desktop's stay in the same place.
 *
 * Shift-lock: Sober turns the camera by the pointer's own movement and never
 * recentres it, and shows no sign of the lock to other clients (no grab, same
 * cursor). Roblox does draw its shift-lock crosshair in the middle of the
 * screen, so 150 ms after every Shift press (XInput2 raw key events, no grab)
 * and every 2 s while Sober has focus, a 69x69 square there is read and the
 * ring looked for: white at radius ~23 with black edges inside and out.
 * While locked the barriers are off; on unlock the pointer goes to the middle of Roblox's screen, as on
 * Windows. A lock counts as on after one check sees the crosshair, as off
 * after two in a row 0.3 s apart don't (the score dips while turning). When a
 * lock starts or ends SDL also puts the pointer on the window's exact centre
 * in pixels, which Sober shows at the bottom-right corner; while unlocked,
 * that is moved to the middle too, and while locked, within 0.6 s of a Shift,
 * it ends the lock at once (about 30 ms after the key, not the crosshair's 0.45 s). First person (a lock without the
 * crosshair) isn't followed.
 *
 * Started by the flatpak launcher for every Sober run; one instance at a
 * time; exits 5 s after the last Sober window closes (or after 3 minutes if
 * none appears), and does nothing if Sober's HiDPI setting is off. If it is
 * killed with SIGKILL the pointer speed stays scaled: `accel flat 0.7`.
 *
 * Build: cc -O2 -o sober-pointer sober-pointer.c $(pkg-config --cflags --libs x11 xi xfixes) -lm
 */
#define _GNU_SOURCE
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <X11/keysym.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#define MAXDEV 32
#define ICON_DELAY 0.15   /* s after Shift: Roblox has drawn the crosshair or removed it */
#define ICON_PERIOD 2.0   /* s between checks while Sober has focus */

static Display *d;
static Window root;
static Atom a_active, a_clients, a_speed, a_float;
static int xi_opcode;
static double scale = 2;
static volatile sig_atomic_t quit;

static struct { int id; float speed; } dev[MAXDEV];
static int ndev;
static PointerBarrier bar[4];
static int have_bars;
static int engaged, bx, by, bw, bh, sw, sh; /* the window's root geometry while engaged; the screen */
static Window game;

enum { NONE, CROSSHAIR }; /* why the mouse counts as locked */
static int locked;
static double check_at, last_check_icon; /* next and last look for the crosshair */
static int second_check;   /* look once more 0.3 s after the first check after Shift */
static int off_votes;      /* checks in a row without the crosshair while locked */
static double last_shift;  /* SDL centres the pointer ~30 ms after the Shift that ends a lock */
static double locked_since; /* ...and ~250 ms after the one that starts it: not an unlock */

static FILE *dbg; /* SOBER_POINTER_LOG=path */
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}
#define LOG(...) do { if (dbg) { fprintf(dbg, "%.3f ", now()); fprintf(dbg, __VA_ARGS__); fflush(dbg); } } while (0)

static void on_signal(int s) { (void)s; quit = 1; }

/* Sober's own setting, from its config file (comments allowed before the JSON) */
static int hidpi_enabled(void)
{
	char path[4096], buf[8192];
	snprintf(path, sizeof path, "%s/.var/app/org.vinegarhq.Sober/config/sober/config.json", getenv("HOME"));
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	char *k = strstr(buf, "\"enable_hidpi\"");
	return k && strstr(k, "true") && (!strstr(k, "false") || strstr(k, "true") < strstr(k, "false"));
}

/* the scale SDL reports on X11: Xft.dpi / 96 */
static void read_scale(void)
{
	char *rm = XResourceManagerString(d), *p;
	if (rm && (p = strstr(rm, "Xft.dpi:"))) {
		double dpi = atof(p + 8);
		if (dpi >= 96) scale = dpi / 96;
	}
}

static int is_game(Window w)
{
	XClassHint h;
	int yes = 0;
	if (w && XGetClassHint(d, w, &h)) {
		yes = h.res_name && !strcmp(h.res_name, "sober"); /* not sober_services' windows */
		XFree(h.res_name);
		XFree(h.res_class);
	}
	return yes;
}

static Window active_window(void)
{
	Atom type;
	int fmt;
	unsigned long n, after;
	unsigned char *v = NULL;
	Window w = None;
	if (XGetWindowProperty(d, root, a_active, 0, 1, False, XA_WINDOW, &type, &fmt, &n, &after, &v) == Success &&
	    v && n)
		w = *(Window *)v;
	if (v) XFree(v);
	return w;
}

static int sober_running(void)
{
	Atom type;
	int fmt, found = 0;
	unsigned long n, after;
	unsigned char *v = NULL;
	if (XGetWindowProperty(d, root, a_clients, 0, 4096, False, XA_WINDOW, &type, &fmt, &n, &after, &v) == Success && v)
		for (unsigned long i = 0; i < n && !found; i++) found = is_game(((Window *)v)[i]);
	if (v) XFree(v);
	return found;
}

static void warp(int x, int y)
{
	XWarpPointer(d, None, root, 0, 0, 0, 0, x, y);
	XFlush(d);
}

static void pointer(int *x, int *y)
{
	Window rw, cw;
	int wx, wy;
	unsigned mask;
	if (!XQueryPointer(d, root, &rw, &cw, x, y, &wx, &wy, &mask)) *x = *y = -1;
}

/* XI2 properties take format-32 data as packed 32-bit values, unlike core ones */
static void set_speed(int id, float s)
{
	XIChangeProperty(d, id, a_speed, a_float, 32, PropModeReplace, (unsigned char *)&s, 1);
}

/* With libinput's flat profile a movement is scaled by 1 + speed, so 1/scale
 * of it is (1 + s) / scale - 1; adaptive gets the same change. */
static void slow_pointers(void)
{
	int n;
	ndev = 0;
	XIDeviceInfo *info = XIQueryDevice(d, XIAllDevices, &n);
	for (int i = 0; i < n && ndev < MAXDEV; i++) {
		if (info[i].use != XISlavePointer || strstr(info[i].name, "XTEST")) continue; /* not xdotool & co */
		Atom type;
		int fmt;
		unsigned long items, after;
		unsigned char *v = NULL;
		if (XIGetProperty(d, info[i].deviceid, a_speed, 0, 1, False, a_float, &type, &fmt, &items, &after, &v) ==
			    Success && items == 1) {
			dev[ndev].id = info[i].deviceid;
			memcpy(&dev[ndev].speed, v, sizeof(float));
			float s = (float)((1 + dev[ndev].speed) / scale - 1);
			set_speed(dev[ndev].id, s < -1 ? -1 : s);
			ndev++;
		}
		if (v) XFree(v);
	}
	XIFreeDeviceInfo(info);
}

static void restore_pointers(void)
{
	for (int i = 0; i < ndev; i++) set_speed(dev[i].id, dev[i].speed);
	ndev = 0;
}

/* Each barrier lets the pointer into the box but not out; they run the whole
 * width or height of the screen so no diagonal move slips past a corner. */
static void make_barriers(void)
{
	if (have_bars) return;
	int r = bx + (int)(bw / scale), b = by + (int)(bh / scale);
	bar[0] = XFixesCreatePointerBarrier(d, root, bx, 0, bx, sh, BarrierPositiveX, 0, NULL);
	bar[1] = XFixesCreatePointerBarrier(d, root, r, 0, r, sh, BarrierNegativeX, 0, NULL);
	bar[2] = XFixesCreatePointerBarrier(d, root, 0, by, sw, by, BarrierPositiveY, 0, NULL);
	bar[3] = XFixesCreatePointerBarrier(d, root, 0, b, sw, b, BarrierNegativeY, 0, NULL);
	have_bars = 1;
}

static void drop_barriers(void)
{
	if (!have_bars) return;
	for (int i = 0; i < 4; i++) XFixesDestroyPointerBarrier(d, bar[i]);
	have_bars = 0;
}

/* focus: focus just arrived (not a move or resize of the window) */
static void engage(Window w, int focus)
{
	XWindowAttributes wa;
	Window child;
	if (!XGetWindowAttributes(d, w, &wa) || wa.map_state != IsViewable) return;
	XTranslateCoordinates(d, w, root, 0, 0, &bx, &by, &child);
	if (bx < 0 || by < 0) return; /* on a hidden dwm tag */
	bw = wa.width;
	bh = wa.height;
	game = w;
	engaged = 1;
	slow_pointers();
	int px, py;
	pointer(&px, &py);
	/* Roblox's cursor goes where the desktop's was (not while locked: any
	 * pointer motion then turns the camera) */
	if (focus && !locked && px >= bx && py >= by && px < bx + bw && py < by + bh)
		warp(bx + (int)((px - bx) / scale), by + (int)((py - by) / scale));
	if (!locked) make_barriers();
	XSelectInput(d, w, StructureNotifyMask);
	XSync(d, False);
	LOG("engage 0x%lx at %d,%d %dx%d%s, pointer %d,%d\n", w, bx, by, bw, bh, locked ? " locked" : "", px, py);
}

/* focus: focus is leaving (not a move or resize of the window) */
static void disengage(int focus)
{
	if (!engaged) return;
	drop_barriers();
	restore_pointers();
	int px, py;
	pointer(&px, &py);
	/* the desktop's cursor goes where Roblox's was */
	if (focus && !locked && px >= bx && py >= by)
		warp(bx + (int)((px - bx) * scale), by + (int)((py - by) * scale));
	XSync(d, False);
	engaged = 0;
	check_at = 0;
	LOG("disengage%s\n", focus ? "" : " (geometry)");
}

static void set_lock(int how, const char *why)
{
	if (how == locked) return;
	int was = locked;
	locked = how;
	if (how) locked_since = now();
	LOG("lock %s (%s)\n", how ? "on" : "off", why);
	if (!engaged) return;
	if (how)
		drop_barriers();
	else if (was) { /* Roblox's cursor back in the middle, as on Windows */
		warp(bx + (int)(bw / 2 / scale), by + (int)(bh / 2 / scale));
		make_barriers();
		XSync(d, False);
	}
}

/* Is Roblox's shift-lock crosshair in the middle of the window? */
static int crosshair(void)
{
	enum { R = 34 };
	int cx = bx + bw / 2, cy = by + bh / 2;
	XImage *im = XGetImage(d, root, cx - R, cy - R, 2 * R + 1, 2 * R + 1, AllPlanes, ZPixmap);
	if (!im) return -1;
	unsigned long mk[3] = { im->red_mask, im->green_mask, im->blue_mask };
	int sh_[3], bits[3];
	for (int c = 0; c < 3; c++) {
		unsigned long m = mk[c];
		for (sh_[c] = 0; m && !(m & 1); m >>= 1) sh_[c]++;
		for (bits[c] = 0; m & 1; m >>= 1) bits[c]++;
	}
	int hits = 0;
	for (int i = 0; i < 16; i++) {
		double a = (i + 0.5) * M_PI / 8; /* off the axes, where the crosshair's ticks are */
		int bright = 0, dark_in = 0, dark_out = 0;
		for (int r = 15; r <= 31; r++) {
			int x = R + (int)lround(r * cos(a)), y = R + (int)lround(r * sin(a));
			unsigned long px = XGetPixel(im, x, y);
			int v = 255; /* darkest channel, in 8 bits */
			for (int c = 0; c < 3; c++) {
				int ch = (int)(((px & mk[c]) >> sh_[c]) * 255 / ((1UL << bits[c]) - 1));
				if (ch < v) v = ch;
			}
			if (r >= 21 && r <= 25 && v >= 170) bright = 1;
			if (r <= 18 && v <= 90) dark_in = 1;
			if (r >= 28 && v <= 90) dark_out = 1;
		}
		hits += bright && dark_in && dark_out;
	}
	XDestroyImage(im);
	LOG("crosshair score %d/16\n", hits);
	return hits >= 11;
}

static void check_crosshair(const char *why)
{
	check_at = 0;
	last_check_icon = now();
	if (second_check) { /* Roblox may not have drawn the change yet */
		second_check = 0;
		check_at = last_check_icon + 0.3;
	}
	if (!engaged) return;
	int on = crosshair();
	if (on < 0) return;
	if (on) {
		off_votes = 0;
		set_lock(CROSSHAIR, why);
	} else if (locked && ++off_votes < 2) { /* the score dips while turning: look again */
		if (!check_at) check_at = now() + 0.3;
	} else {
		off_votes = 0;
		set_lock(NONE, why);
	}
}

/* Every 10 ms while engaged and unlocked. When the mouse lock starts or ends,
 * SDL puts the pointer on the window's exact centre in pixels (a pixel the
 * barriers keep motion from reaching), which Sober shows at the bottom-right
 * corner; move it to the middle of the box instead. */
static void check_centre(void)
{
	int px, py;
	pointer(&px, &py);
	if (px != bx + bw / 2 || py != by + bh / 2) return;
	int nx = bx + (int)(bw / 2 / scale), ny = by + (int)(bh / 2 / scale);
	LOG("pointer centred by Sober, moved to %d,%d\n", nx, ny);
	warp(nx, ny);
}

static void raw_key(int keycode)
{
	KeySym k = XkbKeycodeToKeysym(d, keycode, 0, 0);
	if (engaged && (k == XK_Shift_L || k == XK_Shift_R)) {
		last_shift = now();
		check_at = last_shift + ICON_DELAY;
		second_check = 1;
	}
}

static int ignore_errors(Display *dpy, XErrorEvent *e) { (void)dpy; (void)e; return 0; }

int main(void)
{
	if (!hidpi_enabled()) return 0;
	char lock[4096];
	snprintf(lock, sizeof lock, "%s/sober-pointer.lock", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp");
	int lfd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lfd < 0 || flock(lfd, LOCK_EX | LOCK_NB)) return 0; /* already running */
	if (getenv("SOBER_POINTER_LOG")) dbg = fopen(getenv("SOBER_POINTER_LOG"), "a");
	if (!(d = XOpenDisplay(NULL))) return 1;
	XSetErrorHandler(ignore_errors); /* windows vanish between lookups */
	int ev0, err0;
	if (!XQueryExtension(d, "XInputExtension", &xi_opcode, &ev0, &err0)) return 1;
	root = DefaultRootWindow(d);
	sw = DisplayWidth(d, DefaultScreen(d));
	sh = DisplayHeight(d, DefaultScreen(d));
	a_active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
	a_clients = XInternAtom(d, "_NET_CLIENT_LIST", False);
	a_speed = XInternAtom(d, "libinput Accel Speed", False);
	a_float = XInternAtom(d, "FLOAT", False);
	read_scale();
	if (scale < 1.01) return 0;

	struct sigaction sa = { .sa_handler = on_signal };
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	XSelectInput(d, root, PropertyChangeMask);
	unsigned char m[XIMaskLen(XI_LASTEVENT)] = {0};
	XISetMask(m, XI_RawKeyPress);
	XIEventMask em = { XIAllMasterDevices, sizeof m, m };
	XISelectEvents(d, root, &em, 1);

	double start = now(), last_seen = 0, last_check = 0;
	int recheck = 1;
	while (!quit) {
		double t = now();
		if (recheck || t - last_check >= 1) { /* focus changes, and once a second: is Sober still there */
			Window a = active_window();
			int want = is_game(a);
			if (engaged && (!want || a != game)) disengage(1);
			if (want && !engaged) {
				engage(a, 1);
				if (engaged) check_at = t + ICON_DELAY;
			}
			if (sober_running()) last_seen = t;
			last_check = t;
			recheck = 0;
		}
		if (last_seen ? t - last_seen > 5 : t - start > 180) break;
		while (XPending(d)) {
			XEvent ev;
			XNextEvent(d, &ev);
			if (ev.type == GenericEvent && ev.xcookie.extension == xi_opcode && XGetEventData(d, &ev.xcookie)) {
				XIRawEvent *re = ev.xcookie.data;
				if (ev.xcookie.evtype == XI_RawKeyPress) raw_key(re->detail);
				XFreeEventData(d, &ev.xcookie);
			} else if (ev.type == PropertyNotify && (ev.xproperty.atom == a_active || ev.xproperty.atom == a_clients))
				recheck = 1;
			else if (ev.type == ConfigureNotify && engaged && ev.xconfigure.window == game) {
				Window g = game; /* moved, resized or full-screen: new box, same pointer */
				disengage(0);
				engage(g, 0);
			} else if (ev.type == UnmapNotify || ev.type == DestroyNotify)
				recheck = 1;
		}
		t = now();
		if (engaged && (check_at ? t >= check_at : t - last_check_icon >= ICON_PERIOD))
			check_crosshair(check_at ? "after Shift" : "periodic");
		if (engaged && !locked) check_centre();
		else if (engaged && t - last_shift < 0.6 && locked_since < last_shift) { /* Shift while locked: SDL's centring means it ended */
			int px, py;
			pointer(&px, &py);
			if (px == bx + bw / 2 && py == by + bh / 2) {
				check_at = 0;
				second_check = off_votes = 0;
				set_lock(NONE, "centred after Shift");
			}
		}
		if (recheck) continue;
		struct pollfd p = { ConnectionNumber(d), POLLIN, 0 };
		int wait_ms = !engaged ? 1000 : !locked ? 10 : now() - last_shift < 0.6 ? 5 : 1000;
		if (check_at) {
			int left = (int)((check_at - now()) * 1000) + 1;
			if (left < wait_ms) wait_ms = left < 1 ? 1 : left;
		}
		poll(&p, 1, wait_ms);
	}
	disengage(1);
	XCloseDisplay(d);
	return 0;
}
