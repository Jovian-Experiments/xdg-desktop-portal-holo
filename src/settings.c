// settings.c: org.freedesktop.impl.portal.Settings
//
// SPDX-FileCopyrightText: 2024-2026 Valve Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "config.h"

#include "settings.h"

#include "utils.h"
#include "xdg-desktop-portal-dbus.h"

G_DECLARE_FINAL_TYPE (SettingsManager, settings_manager, SETTINGS, MANAGER, GObject)

struct _SettingsManager
{
  GObject parent_instance;

  GDBusInterfaceSkeleton *helper;

  /* HashTable<unowned str, SettingNamespace> */
  GHashTable *keys;

  GFileMonitor *file_monitor;
};

typedef enum
{
  SETTING_STRING_VALUE,
  SETTING_INT_VALUE,
  SETTING_UINT_VALUE,
  SETTING_COLOR_VALUE
} SettingValueType;

typedef struct
{
  SettingValueType value_type;
  char *namespace;
  char *key;

  union {
    char *v_str;
    int v_int;
    unsigned int v_uint;
    struct {
      double red;
      double green;
      double blue;
    } v_color;
  } value;
} SettingValue;

typedef struct
{
  char *namespace;

  /* HashTable<unowned str, SettingValue> */
  GHashTable *keys;
} SettingNamespace;

static SettingsManager *manager;

#if 0
static SettingValue *
setting_value_new_int (const char *namespace,
                       const char *key,
                       int value)
{
  SettingValue *v = g_new0 (SettingValue, 1);

  v->value_type = SETTING_INT_VALUE;
  v->namespace = g_strdup (namespace);
  v->key = g_strdup (key);
  v->value.v_int = value;

  return v;
}
#endif

static SettingValue *
setting_value_new_uint (const char *namespace,
                        const char *key,
                        unsigned int value)
{
  SettingValue *v = g_new0 (SettingValue, 1);

  v->value_type = SETTING_UINT_VALUE;
  v->namespace = g_strdup (namespace);
  v->key = g_strdup (key);
  v->value.v_uint = value;

  return v;
}

#if 0
static SettingValue *
setting_value_new_string (const char *namespace,
                          const char *key,
                          const char *value)
{
  SettingValue *v = g_new0 (SettingValue, 1);

  v->value_type = SETTING_STRING_VALUE;
  v->namespace = g_strdup (namespace);
  v->key = g_strdup (key);
  v->value.v_str = g_strdup (value);

  return v;
}
#endif

static SettingValue *
setting_value_new_color (const char *namespace,
                         const char *key,
                         size_t n_colors,
                         double *colors)
{
  SettingValue *v = g_new0 (SettingValue, 1);

  v->value_type = SETTING_COLOR_VALUE;
  v->namespace = g_strdup (namespace);
  v->key = g_strdup (key);
  v->value.v_color.red = n_colors > 0 ? colors[0] : 0.0;
  v->value.v_color.green = n_colors > 1 ? colors[1] : 0.0;
  v->value.v_color.blue = n_colors > 2 ? colors[2] : 0.0;

  return v;
}

static void
setting_value_free (gpointer data)
{
  if (data != NULL)
    {
      SettingValue *value = data;

      switch (value->value_type)
        {
        case SETTING_STRING_VALUE:
          g_free (value->value.v_str);
          break;

        case SETTING_INT_VALUE:
        case SETTING_UINT_VALUE:
        case SETTING_COLOR_VALUE:
          break;

        default:
          g_assert_not_reached ();
          break;
        }

      g_free (value->namespace);
      g_free (value->key);
      g_free (value);
    }
}

static GVariant *
setting_value_to_gvariant (SettingValue *value)
{
  GVariant *res;

  switch (value->value_type)
    {
    case SETTING_STRING_VALUE:
      res = g_variant_new_string (value->value.v_str);
      break;

    case SETTING_INT_VALUE:
      res = g_variant_new_int32 (value->value.v_int);
      break;

    case SETTING_UINT_VALUE:
      res = g_variant_new_uint32 (value->value.v_uint);
      break;

    case SETTING_COLOR_VALUE:
      res = g_variant_new ("(ddd)",
                           value->value.v_color.red,
                           value->value.v_color.green,
                           value->value.v_color.blue);
      break;

    default:
      g_assert_not_reached ();
    }

  return res;
}

static SettingNamespace *
setting_namespace_new (const char *namespace)
{
  SettingNamespace *ns = g_new0 (SettingNamespace, 1);

  ns->namespace = g_strdup (namespace);
  ns->keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, setting_value_free);

  return ns;
}

static void
setting_namespace_free (gpointer data)
{
  if (data != NULL)
    {
      SettingNamespace *ns = data;

      g_free (ns->namespace);
      g_hash_table_unref (ns->keys);
      g_free (ns);
    }
}

static bool
namespace_matches (const char         *namespace,
                   const char * const *patterns)
{
  size_t i;

  for (i = 0; patterns[i]; ++i)
    {
      size_t pattern_len;
      const char *pattern = patterns[i];
      if (pattern[0] == '\0')
        return true;

      if (strcmp (namespace, pattern) == 0)
        return true;

      pattern_len = strlen (pattern);
      if (pattern[pattern_len - 1] == '*' && strncmp (namespace, pattern, pattern_len - 1) == 0)
        return true;
    }

  if (i == 0) /* Empty array */
    return true;

  return false;
}

static inline bool
settings_manager_set_key (SettingsManager *self,
                          SettingValue *value)
{
  SettingNamespace *ns = g_hash_table_lookup (self->keys, value->namespace);
  if (ns == NULL)
    {
      ns = setting_namespace_new (value->namespace);
      g_hash_table_insert (self->keys, value->namespace, ns);
    }

  return g_hash_table_replace (ns->keys, value->key, value);
}

static inline SettingValue *
settings_manager_get_key (SettingsManager *self,
                          const char *namespace,
                          const char *key)
{
  SettingNamespace *ns = g_hash_table_lookup (self->keys, namespace);
  if (ns == NULL)
    return NULL;

  return g_hash_table_lookup (ns->keys, key);
}

static bool load_settings_config (SettingsManager *settings_manager,
                                  bool notify);

static void
settings_manager__file_monitor__changed (GFileMonitor *monitor,
                                         GFile *file,
                                         GFile *other_file,
                                         gpointer user_data)
{
  load_settings_config (user_data, true);
}

static void
load_settings (SettingsManager *settings_manager,
               GKeyFile *key_file,
               bool notify)
{
  gsize n_groups;
  g_auto (GStrv) groups = g_key_file_get_groups (key_file, &n_groups);

  for (gsize i = 0; groups[i] != NULL; i++)
    {
      if (strcmp (groups[i], "org.freedesktop.appearance") == 0)
        {
          SettingValue *new_value;

          // casting from int to unsigned int is fine because valid values for color-scheme are 0, 1 and 2
          unsigned int color_scheme = g_key_file_get_integer (key_file, groups[i], "color-scheme", NULL);
          new_value = setting_value_new_uint ("org.freedesktop.appearance", "color-scheme", color_scheme);
          if (settings_manager_set_key (settings_manager, new_value) && notify)
            xdp_impl_settings_emit_setting_changed (XDP_IMPL_SETTINGS (settings_manager->helper),
                                                    new_value->namespace,
                                                    new_value->key,
                                                    g_variant_new ("(v)", g_variant_new_uint32 (new_value->value.v_uint)));

          // casting from int to unsigned int is fine because valid values for contrast are 0 and 1
          unsigned int contrast = g_key_file_get_integer (key_file, groups[i], "contrast", NULL);
          new_value = setting_value_new_uint ("org.freedesktop.appearance", "contrast", contrast);
          if (settings_manager_set_key (settings_manager, new_value) && notify)
            xdp_impl_settings_emit_setting_changed (XDP_IMPL_SETTINGS (settings_manager->helper),
                                                    new_value->namespace,
                                                    new_value->key,
                                                    g_variant_new ("(v)", g_variant_new_uint32 (new_value->value.v_uint)));

          gsize n_items = 0;
          double *accent_color = g_key_file_get_double_list (key_file, groups[i], "accent-color", &n_items, NULL);
          new_value = setting_value_new_color ("org.freedesktop.appearance", "accent-color", n_items, accent_color);
          if (settings_manager_set_key (settings_manager, new_value) && notify)
            xdp_impl_settings_emit_setting_changed (XDP_IMPL_SETTINGS (settings_manager->helper),
                                                    new_value->namespace,
                                                    new_value->key,
                                                    g_variant_new ("(v)",
                                                                   g_variant_new ("(ddd)",
                                                                                  new_value->value.v_color.red,
                                                                                  new_value->value.v_color.green,
                                                                                  new_value->value.v_color.blue)));
          g_free (accent_color);
        }
    }
}

static bool
load_settings_config (SettingsManager *settings_manager,
                      bool notify)
{
  g_autoptr (GPtrArray) search_dirs = g_ptr_array_new_null_terminated (8, g_free, true);

  /* XDG_CONFIG_HOME/SteamOS/portal/settings.conf */
  GPathBuf buf;
  g_path_buf_init_from_path (&buf, g_get_user_config_dir ());
  g_path_buf_push (&buf, "SteamOS");
  g_path_buf_push (&buf, "portal");
  char *path = g_path_buf_clear_to_path (&buf);
  g_ptr_array_add (search_dirs, path);

  /* XDG_CONFIG_DIRS/SteamOS/portal/settings.conf */
  const char * const *system_dirs = g_get_system_config_dirs ();
  for (size_t i = 0; system_dirs[i] != NULL; i++)
    {
      g_path_buf_init_from_path (&buf, system_dirs[i]);
      g_path_buf_push (&buf, "SteamOS");
      g_path_buf_push (&buf, "portal");
      char *path = g_path_buf_clear_to_path (&buf);

      g_ptr_array_add (search_dirs, path);
    }

  g_autoptr (GKeyFile) kf = g_key_file_new ();
  g_autofree char *full_path = NULL;
  g_autoptr (GError) error = NULL;

  if (!g_key_file_load_from_dirs (kf, "settings.conf", (const char **) search_dirs->pdata, &full_path, G_KEY_FILE_NONE, &error))
    {
      g_debug ("Unable to read settings.conf: %s", error->message);
      return false;
    }

  g_debug ("Loading settings configuration from: %s", full_path);

  load_settings (settings_manager, kf, notify);

  if (settings_manager->file_monitor == NULL)
    {
      g_autoptr (GFile) file = g_file_new_for_path (full_path);
      settings_manager->file_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, &error);
      if (error != NULL)
        g_debug ("Unable to monitor settings.conf: %s", error->message);

      g_signal_connect (settings_manager->file_monitor, "changed", G_CALLBACK (settings_manager__file_monitor__changed), settings_manager);
    }

  return true;
}

G_DEFINE_TYPE (SettingsManager, settings_manager, G_TYPE_OBJECT)

enum
{
  N_PROPS
};

static void
settings_manager_constructed (GObject *gobject)
{
  load_settings_config ((SettingsManager *) gobject, false);
}

static void
settings_manager_finalize (GObject *gobject)
{
  SettingsManager *self = SETTINGS_MANAGER (gobject);

  g_clear_object (&self->file_monitor);
  g_clear_pointer (&self->keys, g_hash_table_unref);

  G_OBJECT_CLASS (settings_manager_parent_class)->finalize (gobject);
}

static void
settings_manager_class_init (SettingsManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = settings_manager_constructed;
  gobject_class->finalize = settings_manager_finalize;
}

static void
settings_manager_init (SettingsManager *self)
{
  self->keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, setting_namespace_free);
}

static gboolean
settings_handle_read (XdpImplSettings *object,
                      GDBusMethodInvocation *invocation,
                      const char *arg_namespace,
                      const char *arg_key,
                      gpointer data)
{
  g_debug ("Read %s %s", arg_namespace, arg_key);

  SettingsManager *self = data;

  const SettingValue *v = settings_manager_get_key (self, arg_namespace, arg_key);
  if (v == NULL)
    goto out;

  if (strcmp (arg_namespace, "org.freedesktop.appearance") == 0)
    {
      if (strcmp (arg_key, "color-scheme") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)",
                                                                g_variant_new_uint32 (v->value.v_uint)));
          return TRUE;
        }

      if (strcmp (arg_key, "contrast") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)",
                                                                g_variant_new_uint32 (v->value.v_uint)));
          return TRUE;
        }

      if (strcmp (arg_key, "accent-color") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)",
                                                                g_variant_new ("(ddd)",
                                                                               v->value.v_color.red,
                                                                               v->value.v_color.green,
                                                                               v->value.v_color.blue)));
          return TRUE;
        }
    }

out:
  g_debug ("Attempted to read unknown namespace/key pair: %s %s", arg_namespace, arg_key);
  g_dbus_method_invocation_return_error_literal (invocation, XDG_DESKTOP_PORTAL_ERROR,
                                                 XDG_DESKTOP_PORTAL_ERROR_NOT_FOUND,
                                                 "Requested setting not found");
  return FALSE;
}

static gboolean
settings_handle_read_all (XdpImplSettings *object,
                          GDBusMethodInvocation *invocation,
                          const char * const *arg_namespaces,
                          gpointer data)
{
  g_debug ("ReadAll");

  SettingsManager *self = data;

  g_autoptr(GVariantBuilder) builder = g_variant_builder_new (G_VARIANT_TYPE ("(a{sa{sv}})"));
  GHashTableIter ns_iter;
  SettingNamespace *ns;

  g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sa{sv}}"));

  g_hash_table_iter_init (&ns_iter, self->keys);
  while (g_hash_table_iter_next (&ns_iter, NULL, (gpointer *) &ns))
    {
      GVariantDict dict;

      if (!namespace_matches (ns->namespace, arg_namespaces))
        continue;

      g_variant_dict_init (&dict, NULL);

      GHashTableIter key_iter;
      SettingValue *value;
      g_hash_table_iter_init (&key_iter, ns->keys);
      while (g_hash_table_iter_next (&key_iter, NULL, (gpointer *) &value))
        g_variant_dict_insert_value (&dict, value->key, setting_value_to_gvariant (value));

      g_variant_builder_add (builder, "{s@a{sv}}", ns->namespace, g_variant_dict_end (&dict));
    }

  g_variant_builder_close (builder);

  g_dbus_method_invocation_return_value (invocation, g_variant_builder_end (builder));

  return TRUE;
}

bool
settings_init (GDBusConnection *connection,
               GError **error)
{
  if (g_once_init_enter_pointer (&manager))
    {
      GDBusInterfaceSkeleton *helper =
        G_DBUS_INTERFACE_SKELETON (xdp_impl_settings_skeleton_new ());

      SettingsManager *res = g_object_new (settings_manager_get_type (), NULL);
      res->helper = helper;

      g_signal_connect (helper, "handle-read", G_CALLBACK (settings_handle_read), res);
      g_signal_connect (helper, "handle-read-all", G_CALLBACK (settings_handle_read_all), res);

      if (!g_dbus_interface_skeleton_export (helper, connection, DESKTOP_PORTAL_OBJECT_PATH, error))
        {
          g_object_unref (res);
          return false;
        }

      g_debug ("Providing implementation for interface: %s", g_dbus_interface_skeleton_get_info (helper)->name);

      g_once_init_leave_pointer (&manager, res);
    }

  return true;
}
