/* sober-pointer: right mouse speed and screen edges for Sober's HiDPI mode on X11.
 *
 * With "enable_hidpi" on, Sober draws Roblox at the display scale (Xft.dpi/96,
 * 2 here) but X11 hands it pointer positions in real pixels, which it scales
 * again: Roblox's cursor moves twice as fast as the desktop's and runs off the
 * right and bottom of the window. While Sober's game window has focus, this
 * scales pointer motion by 1/scale (each pointer's Coordinate Transformation
 * Matrix) and keeps the pointer inside the part of the window Sober maps onto
 * the whole of it (XFixes pointer barriers). When focus leaves, the matrices
 * are restored, the barriers go, and the pointer is moved to where Roblox's
 * cursor was; when focus comes back, Sober halves the position itself.
 * It moves the pointer at no other time: Sober answers outside warps by
 * moving the pointer again. Roblox's mouse lock (shift-lock, first person)
 * can't be helped from here: Sober turns the camera by the pointer's own
 * movement, never recentres it, and shows no sign of the lock to other
 * clients (no grab, same cursor), so the cursor shows up where the pointer
 * drifted when the lock ends.
 *
 * Started by the flatpak launcher for every Sober run; one instance at a
 * time; exits 5 s after the last Sober window closes (or after 3 minutes if
 * none appears), and does nothing if Sober's HiDPI setting is off.
 *
 * Build: cc -O2 -o sober-pointer sober-pointer.c $(pkg-config --cflags --libs x11 xi xfixes)
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#define MAXDEV 32

static Display *d;
static Window root;
static Atom a_active, a_clients, a_ctm, a_float;
static double scale = 2;
static volatile sig_atomic_t quit;

static struct { int id; float ctm[9]; } dev[MAXDEV];
static int ndev;
static PointerBarrier bar[4];
static int engaged, bx, by, bw, bh, sw, sh; /* the window's root geometry while engaged; the screen */
static Window game;

static FILE *dbg; /* SOBER_POINTER_LOG=path */
#define LOG(...) do { if (dbg) { fprintf(dbg, "%.3f ", now()); fprintf(dbg, __VA_ARGS__); fflush(dbg); } } while (0)
static double now(void);

static void on_signal(int s) { (void)s; quit = 1; }

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

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

static Window window_prop(Atom a, int index)
{
	Atom type;
	int fmt;
	unsigned long n, after;
	unsigned char *v = NULL;
	Window w = None;
	if (XGetWindowProperty(d, root, a, 0, 1024, False, XA_WINDOW, &type, &fmt, &n, &after, &v) == Success &&
	    v && (unsigned long)index < n)
		w = ((Window *)v)[index];
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

/* XI2 properties take format-32 data as packed 32-bit values, unlike core ones */
static void set_ctm(int id, const float *m)
{
	XIChangeProperty(d, id, a_ctm, a_float, 32, PropModeReplace, (unsigned char *)m, 9);
}

static void engage(Window w)
{
	XWindowAttributes wa;
	Window child;
	if (!XGetWindowAttributes(d, w, &wa) || wa.map_state != IsViewable) return;
	XTranslateCoordinates(d, w, root, 0, 0, &bx, &by, &child);
	if (bx < 0 || by < 0) return; /* on a hidden dwm tag */
	int r = bx + (int)(wa.width / scale), b = by + (int)(wa.height / scale);

	int n;
	ndev = 0;
	XIDeviceInfo *info = XIQueryDevice(d, XIAllDevices, &n);
	for (int i = 0; i < n && ndev < MAXDEV; i++) {
		if (info[i].use != XISlavePointer || strstr(info[i].name, "XTEST")) continue; /* not xdotool & co */
		Atom type;
		int fmt;
		unsigned long items, after;
		unsigned char *v = NULL;
		if (XIGetProperty(d, info[i].deviceid, a_ctm, 0, 9, False, a_float, &type, &fmt, &items, &after, &v) ==
			    Success && items == 9) {
			dev[ndev].id = info[i].deviceid;
			memcpy(dev[ndev].ctm, v, sizeof dev[ndev].ctm);
			float m[9];
			memcpy(m, dev[ndev].ctm, sizeof m);
			for (int k = 0; k < 6; k++) m[k] /= (float)scale; /* the 2x2 linear part and the offsets */
			set_ctm(dev[ndev].id, m);
			ndev++;
		}
		if (v) XFree(v);
	}
	XIFreeDeviceInfo(info);

	/* each barrier lets the pointer in but not out; they run the whole width or
	 * height of the screen so no diagonal move slips past a corner */
	bar[0] = XFixesCreatePointerBarrier(d, root, bx, 0, bx, sh, BarrierPositiveX, 0, NULL);
	bar[1] = XFixesCreatePointerBarrier(d, root, r, 0, r, sh, BarrierNegativeX, 0, NULL);
	bar[2] = XFixesCreatePointerBarrier(d, root, 0, by, sw, by, BarrierPositiveY, 0, NULL);
	bar[3] = XFixesCreatePointerBarrier(d, root, 0, b, sw, b, BarrierNegativeY, 0, NULL);
	/* No warp: a moment after its window gets focus, Sober halves the
	 * pointer's position itself, which puts Roblox's cursor where the real
	 * one was. */
	XSync(d, False);
	engaged = 1;
	game = w;
	bw = wa.width;
	bh = wa.height;
	LOG("engage 0x%lx at %d,%d %dx%d box to %d,%d\n", w, bx, by, wa.width, wa.height, r, b);
	XSelectInput(d, w, StructureNotifyMask);
}

/* warp: leaving Sober, so put the real pointer where Roblox's cursor is */
static void disengage(int warp)
{
	if (!engaged) return;
	LOG("disengage%s\n", warp ? "" : " (geometry)");
	for (int i = 0; i < 4; i++) XFixesDestroyPointerBarrier(d, bar[i]);
	for (int i = 0; i < ndev; i++) set_ctm(dev[i].id, dev[i].ctm);
	Window rw, cw;
	int px, py, wx, wy;
	unsigned mask;
	if (warp && XQueryPointer(d, root, &rw, &cw, &px, &py, &wx, &wy, &mask) && px >= bx && py >= by)
		XWarpPointer(d, None, root, 0, 0, 0, 0, bx + (int)((px - bx) * scale), by + (int)((py - by) * scale));
	XSync(d, False);
	engaged = 0;
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
	root = DefaultRootWindow(d);
	sw = DisplayWidth(d, DefaultScreen(d));
	sh = DisplayHeight(d, DefaultScreen(d));
	a_active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
	a_clients = XInternAtom(d, "_NET_CLIENT_LIST", False);
	a_ctm = XInternAtom(d, "Coordinate Transformation Matrix", False);
	a_float = XInternAtom(d, "FLOAT", False);
	read_scale();
	if (scale < 1.01) return 0;

	struct sigaction sa = { .sa_handler = on_signal };
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	XSelectInput(d, root, PropertyChangeMask);

	double start = now(), last_seen = 0, last_check = 0;
	int recheck = 1;
	while (!quit) {
		double t = now();
		if (recheck || t - last_check >= 1) { /* focus changes, and once a second: is Sober still there */
			Window a = window_prop(a_active, 0);
			int want = is_game(a);
			if (engaged && (!want || a != game)) disengage(1);
			if (want && !engaged) engage(a);
			if (sober_running()) last_seen = t;
			last_check = t;
			recheck = 0;
		}
		if (last_seen ? t - last_seen > 5 : t - start > 180) break;
		while (XPending(d)) {
			XEvent ev;
			XNextEvent(d, &ev);
			if (ev.type == PropertyNotify && (ev.xproperty.atom == a_active || ev.xproperty.atom == a_clients))
				recheck = 1;
			else if (ev.type == ConfigureNotify && engaged && ev.xconfigure.window == game) {
				disengage(0); /* moved, resized or full-screen: new box, same pointer */
				recheck = 1;
			} else if (ev.type == UnmapNotify || ev.type == DestroyNotify)
				recheck = 1;
		}
		if (recheck) continue;
		struct pollfd p = { ConnectionNumber(d), POLLIN, 0 };
		poll(&p, 1, 1000);
	}
	disengage(1);
	XCloseDisplay(d);
	return 0;
}
