/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

enum {
	INIT,
	INPUT,
	FAILED,
	NUMCOLS
};

#include "config.h"

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[NUMCOLS];
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;
static Bool failure = False;
static Bool rr;
static int rrevbase;
static int rrerrbase;

static void
die(const char *errstr, ...)
{
	va_list ap;

	fputs("slock: ", stderr);
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>

static void
dontkillme(void)
{
	int fd;

	fd = open("/proc/self/oom_score_adj", O_WRONLY);
	if (fd < 0 && errno == ENOENT) {
		return;
	}
	if (fd < 0 || write(fd, "-1000\n", (sizeof("-1000\n") - 1)) !=
	    (sizeof("-1000\n") - 1) || close(fd) != 0) {
		die("can't tame the oom-killer. is suid or sgid set?\n");
	}
}
#endif

#ifndef HAVE_BSD_AUTH
/* only run as root */
static const char *
getpw(void)
{
	const char *rval;
	struct passwd *pw;

	errno = 0;
	if (!(pw = getpwuid(getuid()))) {
		if (errno)
			die("getpwuid: %s\n", strerror(errno));
		else
			die("cannot retrieve password entry\n");
	}
	rval = pw->pw_passwd;

#if HAVE_SHADOW_H
	if (rval[0] == 'x' && rval[1] == '\0') {
		struct spwd *sp;
		if (!(sp = getspnam(getenv("USER"))))
			die("cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		rval = sp->sp_pwdp;
	}
#endif

	/* drop privileges */
	if (geteuid() == 0 &&
	    ((getegid() != pw->pw_gid && setgid(pw->pw_gid) < 0) || setuid(pw->pw_uid) < 0))
		die("cannot drop privileges\n");
	return rval;
}
#endif

static void
runscriptonlock()
{
	int pid;

	if ((pid = fork()) < 0) {
		fprintf(stderr, "slock: error on fork() during lock: %s\n", strerror(errno));
		return;
	}

	if (pid == 0) {
		/* child: run script on locking */
		execlp("slock-script-lock", "slock-script-lock", (char *) NULL);
		fprintf(stderr, "slock: error on exec() during lock: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void
runscriptonunlock(const char *passwd)
{
	int ret;

	/* run script after unlocking, passing the password on stdin */
	if ((ret = system("slock-script-unlock")))
		fprintf(stderr, "slock: error running `slock-script-unlock': %d\n", ret);
}

static void
#ifdef HAVE_BSD_AUTH
readpw(Display *dpy)
#else
readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256];
	int num, screen;
	unsigned int len, color;
	KeySym ksym;
	XEvent ev;
	static int oldc = INIT;

	len = 0;
	running = True;

	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
			if (IsKeypadKey(ksym)) {
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if (IsFunctionKey(ksym) ||
			    IsKeypadKey(ksym) ||
			    IsMiscFunctionKey(ksym) ||
			    IsPFKey(ksym) ||
			    IsPrivateKeypadKey(ksym))
				continue;
			switch (ksym) {
			case XK_Return:
				passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
				running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
				running = !!strcmp(crypt(passwd, pws), pws);
#endif
				if (running) {
					XBell(dpy, 100);
					failure = True;
				} else
					runscriptonunlock(passwd);
				len = 0;
				break;
			case XK_Escape:
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					--len;
				break;
			default:
				if (num && !iscntrl((int)buf[0]) && (len + num < sizeof(passwd))) {
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			color = len ? INPUT : (failure || failonclear ? FAILED : INIT);
			if (running && oldc != color) {
				for (screen = 0; screen < nscreens; screen++) {
					XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[color]);
					XClearWindow(dpy, locks[screen]->win);
				}
				oldc = color;
			}
		} else if (rr && ev.type == rrevbase + RRScreenChangeNotify) {
			XRRScreenChangeNotifyEvent *rre = (XRRScreenChangeNotifyEvent*)&ev;
			for (screen = 0; screen < nscreens; screen++) {
				if (locks[screen]->win == rre->window) {
					XResizeWindow(dpy, locks[screen]->win, rre->width, rre->height);
					XClearWindow(dpy, locks[screen]->win);
				}
			}
		} else for (screen = 0; screen < nscreens; screen++)
			XRaiseWindow(dpy, locks[screen]->win);
	}
}

static void
unlockscreen(Display *dpy, Lock *lock)
{
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), lock->colors, NUMCOLS, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static Lock *
lockscreen(Display *dpy, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	int i;
	Lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if (!running || dpy == NULL || screen < 0 || !(lock = malloc(sizeof(Lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	for (i = 0; i < NUMCOLS; i++) {
		XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), colorname[i], &color, &dummy);
		lock->colors[i] = color.pixel;
	}

	runscriptonlock();

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = lock->colors[INIT];
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
	                          0, DefaultDepth(dpy, lock->screen), CopyFromParent,
	                          DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);
	if (rr)
		XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

	/* Try to grab mouse pointer *and* keyboard, else fail the lock */
	for (len = 1000; len; len--) {
		if (XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		    GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if (!len) {
		fprintf(stderr, "slock: unable to grab mouse pointer for screen %d\n", screen);
	} else {
		for (len = 1000; len; len--) {
			if (XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
				/* everything fine, we grabbed both inputs */
				XSelectInput(dpy, lock->root, SubstructureNotifyMask);
				return lock;
			}
			usleep(1000);
		}
		fprintf(stderr, "slock: unable to grab keyboard for screen %d\n", screen);
	}
	/* grabbing one of the inputs failed */
	running = 0;
	unlockscreen(dpy, lock);
	return NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: slock [-v|POST_LOCK_CMD]\n");
	exit(1);
}

int
main(int argc, char **argv) {
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;

	if ((argc >= 2) && !strcmp("-v", argv[1]))
		die("version %s, © 2006-2016 slock engineers\n", VERSION);

	/* treat first argument starting with a '-' as option */
	if ((argc >= 2) && argv[1][0] == '-')
		usage();

#ifdef __linux__
	dontkillme();
#endif

	if (!getpwuid(getuid()))
		die("no passwd entry for you\n");

#ifndef HAVE_BSD_AUTH
	pws = getpw();
#endif

	if (!(dpy = XOpenDisplay(0)))
		die("cannot open display\n");
	rr = XRRQueryExtension(dpy, &rrevbase, &rrerrbase);
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	if (!(locks = malloc(sizeof(Lock*) * nscreens)))
		die("Out of memory.\n");
	int nlocks = 0;
	for (screen = 0; screen < nscreens; screen++) {
		if ((locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { /* nothing to protect */
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	if (argc >= 2 && fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		execvp(argv[1], argv+1);
		die("execvp %s failed: %s\n", argv[1], strerror(errno));
	}

	/* Everything is now blank. Now wait for the correct password. */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif

	/* Password ok, unlock everything and quit. */
	for (screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
