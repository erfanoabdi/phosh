/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#pragma once

#include "phosh-wayland.h"

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib/gi18n.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_HEAD                 (phosh_head_get_type ())

G_DECLARE_FINAL_TYPE (PhoshHead, phosh_head, PHOSH, HEAD, GObject)

typedef struct _PhoshHead PhoshHead;

typedef struct _PhoshHeadMode {
  /*< private >*/
  struct zwlr_output_mode_v1 *wlr_mode;
  PhoshHead                  *head;

  int32_t                     width, height;
  int32_t                     refresh;
  gboolean                    preferred;
} PhoshHeadMode;

struct _PhoshHead {
  GObject                     parent;

  gchar                      *name;
  gchar                      *description;
  gboolean                    enabled;

  struct {
    int32_t width, height;
  } phys;
  int32_t                     x, y;

  enum wl_output_transform    transform;
  double                      scale;
  PhoshHeadMode              *mode;
  GPtrArray                  *modes;

  struct pending {
    int32_t x, y;
    enum wl_output_transform transform;
    PhoshHeadMode *mode;
    double scale;
  } pending;

  struct zwlr_output_head_v1 *wlr_head;
};


PhoshHead                  *phosh_head_new_from_wlr_head (gpointer wlr_head);
struct zwlr_output_head_v1 *phosh_head_get_wlr_head (PhoshHead *self);
gboolean                    phosh_head_get_enabled (PhoshHead *self);
PhoshHeadMode              *phosh_head_get_preferred_mode (PhoshHead *self);

G_END_DECLS
