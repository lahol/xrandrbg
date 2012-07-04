#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

#include <cairo.h>
#include "images.h"

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

const char *config_get_bg_for_output(const char *outputname);
void render_surface(cairo_t *cr, int x, int y, unsigned int w, unsigned int h, cairo_surface_t *img);

char *config_output_images[MAX_OUTPUT][2];

int main(int argc, char **argv)
{
  XEvent ev;
  if (init(NULL) != 0) {
    return 1;
  }

  int i;
  for (i = 1; i < argc; i+=2) {
    if (i+2 <= argc) {
      config_output_images[i/2][0] = strdup(argv[i]);
      config_output_images[i/2][1] = strdup(argv[i+1]);
    }
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
  cairo_surface_t *img_surf;
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
    fprintf(stderr, "render output %d\n", i);
    img_surf = load_image(config_get_bg_for_output(screen_layout.outputnames[i]));
    if (!img_surf) {
      cairo_move_to(cr, screen_layout.outputrects[i][0]+20,
                        screen_layout.outputrects[i][1]+30);
      cairo_show_text(cr, screen_layout.outputnames[i]);
    }
    else {
      render_surface(cr, screen_layout.outputrects[i][0],
                         screen_layout.outputrects[i][1],
                         screen_layout.outputrects[i][2],
                         screen_layout.outputrects[i][3],
                         img_surf);
      cairo_surface_destroy(img_surf);
    }
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

const char *config_get_bg_for_output(const char *outputname)
{
  int i=0;
  while (i < MAX_OUTPUT && config_output_images[i][0]) {
    if (!strcmp(outputname, config_output_images[i][0])) {
      fprintf(stderr, "found %s:%s\n", config_output_images[i][0], config_output_images[i][1]);
      return config_output_images[i][1];
    }
    i++;
  }
  fprintf(stderr, "output not found\n");
  return NULL;
}

void render_surface(cairo_t *cr, int x, int y, unsigned int w, unsigned int h, cairo_surface_t *img)
{
  cairo_matrix_t m;

  double scale, tmp;
  int iw, ih;
  double ox=0, oy=0;

  iw = cairo_image_surface_get_width(img);
  ih = cairo_image_surface_get_height(img);

  scale = ((double)w)/((double)iw);
  tmp = ((double)h)/((double)ih);
  if (tmp > scale) scale = tmp;

  ox = (iw*scale-w)*0.5;
  oy = (ih*scale-h)*0.5;

  cairo_get_matrix(cr, &m);
  cairo_translate(cr, x, y);

  cairo_scale(cr, scale, scale);

  cairo_set_source_surface(cr, img, ox, oy);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  cairo_set_matrix(cr, &m);
}
