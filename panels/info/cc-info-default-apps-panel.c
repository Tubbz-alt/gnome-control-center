/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Mohammed Sadiq <sadiq@sadiqpk.org>
 * Copyright (C) 2010 Red Hat, Inc
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include "cc-info-panel.h"
#include "cc-info-resources.h"
#include "info-cleanup.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixmounts.h>
#include <gio/gdesktopappinfo.h>

#include <glibtop/fsusage.h>
#include <glibtop/mountlist.h>
#include <glibtop/mem.h>
#include <glibtop/sysinfo.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "gsd-disk-space-helper.h"

#include "cc-info-default-apps-panel.h"

typedef struct
{
  const char *content_type;
  gint label_offset;
  /* A pattern used to filter supported mime types
     when changing preferred applications. NULL
     means no other types should be changed */
  const char *extra_type_filter;
} DefaultAppData;

struct _CcInfoDefaultAppsPanel
{
  GtkBox     parent_instance;

  GtkWidget *default_apps_grid;

  GtkWidget *web_label;
  GtkWidget *mail_label;
  GtkWidget *calendar_label;
  GtkWidget *music_label;
  GtkWidget *video_label;
  GtkWidget *photos_label;
};


G_DEFINE_TYPE (CcInfoDefaultAppsPanel, cc_info_default_apps_panel, GTK_TYPE_BOX)

static void
default_app_changed (GtkAppChooserButton    *button,
                     CcInfoDefaultAppsPanel *self)
{
  GAppInfo *info;
  GError *error = NULL;
  DefaultAppData *app_data;
  int i;

  info = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (button));
  app_data = g_object_get_data (G_OBJECT (button), "cc-default-app-data");

  if (g_app_info_set_as_default_for_type (info, app_data->content_type, &error) == FALSE)
    {
      g_warning ("Failed to set '%s' as the default application for '%s': %s",
                 g_app_info_get_name (info), app_data->content_type, error->message);
      g_error_free (error);
      error = NULL;
    }
  else
    {
      g_debug ("Set '%s' as the default handler for '%s'",
               g_app_info_get_name (info), app_data->content_type);
    }

  if (app_data->extra_type_filter)
    {
      const char *const *mime_types;
      GPatternSpec *pattern;

      pattern = g_pattern_spec_new (app_data->extra_type_filter);
      mime_types = g_app_info_get_supported_types (info);

      for (i = 0; mime_types && mime_types[i]; i++)
        {
          if (!g_pattern_match_string (pattern, mime_types[i]))
            continue;

          if (g_app_info_set_as_default_for_type (info, mime_types[i], &error) == FALSE)
            {
              g_warning ("Failed to set '%s' as the default application for secondary "
                         "content type '%s': %s",
                         g_app_info_get_name (info), mime_types[i], error->message);
              g_error_free (error);
            }
          else
            {
              g_debug ("Set '%s' as the default handler for '%s'",
              g_app_info_get_name (info), mime_types[i]);
            }
        }

      g_pattern_spec_free (pattern);
    }

  g_object_unref (info);
}

#define OFFSET(x)             (G_STRUCT_OFFSET (CcInfoDefaultAppsPanel, x))
#define WIDGET_FROM_OFFSET(x) (G_STRUCT_MEMBER (GtkWidget*, self, x))

static void
info_panel_setup_default_app (CcInfoDefaultAppsPanel *self,
                              DefaultAppData         *data,
                              guint                   left_attach,
                              guint                   top_attach)
{
  GtkWidget *button;
  GtkWidget *label;

  button = gtk_app_chooser_button_new (data->content_type);
  g_object_set_data (G_OBJECT (button), "cc-default-app-data", data);

  gtk_app_chooser_button_set_show_default_item (GTK_APP_CHOOSER_BUTTON (button), TRUE);
  gtk_grid_attach (GTK_GRID (self->default_apps_grid), button, left_attach, top_attach,
                   1, 1);
  g_signal_connect (G_OBJECT (button), "changed",
                    G_CALLBACK (default_app_changed), self);
  gtk_widget_show (button);

  label = WIDGET_FROM_OFFSET (data->label_offset);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), button);
}

static DefaultAppData preferred_app_infos[] = {
  /* for web, we need to support text/html,
     application/xhtml+xml and x-scheme-handler/https,
     hence the "*" pattern
  */
  { "x-scheme-handler/http", OFFSET (web_label), "*" },
  { "x-scheme-handler/mailto", OFFSET (mail_label), NULL },
  { "text/calendar", OFFSET (calendar_label), NULL },
  { "audio/x-vorbis+ogg", OFFSET (music_label), "audio/*" },
  { "video/x-ogm+ogg", OFFSET (video_label), "video/*" },
  { "image/jpeg", OFFSET (photos_label), "image/*" }
};

static void
info_panel_setup_default_apps (CcInfoDefaultAppsPanel *self)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (preferred_app_infos); i++)
    {
      info_panel_setup_default_app (self, &preferred_app_infos[i],
                                    1, i);
    }
}

static void
cc_info_default_apps_panel_class_init (CcInfoDefaultAppsPanelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/info/info-default-apps.ui");
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, default_apps_grid);
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, web_label);
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, mail_label);
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, calendar_label);
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, music_label);
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, video_label);
  gtk_widget_class_bind_template_child (widget_class, CcInfoDefaultAppsPanel, photos_label);
}

static void
cc_info_default_apps_panel_init (CcInfoDefaultAppsPanel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  info_panel_setup_default_apps (self);
}

GtkWidget *
cc_info_default_apps_panel_new (void)
{
  return g_object_new (CC_TYPE_INFO_DEFAULT_APPS_PANEL,
                       NULL);
}