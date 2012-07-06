/* Copyright (c) 2012, Holger Langenau
 * see LICENSE for details
 */
#include "images.h"
#include <stdio.h>
#include <stdlib.h>

cairo_surface_t *load_image(const char *filename)
{
  cairo_surface_t *img;
  fprintf(stderr, "trying to load image %s\n", filename);
  if (!filename) {
    return NULL;
  }

  img = cairo_image_surface_create_from_png(filename);

  /* error handling */
  return img;
}
