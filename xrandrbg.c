#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <confuse.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

#include <cairo.h>
#include <cairo-xlib.h>
#include "images.h"

/* can easily be made more flexible, but for my purpose enough for now */
#define MAX_OUTPUT 32

#define HEX_VALUE(c) (((c)>='0' && (c)<='9') ? (c)-'0' : \
    (((c)>='A' && (c)<= 'F') ? (c)-'A'+10 :\
     (((c)>='a' && (c)<= 'f') ? (c)-'a'+10 : 0)))

struct ScreenLayout {
  int noutputs;
  int outputrects[MAX_OUTPUT][4];
  char *outputnames[MAX_OUTPUT];
} screen_layout;

enum IMAGE_MODE {
  IM_CENTERED = 0,
  IM_SCALED,
  IM_ZOOMED,
  IM_ZOOMED_FILL,
  IM_TILED
};

Display *dsp = NULL;
Window root = 0;
int randr_eventbase;
int have_randr;
int error_base;

int init(const char *dpy);
int init_config(const char *cfgpath);
void cleanup(void);
void handle_event(XEvent *event);
void get_screen_layout(void);
void update(XEvent *ev);
void free_strings(char **strings, int count);

void draw_bg(void);
void preserve_resource(void);

int validate_color(cfg_t *cfg, cfg_opt_t *opt);
int validate_mode(cfg_t *cfg, cfg_opt_t *opt);

void translate_mode(const char *str, enum IMAGE_MODE *mode);
void translate_color(const char *str, double *r, double *g, double *b);

void config_get_bg_for_output(const char *outputname,
                              char **filename,
                              enum IMAGE_MODE *mode,
                              double *r, double *g, double *b);
void render_surface(cairo_t *cr, enum IMAGE_MODE mode, int x, int y, unsigned int w, unsigned int h, cairo_surface_t *img);
void get_image_scale_and_offset(enum IMAGE_MODE mode,
                                unsigned int img_w,
                                unsigned int img_h,
                                unsigned int out_w,
                                unsigned int out_h,
                                double *scale_x,
                                double *scale_y,
                                double *offset_x,
                                double *offset_y);

/*char *config_output_images[MAX_OUTPUT][2];*/

cfg_t *config = NULL;

int main(int argc, char **argv)
{
  XEvent ev;
  if (init_config(argc > 1 ? argv[1] : NULL) != 0) {
    return 1;
  }
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

int validate_color(cfg_t *cfg, cfg_opt_t *opt)
{
  int i;
  char *str = cfg_opt_getnstr(opt, 0);
  if (!str || strlen(str) != 7) {
    return -1;
  }
  if (str[0] != '#') return -1;
  for (i = 1; i < 7; i++) {
    if (str[i] >= '0' && str[i] <= '9') continue;
    if (str[i] >= 'a' && str[i] <= 'f') continue;
    if (str[i] >= 'A' && str[i] <= 'F') continue;
    return -1;
  }
  return 0;
}

int validate_mode(cfg_t *cfg, cfg_opt_t *opt)
{
  char *str = cfg_opt_getnstr(opt, 0);
  if (!str) return -1;
  if (!strcasecmp(str, "centered") ||
      !strcasecmp(str, "scaled") ||
      !strcasecmp(str, "zoomed") ||
      !strcasecmp(str, "zoomed-fill") ||
      !strcasecmp(str, "tiled")) {
    return 0;
  }
  return -1;
}

int init_config(const char *cfgpath)
{
  cfg_opt_t output_opts[] = {
    CFG_STR("file", NULL, CFGF_NONE),
    CFG_STR("color", "#000000", CFGF_NOCASE),
    CFG_STR("mode", "centered", CFGF_NOCASE),
    CFG_END()
  };

  cfg_opt_t opts[] = {
    CFG_SEC("output", output_opts, CFGF_TITLE | CFGF_MULTI | CFGF_NOCASE),
    CFG_END()
  };

  config = cfg_init(opts, CFGF_NONE);
  cfg_set_validate_func(config, "output|color", validate_color);
  cfg_set_validate_func(config, "output|mode", validate_mode);
  if (!cfgpath) {
    /* use defaults */
    cfg_parse_buf(config, "output default {}");
  }
  else if (cfg_parse(config, cfgpath) == CFG_PARSE_ERROR) {
    fprintf(stderr, "Config file error\n");
    return 1;
  }

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

  if (config) {
    cfg_free(config);
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
  double r, g, b;
  enum IMAGE_MODE mode;
  char *filename;
  
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
    config_get_bg_for_output(screen_layout.outputnames[i],
                             &filename,
                             &mode,
                             &r, &g, &b);
    if (filename) {
      img_surf = load_image(filename);
    }
    else {
      img_surf = NULL;
    }
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr,
                    screen_layout.outputrects[i][0],
                    screen_layout.outputrects[i][1],
                    screen_layout.outputrects[i][2],
                    screen_layout.outputrects[i][3]);
    cairo_fill(cr);
    if (img_surf) {
      render_surface(cr, mode,
                         screen_layout.outputrects[i][0],
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

void translate_mode(const char *str, enum IMAGE_MODE *mode)
{
  if (!mode) return;
  if (!str) *mode = IM_CENTERED;

  if (!strcasecmp(str, "scaled"))
    *mode = IM_SCALED;
  else if (!strcasecmp(str, "zoomed"))
    *mode = IM_ZOOMED;
  else if (!strcasecmp(str, "zoomed-fill"))
    *mode = IM_ZOOMED_FILL;
  else if (!strcasecmp(str, "tiled"))
    *mode = IM_TILED;
}

void translate_color(const char *str, double *r, double *g, double *b)
{
  if (r) *r = 0.0f;
  if (g) *g = 0.0f;
  if (b) *b = 0.0f;

  if (!str || strlen(str) != 7) {
    return;
  }

  if (r) *r = (HEX_VALUE(str[1])*16+HEX_VALUE(str[2]))*0.00390625;
  if (g) *g = (HEX_VALUE(str[3])*16+HEX_VALUE(str[4]))*0.00390625;
  if (b) *b = (HEX_VALUE(str[5])*16+HEX_VALUE(str[6]))*0.00390625;
}

void config_get_bg_for_output(const char *outputname,
                              char **filename,
                              enum IMAGE_MODE *mode,
                              double *r, double *g, double *b)
{
  cfg_t *cfg_output = NULL, *cfg_default = NULL;
  int i, j;

  double red = 0.0f, green = 0.0f, blue = 0.0f;
  enum IMAGE_MODE im = IM_CENTERED;
  char *fn = NULL;

  for (i = 0; i < cfg_size(config, "output"); i++) {
    cfg_output = cfg_getnsec(config, "output", i);
    if (outputname && !strcasecmp(outputname, cfg_title(cfg_output))) {
      /* found the section for this output */
      break;
    }
    else if (!strcasecmp("default", cfg_title(cfg_output))) {
      cfg_default = cfg_output;
    }
  }

  if (!cfg_output) {
    if (cfg_default) {
      cfg_output = cfg_default;
    }
  }
  if (cfg_output) {
    fn = cfg_getstr(cfg_output, "file");
    translate_mode(cfg_getstr(cfg_output, "mode"), &im);
    translate_color(cfg_getstr(cfg_output, "color"), &red, &green, &blue);
  }

  if (filename) *filename = fn;
  if (mode) *mode = im;
  if (r) *r = red;
  if (g) *g = green;
  if (b) *b = blue;
}

void render_surface(cairo_t *cr, enum IMAGE_MODE mode, int x, int y, unsigned int w, unsigned int h, cairo_surface_t *img)
{
  cairo_matrix_t m;

  double scale_x, scale_y;
  int iw, ih;
  double ox=0, oy=0;

  iw = cairo_image_surface_get_width(img);
  ih = cairo_image_surface_get_height(img);

  get_image_scale_and_offset(mode, iw, ih, w, h, &scale_x, &scale_y, &ox, &oy);

  cairo_get_matrix(cr, &m);
  cairo_translate(cr, x, y);

  cairo_scale(cr, scale_x, scale_y);

  cairo_set_source_surface(cr, img, ox, oy);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  cairo_set_matrix(cr, &m);
}

void get_image_scale_and_offset(enum IMAGE_MODE mode,
                                unsigned int img_w,
                                unsigned int img_h,
                                unsigned int out_w,
                                unsigned int out_h,
                                double *scale_x,
                                double *scale_y,
                                double *offset_x,
                                double *offset_y)
{
  double sx=1.0f, sy=1.0f, ox=0.0f, oy=0.0f;

  switch (mode) {
    default:
    case IM_CENTERED:
      /* center image on screen */
      ox = (out_w-img_w)*0.5f;
      oy = (out_h-img_h)*0.5f;
      break;
    case IM_SCALED:
      /* scale image with no respect for aspect ratio */
      sx = ((double)out_w)/((double)img_w);
      sy = ((double)out_h)/((double)img_h);
      break;
    case IM_ZOOMED:
      /* zoom the image as big as possible, while keeping aspect ratio,
       * rest will be filled with background color */
      sx = ((double)out_w)/((double)img_w);
      sy = ((double)out_h)/((double)img_h);
      if (sy < sx) sx = sy;
      else sy = sx;

      ox = (out_w/sx-img_w)*0.5f;
      oy = (out_h/sy-img_h)*0.5f;
      break;
    case IM_ZOOMED_FILL:
      /* zoom the image so that one side fills the output,
       * the other may be cut off, keep aspect ratio */
      sx = ((double)out_w)/((double)img_w);
      sy = ((double)out_h)/((double)img_h);
      if (sy > sx) sx = sy;
      else sy = sx;

      ox = (out_w/sx-img_w)*0.5f;
      oy = (out_h/sy-img_h)*0.5f;

      break;
    case IM_TILED:
      /* nothing to do here */
      break;
  }

  if (scale_x) *scale_x = sx;
  if (scale_y) *scale_y = sy;
  if (offset_x) *offset_x = ox;
  if (offset_y) *offset_y = oy;
}

