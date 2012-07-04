#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

#include <cairo.h>

/* can easily be made more flexible, but for my purpose enough for now */
#define MAX_OUTPUT 32

struct ScreenLayout {
  int noutputs;
  int outputrects[MAX_OUTPUT][4];
  char *outputnames[MAX_OUTPUT];
} screen_layout;

Display *dsp = NULL;
Window root = 0;
int randr_eventbase;
int have_randr;
int error_base;

int init(const char *dpy);
void cleanup(void);
void handle_event(XEvent *event);
void get_screen_layout(void);
void update(XEvent *ev);
void free_strings(char **strings, int count);

void draw_bg(void);
void preserve_resource(void);

int main(int argc, char **argv)
{
  XEvent ev;
  if (init(NULL) != 0) {
    return 1;
  }

  get_screen_layout();
  draw_bg();

  while (1) {
    XNextEvent(dsp, &ev);
    handle_event(&ev);
  }

/*  cleanup();*/
}

int init(const char *dpy)
{
  dsp = XOpenDisplay(dpy);
  if (!dsp) {
    return 1;
  }

  root = RootWindow(dsp, DefaultScreen(dsp));
  if (!root) {
    XCloseDisplay(dsp);
    return 1;
  }

  have_randr = XRRQueryExtension(dsp, &randr_eventbase, &error_base);
  if (!have_randr) {
    return 0;
  }

  XRRSelectInput(dsp, root, RRScreenChangeNotifyMask | RROutputChangeNotifyMask);

  return 0;
}

void cleanup(void)
{
  if (root) {
    XDestroyWindow(dsp, root);
  }

  if (dsp) {
    XCloseDisplay(dsp);
  }
}

void handle_event(XEvent *event)
{
  if (have_randr && (ulong)(event->type-randr_eventbase) < RRNumberEvents) {
    switch (event->type-randr_eventbase) {
      default:
        break;
      case RRScreenChangeNotify:
        update(event);
        break;
      case RRNotify:
        if (((XRRNotifyEvent*)event)->subtype == RRNotify_OutputChange) {
          update(event);
        }
        break;
    }
  }
}

void update(XEvent *ev)
{
  XRRUpdateConfiguration(ev);
  get_screen_layout();
  draw_bg();
}

void get_screen_layout(void)
{
  XRRScreenResources *res;
  int i, j;
  XRROutputInfo *outinfo;
  XRRCrtcInfo *crtcinfo;

  screen_layout.noutputs = 0;
  free_strings(screen_layout.outputnames, MAX_OUTPUT);

  res = XRRGetScreenResources(dsp, root);
  if (!res) {
    return;
  }

  for (i = 0; i < res->ncrtc; i++) {
    crtcinfo = XRRGetCrtcInfo(dsp, res, res->crtcs[i]);
    if (!crtcinfo) {
      continue;
    }
    for (j = 0; j < crtcinfo->noutput; j++) {
      outinfo = XRRGetOutputInfo(dsp, res, crtcinfo->outputs[j]);
      if (!outinfo) {
        continue;
      }
      screen_layout.outputnames[screen_layout.noutputs] = strdup(outinfo->name);
      screen_layout.outputrects[screen_layout.noutputs][0] = crtcinfo->x;
      screen_layout.outputrects[screen_layout.noutputs][1] = crtcinfo->y;
      screen_layout.outputrects[screen_layout.noutputs][2] = crtcinfo->width;
      screen_layout.outputrects[screen_layout.noutputs][3] = crtcinfo->height;
      screen_layout.noutputs++;

      XRRFreeOutputInfo(outinfo);
    }

    XRRFreeCrtcInfo(crtcinfo);
  }

  XRRFreeScreenResources(res);
}

void draw_bg(void)
{
  XWindowAttributes attr;
  Pixmap pm;
  int i;
  cairo_surface_t *scr_surf;
  cairo_t *cr;
  
  XGetWindowAttributes(dsp, root, &attr);

  pm = XCreatePixmap(dsp, root, attr.width, attr.height, attr.depth);

  scr_surf = cairo_xlib_surface_create(dsp, pm, attr.visual, attr.width, attr.height);

  cr = cairo_create(scr_surf);
  
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_rectangle(cr, 0, 0, attr.width, attr.height);
  cairo_fill(cr);

  /* draw to pixmap */
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_set_font_size(cr, 42);

  for (i = 0; i < screen_layout.noutputs; i++) {
    cairo_move_to(cr, screen_layout.outputrects[i][0]+20,
                      screen_layout.outputrects[i][1]+40);
    cairo_show_text(cr, screen_layout.outputnames[i]);
  }

  XSetWindowBackgroundPixmap(dsp, root, pm);
  XClearWindow(dsp, root);
  XFreePixmap(dsp, pm);

  preserve_resource();
}

void preserve_resource(void)
{
  Pixmap pm = XCreatePixmap(dsp, root, 1, 1, 1);
  unsigned char *data = (unsigned char*)&pm;

  Atom atom = XInternAtom(dsp, "_XSETROOT_ID", 0);

  XChangeProperty(dsp, root, atom, XA_PIXMAP, 32, PropModeReplace,
      data, sizeof(Pixmap)/4);

  XSetCloseDownMode(dsp, RetainPermanent);
}

void free_strings(char **strings, int count)
{
  int i;
  for (i = 0; i < count; i++) {
    if (strings[i]) {
      free(strings[i]);
      strings[i] = NULL;
    }
  }
}

