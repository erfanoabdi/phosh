/*
 * Copyright (C) 2018 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-lockscreen-manager"

#include "lockscreen-manager.h"
#include "lockscreen.h"
#include "lockshield.h"
#include "monitor-manager.h"
#include "monitor/monitor.h"
#include "phosh-wayland.h"
#include "shell.h"
#include "util.h"
#include "session-presence.h"
#include <gdk/gdkwayland.h>


enum {
  PHOSH_LOCKSCREEN_MANAGER_PROP_0,
  PHOSH_LOCKSCREEN_MANAGER_PROP_LOCKED,
  PHOSH_LOCKSCREEN_MANAGER_PROP_TIMEOUT,
  PHOSH_LOCKSCREEN_MANAGER_PROP_LAST_PROP
};
static GParamSpec *props[PHOSH_LOCKSCREEN_MANAGER_PROP_LAST_PROP];


typedef struct {
  GObject parent;

  PhoshLockscreen *lockscreen;     /* phone display lock screen */
  PhoshSessionPresence *presence;  /* gnome-session's presence interface */
  GPtrArray *shields;              /* other outputs */
  gulong unlock_handler_id;
  GSettings *settings;

  gint timeout;                    /* timeout in seconds before screen locks */
  gboolean locked;
} PhoshLockscreenManagerPrivate;


typedef struct _PhoshLockscreenManager {
  GObject parent;
} PhoshLockscreenManager;


G_DEFINE_TYPE_WITH_PRIVATE (PhoshLockscreenManager, phosh_lockscreen_manager, G_TYPE_OBJECT)


static void
lockscreen_unlock_cb (PhoshLockscreenManager *self, PhoshLockscreen *lockscreen)
{
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (lockscreen));
  g_return_if_fail (lockscreen == PHOSH_LOCKSCREEN (priv->lockscreen));

  if (priv->unlock_handler_id) {
    g_signal_handler_disconnect (lockscreen, priv->unlock_handler_id);
    priv->unlock_handler_id = 0;
  }
  g_clear_pointer (&priv->lockscreen, phosh_cp_widget_destroy);

  /* Unlock all other outputs */
  g_clear_pointer (&priv->shields, g_ptr_array_unref);

  priv->locked = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_LOCKSCREEN_MANAGER_PROP_LOCKED]);
}


static void
lockscreen_lock (PhoshLockscreenManager *self)
{
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);
  PhoshMonitor *primary_monitor;
  PhoshWayland *wl = phosh_wayland_get_default ();
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshMonitorManager *monitor_manager = phosh_shell_get_monitor_manager (shell);

  g_return_if_fail (!priv->locked);

  primary_monitor = phosh_shell_get_primary_monitor (shell);
  g_return_if_fail (primary_monitor);

  /* The primary output gets the clock, keypad, ... */
  priv->lockscreen = PHOSH_LOCKSCREEN (phosh_lockscreen_new (
                                         phosh_wayland_get_zwlr_layer_shell_v1(wl),
                                         primary_monitor->wl_output));

  /* Lock all other outputs */
  priv->shields = g_ptr_array_new_with_free_func ((GDestroyNotify) (gtk_widget_destroy));

  for (int i = 0; i < phosh_monitor_manager_get_num_monitors (monitor_manager); i++) {
    PhoshMonitor *monitor = phosh_monitor_manager_get_monitor (monitor_manager, i);

    if (monitor == NULL || monitor == primary_monitor)
      continue;
    g_ptr_array_add (priv->shields, phosh_lockshield_new (
                       phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                       monitor->wl_output));
  }

  priv->unlock_handler_id = g_signal_connect_swapped (
    priv->lockscreen,
    "lockscreen-unlock",
    G_CALLBACK(lockscreen_unlock_cb),
    self);

  priv->locked = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_LOCKSCREEN_MANAGER_PROP_LOCKED]);
}


static void
presence_status_changed_cb (PhoshLockscreenManager *self, guint32 status, gpointer *data)
{
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));

  g_debug ("Presence status changed: %d", status);
  phosh_lockscreen_manager_set_locked (self, TRUE);
}


static void
phosh_lockscreen_manager_set_property (GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);

  switch (property_id) {
  case PHOSH_LOCKSCREEN_MANAGER_PROP_LOCKED:
    phosh_lockscreen_manager_set_locked (self, g_value_get_boolean (value));
    break;
  case PHOSH_LOCKSCREEN_MANAGER_PROP_TIMEOUT:
    phosh_lockscreen_manager_set_timeout (self, g_value_get_int (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_lockscreen_manager_get_property (GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private(self);

  switch (property_id) {
  case PHOSH_LOCKSCREEN_MANAGER_PROP_LOCKED:
    g_value_set_boolean (value, priv->locked);
    break;
  case PHOSH_LOCKSCREEN_MANAGER_PROP_TIMEOUT:
    g_value_set_uint (value, priv->timeout);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_lockscreen_manager_dispose (GObject *object)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  g_clear_pointer (&priv->shields, g_ptr_array_unref);
  if (priv->lockscreen) {
    if (priv->unlock_handler_id) {
      g_signal_handler_disconnect (priv->lockscreen, priv->unlock_handler_id);
      priv->unlock_handler_id = 0;
    }
    g_clear_pointer (&priv->lockscreen, phosh_cp_widget_destroy);
  }
  g_clear_object (&priv->settings);

  G_OBJECT_CLASS (phosh_lockscreen_manager_parent_class)->dispose (object);
}


static void
phosh_lockscreen_manager_constructed (GObject *object)
{
  PhoshLockscreenManager *self = PHOSH_LOCKSCREEN_MANAGER (object);
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  G_OBJECT_CLASS (phosh_lockscreen_manager_parent_class)->constructed (object);

  priv->settings = g_settings_new ("org.gnome.desktop.session");
  g_settings_bind (priv->settings, "idle-delay", self, "timeout", G_SETTINGS_BIND_GET);

  priv->presence = phosh_session_presence_get_default_failable ();
  if (priv->presence) {
    g_signal_connect_swapped (priv->presence,
                              "status-changed",
                              (GCallback) presence_status_changed_cb,
                              self);
  }
}


static void
phosh_lockscreen_manager_class_init (PhoshLockscreenManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_lockscreen_manager_constructed;
  object_class->dispose = phosh_lockscreen_manager_dispose;

  object_class->set_property = phosh_lockscreen_manager_set_property;
  object_class->get_property = phosh_lockscreen_manager_get_property;

  props[PHOSH_LOCKSCREEN_MANAGER_PROP_LOCKED] =
    g_param_spec_boolean ("locked",
                          "Locked",
                          "Whether the screen is locked",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  props[PHOSH_LOCKSCREEN_MANAGER_PROP_TIMEOUT] =
    g_param_spec_int ("timeout",
                      "Timeout",
                      "Idle timeout in seconds until screen locks",
                      0,
                      G_MAXINT,
                      300,
                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PHOSH_LOCKSCREEN_MANAGER_PROP_LAST_PROP, props);
}


static void
phosh_lockscreen_manager_init (PhoshLockscreenManager *self)
{
}


PhoshLockscreenManager *
phosh_lockscreen_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_LOCKSCREEN_MANAGER, NULL);
}


void
phosh_lockscreen_manager_set_locked (PhoshLockscreenManager *self, gboolean state)
{
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));
  if (state == priv->locked)
    return;

  if (state)
    lockscreen_lock (self);
  else
    lockscreen_unlock_cb (self, PHOSH_LOCKSCREEN (priv->lockscreen));
}


gboolean
phosh_lockscreen_manager_get_locked (PhoshLockscreenManager *self)
{
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), FALSE);
  return priv->locked;
}


void
phosh_lockscreen_manager_set_timeout (PhoshLockscreenManager *self, gint timeout)
{
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self));
  if (timeout == priv->timeout)
    return;

  g_debug("Setting lock screen idle timeout to %d seconds", timeout);
  priv->timeout = timeout;

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_LOCKSCREEN_MANAGER_PROP_TIMEOUT]);
}


gint
phosh_lockscreen_manager_get_timeout (PhoshLockscreenManager *self)
{
  PhoshLockscreenManagerPrivate *priv = phosh_lockscreen_manager_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self), 0);
  return priv->timeout;
}
