/* Copyright (c) 2012, Holger Langenau
 * see LICENSE for details
 */
#include "images.h"
#include <stdio.h>
#include <stdlib.h>

cairo_surface_t *load_image(const char *filename)
{
  fprintf(stderr, "trying to load image %s\n", filename);
  cairo_surface_t *img;
  if (!filename) {
    return NULL;
  }

  img = cairo_image_surface_create_from_png(filename);

  /* error handling */
  return img;
}
