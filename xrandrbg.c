/* Copyright (c) 2012, Holger Langenau
 * see LICENSE for details
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <unistd.h>

#include <confuse.h>

#include <ev.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

#include <Imlib2.h>

/* can easily be made more flexible, but for my purpose enough for now */
#define MAX_OUTPUT 32

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
Colormap colormap;
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

void x11_cb(EV_P_ ev_io *w, int revents);
void signal_cb(EV_P_ ev_signal *w, int revents);

void draw_bg(void);
void preserve_resource(void);

int validate_color(cfg_t *cfg, cfg_opt_t *opt);
int validate_mode(cfg_t *cfg, cfg_opt_t *opt);

void translate_mode(const char *str, enum IMAGE_MODE *mode);

void config_get_bg_for_output(const char *outputname,
                              char **filename,
                              enum IMAGE_MODE *mode,
                              char *color_string);
void render_image(enum IMAGE_MODE mode, int x, int y, unsigned int w, unsigned int h, Imlib_Image img);
void get_image_scale_and_offset(enum IMAGE_MODE mode,
                                unsigned int img_w,
                                unsigned int img_h,
                                unsigned int out_w,
                                unsigned int out_h,
                                double *scale_x,
                                double *scale_y,
                                double *offset_x,
                                double *offset_y);
void get_image_offset_and_width(enum IMAGE_MODE mode,
                                unsigned int img_w,
                                unsigned int img_h,
                                unsigned int out_w,
                                unsigned int out_h,
                                int *src_x, int *src_y, int *src_w, int *src_h,
                                int *dst_x, int *dst_y, int *dst_w, int *dst_h);

cfg_t *config = NULL;

ev_io x11_watcher;
ev_signal sigint_watcher;
ev_signal sigterm_watcher;

int main(int argc, char **argv)
{
  int x11_fd;
  struct ev_loop *loop = EV_DEFAULT;

  if (init_config(argc > 1 ? argv[1] : NULL) != 0) {
    return 1;
  }
  if (init(NULL) != 0) {
    cleanup();
    return 1;
  }

  get_screen_layout();
  draw_bg();

  x11_fd = ConnectionNumber(dsp);
 
  ev_io_init(&x11_watcher, x11_cb, x11_fd, EV_READ);
  ev_io_start(loop, &x11_watcher);

  ev_signal_init(&sigint_watcher, signal_cb, SIGINT);
  ev_signal_start(loop, &sigint_watcher);

  ev_signal_init(&sigterm_watcher, signal_cb, SIGTERM);
  ev_signal_start(loop, &sigterm_watcher);

  ev_run(loop, 0);

  cleanup();
  return 0;
}

void x11_cb(EV_P_ ev_io *w, int revents)
{
  XEvent ev;
  while (XPending(dsp)) {
    XNextEvent(dsp, &ev);
    handle_event(&ev);
  }
}

void signal_cb(EV_P_ ev_signal *w, int revents)
{
  ev_break(EV_A_ EVBREAK_ALL);
}

int init(const char *dpy)
{
  dsp = XOpenDisplay(dpy);
  if (!dsp) {
    return 1;
  }

  root = RootWindow(dsp, DefaultScreen(dsp));
  if (!root) {
    return 1;
  }

  have_randr = XRRQueryExtension(dsp, &randr_eventbase, &error_base);
  if (!have_randr) {
    return 0;
  }

  XRRSelectInput(dsp, root, RRScreenChangeNotifyMask | RROutputChangeNotifyMask);

  colormap = DefaultColormap(dsp, DefaultScreen(dsp));

  imlib_context_set_display(dsp);
  imlib_context_set_visual(DefaultVisual(dsp, DefaultScreen(dsp)));
  imlib_context_set_colormap(colormap);

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
  Imlib_Image img;
  enum IMAGE_MODE mode;
  char *filename;
  GC gc;
  XColor bg_col;
  char col_str[8];
  
  XGetWindowAttributes(dsp, root, &attr);

  pm = XCreatePixmap(dsp, root, attr.width, attr.height, attr.depth);
  imlib_context_set_drawable(pm);

  for (i = 0; i < screen_layout.noutputs; i++) {
    config_get_bg_for_output(screen_layout.outputnames[i],
                             &filename,
                             &mode,
                             col_str);
    /* fill with background color */
    gc = XCreateGC(dsp, pm, 0, 0);
    XParseColor(dsp, colormap, col_str, &bg_col);
    XAllocColor(dsp, colormap, &bg_col);
    XSetForeground(dsp, gc, bg_col.pixel);
    XFillRectangle(dsp, pm, gc,
                   screen_layout.outputrects[i][0],
                   screen_layout.outputrects[i][1],
                   screen_layout.outputrects[i][2],
                   screen_layout.outputrects[i][3]);

    /* if a file is given, try to render that image */
    if (filename) {
      img = imlib_load_image(filename);
      if (img) {
        render_image(mode,
                     screen_layout.outputrects[i][0],
                     screen_layout.outputrects[i][1],
                     screen_layout.outputrects[i][2],
                     screen_layout.outputrects[i][3],
                     img);
        imlib_free_image();
      }
    }
  }

  /* set the pixmap as background image of the root window */
  XSetWindowBackgroundPixmap(dsp, root, pm);
  XClearWindow(dsp, root);
  XFreePixmap(dsp, pm);

  /* make changes persistent */
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

void config_get_bg_for_output(const char *outputname,
                              char **filename,
                              enum IMAGE_MODE *mode,
                              char *color_string)
{
  cfg_t *cfg_output = NULL, *cfg_default = NULL;
  int i;

  char cstr[] = "#000000";
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
    strcpy(cstr, cfg_getstr(cfg_output, "color"));
  }

  if (filename) *filename = fn;
  if (mode) *mode = im;
  if (color_string) strcpy(color_string, cstr);
}

void render_image(enum IMAGE_MODE mode, int x, int y, unsigned int w, unsigned int h, Imlib_Image img)
{
  int iw, ih;
  int tiles_x, tiles_y;
  int i, j;
  int tw, th;

  int src_x, src_y, src_w, src_h;
  int dst_x, dst_y, dst_w, dst_h;

  imlib_context_set_image(img);
  iw = imlib_image_get_width();
  ih = imlib_image_get_height();

  if (w <= 0 || h <= 0 || iw <= 0 || ih <= 0) {
    return;
  }

  if (mode != IM_TILED) {
    get_image_offset_and_width(mode, iw, ih, w, h, 
                               &src_x, &src_y, &src_w, &src_h,
                               &dst_x, &dst_y, &dst_w, &dst_h);
    imlib_render_image_part_on_drawable_at_size(src_x, src_y, src_w, src_h,
                                                x+dst_x, y+dst_y, dst_w, dst_h);
  }
  else {
    tiles_x = w/iw + (w%iw ? 1 : 0);
    tiles_y = h/ih + (h%ih ? 1 : 0);
    th = ih;
    for (i = 0; i <= tiles_y; i++) {
      if (i == tiles_y-1) {
        th = h-i*ih;
      }
      tw = iw;
      for (j = 0; j < tiles_x; j++) {
        if (j == tiles_x-1) {
          tw = w-j*iw;
        }
        imlib_render_image_part_on_drawable_at_size(0, 0, tw, th,
                                                    x+j*iw, y+i*ih, tw, th);
      }
    }
  }
}

void get_image_offset_and_width(enum IMAGE_MODE mode,
                                unsigned int img_w,
                                unsigned int img_h,
                                unsigned int out_w,
                                unsigned int out_h,
                                int *src_x, int *src_y, int *src_w, int *src_h,
                                int *dst_x, int *dst_y, int *dst_w, int *dst_h)
{
  int sx, sy, sw, sh, dx, dy, dw, dh;
  double scw, sch;

  switch (mode) {
    default:
    case IM_CENTERED:
      if (out_w >= img_w) {
        sx = 0; sw = dw = img_w;
        dx = (out_w-img_w) >> 1;
      }
      else {
        dx = 0;
        sw = dw = out_w;
        sx = (img_w-out_w) >> 1;
      }

      if (out_h >= img_h) {
        sy = 0; sh = dh = img_h;
        dy = (out_h-img_h) >> 1;
      }
      else {
        dy = 0;
        sh = dh = out_h;
        sy = (img_h-out_h) >> 1;
      }
      break;
    case IM_SCALED:
      sx = sy = dx = dy = 0;
      sw = img_w;
      dw = out_w;
      sh = img_h;
      dh = out_h;
      break;
    case IM_ZOOMED:
      sx = sy = 0;
      sw = img_w;
      sh = img_h;

      scw = ((double)out_w)/((double)img_w);
      sch = ((double)out_h)/((double)img_h);
      if (scw < sch) {
        dx = 0;
        dw = out_w;
        dh = (int)(img_h*scw);
        dy = (out_h-dh) >> 1;
      }
      else {
        dy = 0;
        dh = out_h;
        dw = (int)(img_w*sch);
        dx = (out_w-dw) >> 1;
      }
      break;
    case IM_ZOOMED_FILL:
      dx = dy = 0;
      dw = out_w;
      dh = out_h;

      scw = ((double)out_w)/((double)img_w);
      sch = ((double)out_h)/((double)img_h);
      if (scw > sch) {
        sx = 0;
        sw = img_w;
        sh = (int)(out_h/scw);
        sy = (img_h-sh) >> 1;
      }
      else {
        sy = 0;
        sh = img_h;
        sw = (int)(out_w/sch);
        sx = (img_w-sw) >> 1;
      }
      break;
  }

  if (src_x) *src_x = sx;
  if (src_y) *src_y = sy;
  if (src_w) *src_w = sw;
  if (src_h) *src_h = sh;
  if (dst_x) *dst_x = dx;
  if (dst_y) *dst_y = dy;
  if (dst_w) *dst_w = dw;
  if (dst_h) *dst_h = dh;
}


