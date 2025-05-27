// appchooser.c: org.freedesktop.impl.portal.AppChooser
//
// SPDX-FileCopyrightText: 2025 Valve Corporation
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "config.h"

#include "appchooser.h"
#include "request.h"

#include "utils.h"
#include "xdg-desktop-portal-dbus.h"

#include <gio/gdesktopappinfo.h>

#define I_(str) g_intern_static_string ((str))

static bool
handle_choose_application (XdpImplAppChooser *object,
                           GDBusMethodInvocation *invocation,
                           const char *arg_handle,
                           const char *arg_app_id,
                           const char */*arg_parent_window*/,
                           const char **/*choices*/,
                           GVariant */*arg_options*/)
{
  const char *sender = g_dbus_method_invocation_get_sender (invocation);
  g_autoptr(Request) request = request_new (sender, arg_app_id, arg_handle);
  g_object_ref (request);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));
  // The frontend (xdg-desktop-portal) expects a request to be exported for the
  // duration of the user interaction. There is no user interaction here.
  request_unexport (request);

  // Sanity check: verify that the Steam helper application exists
  // and can be located by its desktop file.
  g_autoptr(GAppInfo) info = G_APP_INFO (g_desktop_app_info_new (I_("steam_http_loader.desktop")));
  if (!info)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             XDG_DESKTOP_PORTAL_ERROR,
                                             XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                             "Unable to locate Steam helper to open files");
    }
  else
    {
      GVariantBuilder opt_builder;
      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&opt_builder, "{sv}", "choice", g_variant_new_string (I_("steam_http_loader")));
      xdp_impl_app_chooser_complete_choose_application (object,
                                                        invocation,
                                                        0, // Success, the request is carried out
                                                        g_variant_builder_end (&opt_builder));
    }

  g_object_unref (request);

  return true;
}

static bool
handle_update_choices (XdpImplAppChooser */*object*/,
                       GDBusMethodInvocation *invocation,
                       const char */*arg_handle*/,
                       const char **/*choices*/)
{
  g_dbus_method_invocation_return_error (invocation,
                                         XDG_DESKTOP_PORTAL_ERROR,
                                         XDG_DESKTOP_PORTAL_ERROR_NOT_ALLOWED,
                                         "Not implemented.");

  return true;
}

bool
app_chooser_init (GDBusConnection *connection,
                  GError **error)
{
  GDBusInterfaceSkeleton *helper =
    G_DBUS_INTERFACE_SKELETON (xdp_impl_app_chooser_skeleton_new ());

  g_signal_connect (helper, "handle-choose-application", G_CALLBACK (handle_choose_application), NULL);
  g_signal_connect (helper, "handle-update-choices", G_CALLBACK (handle_update_choices), NULL);

  if (!g_dbus_interface_skeleton_export (helper, connection, DESKTOP_PORTAL_OBJECT_PATH, error))
    {
      return false;
    }

  g_debug ("Providing implementation for interface: %s",
           g_dbus_interface_skeleton_get_info (helper)->name);

  return true;
}
