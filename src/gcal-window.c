/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-window.c
 * Copyright (C) 2015 Erick Pérez Castellanos <erickpc@gnome.org>
 * Copyright (C) 2014 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * gnome-calendar is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gnome-calendar is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gcal-window.h"

#include "gcal-manager.h"
#include "gcal-view.h"
#include "gcal-month-view.h"
#include "gcal-week-view.h"
#include "gcal-year-view.h"
#include "gcal-search-view.h"
#include "gcal-event-widget.h"
#include "gcal-edit-dialog.h"
#include "gcal-enum-types.h"
#include "gcal-source-dialog.h"

#include <glib/gi18n.h>

#include <libecal/libecal.h>
#include <libical/icaltime.h>

typedef struct
{
  gint               x;
  gint               y;
  icaltimetype      *start_date;
  icaltimetype      *end_date;
} NewEventData;

typedef struct
{
  GcalWindow *window;
  gchar *uuid;
} OpenEditDialogData;

typedef struct
{
  /* timeout ids */
  guint                save_geometry_timeout_id;
  guint                notification_timeout;

  /* upper level widgets */
  GtkWidget           *main_box;

  GtkWidget           *header_bar;
  GtkWidget           *search_bar;
  GtkWidget           *views_overlay;
  GtkWidget           *views_stack;
  GtkWidget           *month_view;
  GtkWidget           *year_view;
  GtkWidget           *notification;
  GtkWidget           *notification_label;
  GtkWidget           *notification_action_button;
  GtkWidget           *notification_close_button;

  /* header_bar widets */
  GtkWidget           *menu_button;
  GtkWidget           *search_button;
  GtkWidget           *calendars_button;
  GtkWidget           *search_entry;
  GtkWidget           *back_button;
  GtkWidget           *today_button;
  GtkWidget           *forward_button;
  GtkWidget           *views_switcher;

  /* new event popover widgets */
  GtkWidget           *popover;
  GtkWidget           *new_event_title_label;
  GtkWidget           *new_event_create_button;
  GtkWidget           *new_event_details_button;
  GtkWidget           *new_event_what_entry;

  GtkWidget           *search_view;

  /* day, week, month, year, list */
  GtkWidget           *views [6];
  GtkWidget           *edit_dialog;

  GcalManager         *manager;
  GcalWindowViewType   active_view;
  icaltimetype        *active_date;

  icaltimetype        *current_date;

  /* states */
  gboolean             new_event_mode;
  gboolean             search_mode;
  gboolean             leaving_search_mode;

  NewEventData        *event_creation_data;

  GcalEventData       *event_to_delete;

  /* calendar management */
  GtkWidget           *calendar_popover;
  GtkWidget           *calendar_listbox;
  GtkWidget           *source_dialog;
  gint                 refresh_timeout;
  gint                 refresh_timeout_id;

  /* temp to keep event_creation */
  gboolean             open_edit_dialog;
} GcalWindowPrivate;

enum
{
  PROP_0,
  PROP_ACTIVE_VIEW,
  PROP_MANAGER,
  PROP_ACTIVE_DATE,
  PROP_NEW_EVENT_MODE
};

#define SAVE_GEOMETRY_ID_TIMEOUT 100 /* ms */
#define FAST_REFRESH_TIMEOUT     900000 /* ms */
#define SLOW_REFRESH_TIMEOUT     3600000 /* ms */

static gboolean       key_pressed                        (GtkWidget           *widget,
                                                          GdkEvent            *event,
                                                          gpointer             user_data);

static void           date_updated                       (GtkButton           *buttton,
                                                          gpointer             user_data);

static void           search_event_selected              (GcalSearchView      *search_view,
                                                          icaltimetype        *date,
                                                          gpointer             user_data);

static gint           calendar_listbox_sort_func         (GtkListBoxRow       *row1,
                                                          GtkListBoxRow       *row2,
                                                          gpointer             user_data);

static void           load_geometry                      (GcalWindow          *window);

static gboolean       save_geometry                      (gpointer             user_data);

static void           view_changed                       (GObject             *object,
                                                          GParamSpec          *pspec,
                                                          gpointer             user_data);

static void           set_new_event_mode                 (GcalWindow          *window,
                                                          gboolean             enabled);

static void           show_new_event_widget              (GcalView            *view,
                                                          gpointer             start_span,
                                                          gpointer             end_span,
                                                          gdouble              x,
                                                          gdouble              y,
                                                          gpointer             user_data);

static void           prepare_new_event_widget           (GcalWindow          *window);

static void           place_new_event_widget             (GcalWindow          *window,
                                                          gint                 x,
                                                          gint                 y);

static void           close_new_event_widget             (GtkButton           *button,
                                                          gpointer             user_data);

static void           new_event_entry_text_changed       (GObject             *object,
                                                          GParamSpec          *pspec,
                                                          gpointer             user_data);

static void           create_notification                (GcalWindow          *window,
                                                          gchar               *message,
                                                          gchar               *button_label);

static void           hide_notification                  (GcalWindow          *window,
                                                          GtkWidget           *button);

/* calendar management */
static void           add_source                         (GcalManager         *manager,
                                                          ESource             *source,
                                                          gboolean             enabled,
                                                          gpointer             user_data);


static GtkWidget*     make_row_from_source               (GcalWindow          *window,
                                                          ESource             *source);

static void           remove_source                      (GcalManager         *manager,
                                                          ESource             *source,
                                                          gpointer             user_data);

static void           show_source_dialog                 (GtkButton           *button,
                                                          gpointer             user_data);

static void           source_row_activated               (GtkListBox          *listbox,
                                                          GtkListBoxRow       *row,
                                                          gpointer             user_data);

static void           source_enabled                     (GcalManager         *manager,
                                                          ESource             *source,
                                                          gboolean             enabled,
                                                          gpointer             user_data);


static void           on_calendar_toggled                (GObject             *object,
                                                          GParamSpec          *pspec,
                                                          gpointer             user_data);

static gboolean       refresh_sources                    (GcalWindow          *window);

static gboolean       window_state_changed               (GtkWidget           *window,
                                                          GdkEvent            *event,
                                                          gpointer             user_data);

/* handling events interaction */
static void           create_event                       (gpointer             user_data,
                                                          GtkWidget           *widget);

static void           create_event_detailed_cb           (GcalView            *view,
                                                          gpointer             start_span,
                                                          gpointer             end_span,
                                                          gpointer             user_data);

static void           event_activated                    (GcalView            *view,
                                                          GcalEventWidget     *event_widget,
                                                          gpointer             user_data);

static void           edit_dialog_closed                 (GtkDialog           *dialog,
                                                          gint                 response,
                                                          gpointer             user_data);

static void           search_toggled                     (GObject             *object,
                                                          GParamSpec          *pspec,
                                                          gpointer             user_data);

static void           search_changed                     (GtkEditable         *editable,
                                                          gpointer             user_data);

static void           search_bar_revealer_toggled        (GObject             *object,
                                                          GParamSpec          *pspec,
                                                          gpointer             user_data);

static void           remove_event                       (GtkWidget           *notification,
                                                          GParamSpec          *spec,
                                                          gpointer             user_data);

static void           undo_remove_action                 (GtkButton           *button,
                                                          gpointer             user_data);

static void           gcal_window_constructed            (GObject             *object);

static void           gcal_window_finalize               (GObject             *object);

static void           gcal_window_set_property           (GObject             *object,
                                                          guint                property_id,
                                                          const GValue        *value,
                                                          GParamSpec          *pspec);

static void           gcal_window_get_property           (GObject             *object,
                                                          guint                property_id,
                                                          GValue              *value,
                                                          GParamSpec          *pspec);

static gboolean       gcal_window_configure_event        (GtkWidget           *widget,
                                                          GdkEventConfigure   *event);

static gboolean       gcal_window_state_event            (GtkWidget           *widget,
                                                          GdkEventWindowState *event);

G_DEFINE_TYPE_WITH_PRIVATE (GcalWindow, gcal_window, GTK_TYPE_APPLICATION_WINDOW)

static gboolean
key_pressed (GtkWidget *widget,
             GdkEvent  *event,
             gpointer   user_data)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  /* special case: creating an event */
  if (priv->new_event_mode)
    return GDK_EVENT_PROPAGATE;

  return gtk_search_bar_handle_event (GTK_SEARCH_BAR (priv->search_bar),
                                      event);
}

static void
update_active_date (GcalWindow   *window,
                    icaltimetype *new_date)
{
  GcalWindowPrivate *priv;

  time_t range_start, range_end;
  icaltimetype date;
  icaltimetype *previous_date;
  icaltimezone* default_zone;

  priv = gcal_window_get_instance_private (window);
  default_zone = gcal_manager_get_system_timezone (priv->manager);

  previous_date = priv->active_date;
  priv->active_date = new_date;
  g_object_notify (G_OBJECT (window), "active-date");

  /* year_view */
  if (previous_date->year != new_date->year)
    {
      date = *new_date;
      date.day = 1;
      date.month = 1;
      date.hour = 0;
      date.minute = 0;
      date.second = 0;
      date.is_date = 0;
      range_start = icaltime_as_timet_with_zone (date, default_zone);

      date.day = 31;
      date.month = 12;
      date.hour = 23;
      date.minute = 59;
      range_end = icaltime_as_timet_with_zone (date, default_zone);

      gcal_manager_set_subscriber (priv->manager, E_CAL_DATA_MODEL_SUBSCRIBER (priv->year_view), range_start, range_end);
    }

  /* month_view */
  if (previous_date->month != new_date->month || previous_date->year != new_date->year)
    {
      date = *new_date;
      date.day = 1;
      date.hour = 0;
      date.minute = 0;
      date.second = 0;
      date.is_date = 0;
      range_start = icaltime_as_timet_with_zone (date, default_zone);

      date.day = time_days_in_month (new_date->year, new_date->month - 1);
      date.hour = 23;
      date.minute = 59;
      range_end = icaltime_as_timet_with_zone (date, default_zone);

      gcal_manager_set_subscriber (priv->manager, E_CAL_DATA_MODEL_SUBSCRIBER (priv->month_view), range_start, range_end);
    }

  g_free (previous_date);
}

static gboolean
update_current_date (GcalWindow *window)
{
  GcalWindowPrivate *priv = gcal_window_get_instance_private (window);
  guint seconds;

  if (priv->current_date == NULL)
    priv->current_date = g_new0 (icaltimetype, 1);

  *(priv->current_date) = icaltime_current_time_with_zone (gcal_manager_get_system_timezone (priv->manager));
  *(priv->current_date) = icaltime_set_timezone (priv->current_date, gcal_manager_get_system_timezone (priv->manager));

  gcal_month_view_set_current_date (GCAL_MONTH_VIEW (priv->month_view), priv->current_date);
  gcal_year_view_set_current_date (GCAL_YEAR_VIEW (priv->year_view), priv->current_date);

  seconds = 24 * 60 * 60 - (icaltime_as_timet (*(priv->current_date)) % (24 * 60 * 60));
  g_timeout_add_seconds (seconds, (GSourceFunc) update_current_date, window);
  return FALSE;
}

static void
date_updated (GtkButton  *button,
              gpointer    user_data)
{
  GcalWindowPrivate *priv;

  icaltimetype *new_date;
  gboolean move_back, move_today;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  move_today = priv->today_button == (GtkWidget*) button;
  move_back = priv->back_button == (GtkWidget*) button;

  new_date = gcal_dup_icaltime (priv->active_date);

  if (move_today)
    {
      *new_date = *(priv->current_date);
    }
  else
    {
      switch (priv->active_view)
        {
        case GCAL_WINDOW_VIEW_DAY:
          new_date->day += 1 * (move_back ? -1 : 1);
          break;
        case GCAL_WINDOW_VIEW_WEEK:
          new_date->day += 7 * (move_back ? -1 : 1);
          break;
        case GCAL_WINDOW_VIEW_MONTH:
          new_date->day = 1;
          new_date->month += 1 * (move_back ? -1 : 1);
          break;
        case GCAL_WINDOW_VIEW_YEAR:
          new_date->year += 1 * (move_back ? -1 : 1);
          break;
        case GCAL_WINDOW_VIEW_LIST:
        case GCAL_WINDOW_VIEW_SEARCH:
          break;
        }

      *new_date = icaltime_normalize (*new_date);
    }

  update_active_date (user_data, new_date);
}

static void
search_event_selected (GcalSearchView *search_view,
                       icaltimetype   *date,
                       gpointer        user_data)
{
  g_object_set (user_data, "active-date", date, NULL);
  gcal_window_set_search_mode (GCAL_WINDOW (user_data), FALSE);
}

static gint
calendar_listbox_sort_func (GtkListBoxRow *row1,
                            GtkListBoxRow *row2,
                            gpointer       user_data)
{
  ESource *source1, *source2;

  source1 = g_object_get_data (G_OBJECT (row1), "source");
  source2 = g_object_get_data (G_OBJECT (row2), "source");

  if (source1 == NULL && source2 == NULL)
    return 0;

  return g_strcmp0 (e_source_get_display_name (source1), e_source_get_display_name (source2));
}

static void
load_geometry (GcalWindow *window)
{
  GcalApplication *app;
  GSettings *settings;
  GVariant *variant;
  gboolean maximized;
  const gint32 *position;
  const gint32 *size;
  gsize n_elements;

  app = GCAL_APPLICATION (gtk_window_get_application (GTK_WINDOW (window)));
  settings = gcal_application_get_settings (app);

  /* load window settings: size */
  variant = g_settings_get_value (settings,
                                  "window-size");
  size = g_variant_get_fixed_array (variant,
                                    &n_elements,
                                    sizeof (gint32));
  if (n_elements == 2)
    gtk_window_set_default_size (GTK_WINDOW (window),
                                 size[0],
                                 size[1]);
  g_variant_unref (variant);

  /* load window settings: position */
  variant = g_settings_get_value (settings,
                                  "window-position");
  position = g_variant_get_fixed_array (variant,
                                        &n_elements,
                                        sizeof (gint32));
  if (n_elements == 2)
    gtk_window_move (GTK_WINDOW (window),
                     position[0],
                     position[1]);

  g_variant_unref (variant);

  /* load window settings: state */
  maximized = g_settings_get_boolean (settings,
                                      "window-maximized");
  if (maximized)
    gtk_window_maximize (GTK_WINDOW (window));
}

static gboolean
save_geometry (gpointer user_data)
{
  GtkWindow *self;
  GdkWindow *window;
  GcalWindowPrivate *priv;
  GdkWindowState state;
  GcalApplication *app;
  GSettings *settings;
  gboolean maximized;
  GVariant *variant;
  gint32 size[2];
  gint32 position[2];

  self = GTK_WINDOW (user_data);

  window = gtk_widget_get_window (GTK_WIDGET (self));
  state = gdk_window_get_state (window);
  priv = gcal_window_get_instance_private (GCAL_WINDOW (self));

  app = GCAL_APPLICATION (gtk_window_get_application (self));
  settings = gcal_application_get_settings (app);

  /* save window's state */
  maximized = state & GDK_WINDOW_STATE_MAXIMIZED;
  g_settings_set_boolean (settings,
                          "window-maximized",
                          maximized);

  if (maximized)
    {
      priv->save_geometry_timeout_id = 0;
      return FALSE;
    }

  /* save window's size */
  gtk_window_get_size (self,
                       (gint *) &size[0],
                       (gint *) &size[1]);
  variant = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                       size,
                                       2,
                                       sizeof (size[0]));
  g_settings_set_value (settings,
                        "window-size",
                        variant);

  /* save windows's position */
  gtk_window_get_position (self,
                           (gint *) &position[0],
                           (gint *) &position[1]);
  variant = g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                       position,
                                       2,
                                       sizeof (position[0]));
  g_settings_set_value (settings,
                        "window-position",
                        variant);

  priv->save_geometry_timeout_id = 0;

  return FALSE;
}

/**
 * view_changed:
 * @object:
 * @pspec:
 * @user_data:
 *
 * Called every time the user activate the stack-switcher
 * Retrieve the enum value representing the view, update internal
 * @active_view with it
 **/
static void
view_changed (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
  GcalWindowPrivate *priv;
  GEnumClass *eklass;
  GEnumValue *eval;
  GcalWindowViewType view_type;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  /* XXX: this is the destruction process */
  if (!gtk_widget_get_visible (priv->views_stack))
    return;

  eklass = g_type_class_ref (gcal_window_view_type_get_type ());
  eval = g_enum_get_value_by_nick (
             eklass,
             gtk_stack_get_visible_child_name (GTK_STACK (priv->views_stack)));

  view_type = eval->value;

  g_type_class_unref (eklass);

  if (view_type == GCAL_WINDOW_VIEW_SEARCH)
    return;

  priv->active_view = view_type;
  g_object_notify (G_OBJECT (user_data), "active-view");
}

static void
set_new_event_mode (GcalWindow *window,
                    gboolean    enabled)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (window);
  priv->new_event_mode = enabled;
  g_object_notify (G_OBJECT (window), "new-event-mode");

  if (! enabled && priv->views[priv->active_view] != NULL)
    gcal_view_clear_marks (GCAL_VIEW (priv->views[priv->active_view]));

  /* XXX: here we could disable clicks from the views, yet */
  /* for now we relaunch the new-event widget */
  if (!enabled &&
      gtk_widget_is_visible (priv->popover))
    {
      gtk_widget_set_visible (priv->popover, FALSE);
    }
}

/* new-event interaction: second variant */
static void
show_new_event_widget (GcalView *view,
                       gpointer  start_span,
                       gpointer  end_span,
                       gdouble   x,
                       gdouble   y,
                       gpointer  user_data)
{
  GcalWindowPrivate *priv;

  g_return_if_fail (user_data);
  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  /* 1st and 2nd steps */
  set_new_event_mode (GCAL_WINDOW (user_data), TRUE);

  if (priv->event_creation_data != NULL)
    {
      g_free (priv->event_creation_data->start_date);
      g_free (priv->event_creation_data->end_date);
      g_free (priv->event_creation_data);
    }

  priv->event_creation_data = g_new0 (NewEventData, 1);
  priv->event_creation_data->x = x;
  priv->event_creation_data->y = y;
  priv->event_creation_data->start_date = gcal_dup_icaltime (start_span);
  if (end_span != NULL)
    priv->event_creation_data->end_date = gcal_dup_icaltime (end_span);
  g_debug ("[show_new_event] position (%f, %f)", x, y);

  /* Setup new event widget data */
  prepare_new_event_widget (GCAL_WINDOW (user_data));

  place_new_event_widget (GCAL_WINDOW (user_data), x, y);
}

static void
prepare_new_event_widget (GcalWindow *window)
{
  GcalWindowPrivate *priv;

  struct tm tm_date;
  gchar start[64];
  gchar *title_date;

  priv = gcal_window_get_instance_private (window);

  /* setting title */
  tm_date = icaltimetype_to_tm (priv->event_creation_data->start_date);
  /* Translators:
   * this is the format string for representing a date consisting of a month name
   * and a date of month. */
  e_utf8_strftime_fix_am_pm (start, 64, C_("event date format", "%B %d"), &tm_date);
  title_date = g_strdup_printf (_("New Event on %s"), start);

  gtk_label_set_text (GTK_LABEL (priv->new_event_title_label),
                      title_date);
  g_free (title_date);

  /* clear entry */
  gtk_entry_set_text (GTK_ENTRY (priv->new_event_what_entry), "");
}

static void
place_new_event_widget (GcalWindow   *window,
                        gint          x,
                        gint          y)
{
  GcalWindowPrivate *priv;

  gint out_x, out_y;
  GdkRectangle rect;

  priv = gcal_window_get_instance_private (window);
  gtk_widget_translate_coordinates (priv->views[priv->active_view], priv->views_stack, x, y, &out_x, &out_y);

  /* Place popover over the given (x,y) position */
  rect.x = out_x;
  rect.y = out_y;
  rect.width = 1;
  rect.height = 1;

  gtk_popover_set_pointing_to (GTK_POPOVER (priv->popover), &rect);
  gtk_widget_show_all (priv->popover);
}

static void
close_new_event_widget (GtkButton *button,
                        gpointer   user_data)
{
  set_new_event_mode (GCAL_WINDOW (user_data), FALSE);
}

static void
new_event_entry_text_changed (GObject    *object,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  GcalWindowPrivate *priv;
  static gboolean blocked = TRUE;
  gint length;

  g_return_if_fail (user_data);
  priv = gcal_window_get_instance_private (GCAL_WINDOW (object));

  length = g_utf8_strlen (gtk_entry_get_text (GTK_ENTRY (user_data)), -1);

  gtk_widget_set_sensitive (priv->new_event_create_button, length > 0);

  if (length > 0 && blocked)
    {
      g_signal_handlers_unblock_by_func (user_data, create_event, object);
      blocked = FALSE;
    }
  else if (length < 1 && !blocked)
    {
      g_signal_handlers_block_by_func (user_data, create_event, object);
      blocked = TRUE;
    }
}

/**
 * create_notification: Internal method for creating a notification
 * @window:
 * @message: The label it goes into the message part
 * @button_label: (allow-none): The label of the actionable button
 *
 **/
static void
create_notification (GcalWindow *window,
                     gchar      *message,
                     gchar      *button_label)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (window);

  /* notification content */
  gtk_label_set_markup (GTK_LABEL (priv->notification_label), message);
  gtk_widget_show_all (priv->notification);

  if (button_label != NULL)
    {
      gtk_button_set_label (GTK_BUTTON (priv->notification_action_button),
                            button_label);
      gtk_widget_show (priv->notification_action_button);
    }
  else
    {
      gtk_widget_hide (priv->notification_action_button);
    }
}

static void
hide_notification (GcalWindow *window,
                   GtkWidget  *button)
{
  GcalWindowPrivate *priv = gcal_window_get_instance_private (window);
  gtk_revealer_set_reveal_child (GTK_REVEALER (priv->notification), FALSE);
  priv->notification_timeout = 0;
}

static gboolean
hide_notification_scheduled (gpointer window)
{
  hide_notification (GCAL_WINDOW (window), NULL);
  return FALSE;
}

static void
add_source (GcalManager *manager,
            ESource     *source,
            gboolean     enabled,
            gpointer     user_data)
{
  GcalWindowPrivate *priv;
  GtkWidget *row;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  row = make_row_from_source (GCAL_WINDOW (user_data), source);
  g_object_set_data (G_OBJECT (row), "source", source);

  gtk_container_add (GTK_CONTAINER (priv->calendar_listbox), row);
}

/**
 * make_row_from_source:
 *
 * Create a GtkListBoxRow for a given
 * ESource.
 *
 * Returns: (transfer full) the new row
 */
static GtkWidget*
make_row_from_source (GcalWindow *window,
                      ESource    *source)
{
  GcalWindowPrivate *priv;
  GtkWidget *label, *icon, *separator, *checkbox, *box, *row;
  GtkStyleContext *context;
  GdkPixbuf *pixbuf;
  GdkRGBA color;

  priv = gcal_window_get_instance_private (window);
  row = gtk_list_box_row_new ();

  /* apply some nice styling */
  context = gtk_widget_get_style_context (row);
  gtk_style_context_add_class (context, "button");
  gtk_style_context_add_class (context, "flat");
  gtk_style_context_add_class (context, "menuitem");

  /* main box */
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (box), 6);

  /* source color icon */
  get_color_name_from_source (source, &color);
  pixbuf = gcal_get_pixbuf_from_color (&color, 16);
  icon = gtk_image_new_from_pixbuf (pixbuf);

  /* source name label */
  label = gtk_label_new (e_source_get_display_name (source));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_hexpand (label, TRUE);

  /* vertical separator */
  separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);

  /* checkbox */
  checkbox = gtk_check_button_new ();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), gcal_manager_source_enabled (priv->manager, source));
  g_signal_connect (checkbox, "notify::active", G_CALLBACK (on_calendar_toggled), window);

  gtk_container_add (GTK_CONTAINER (box), icon);
  gtk_container_add (GTK_CONTAINER (box), label);
  gtk_container_add (GTK_CONTAINER (box), separator);
  gtk_container_add (GTK_CONTAINER (box), checkbox);
  gtk_container_add (GTK_CONTAINER (row), box);

  gtk_widget_show_all (row);

  g_object_unref (pixbuf);

  return row;
}

static void
remove_source (GcalManager *manager,
               ESource     *source,
               gpointer     user_data)
{
  GcalWindowPrivate *priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));
  GList *children, *aux;

  children = gtk_container_get_children (GTK_CONTAINER (priv->calendar_listbox));

  for (aux = children; aux != NULL; aux = aux->next)
    {
      ESource *child_source = g_object_get_data (G_OBJECT (aux->data), "source");

      if (child_source != NULL && child_source == source)
        {
          gtk_widget_destroy (aux->data);
          break;
        }
    }

  g_list_free (children);
}

static void
show_source_dialog (GtkButton *button,
                    gpointer   user_data)
{
  GcalWindowPrivate *priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  gcal_source_dialog_set_mode (GCAL_SOURCE_DIALOG (priv->source_dialog), GCAL_SOURCE_DIALOG_MODE_NORMAL);

  gtk_widget_hide (priv->calendar_popover);

  gtk_dialog_run (GTK_DIALOG (priv->source_dialog));
}

static void
source_row_activated (GtkListBox    *listbox,
                      GtkListBoxRow *row,
                      gpointer       user_data)
{
  GcalWindowPrivate *priv;
  ESource *source;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));
  source = g_object_get_data (G_OBJECT (row), "source");

  if (source == NULL)
    return;

  gcal_source_dialog_set_source (GCAL_SOURCE_DIALOG (priv->source_dialog), source);
  gcal_source_dialog_set_mode (GCAL_SOURCE_DIALOG (priv->source_dialog), GCAL_SOURCE_DIALOG_MODE_EDIT);

  gtk_widget_hide (priv->calendar_popover);

  gtk_dialog_run (GTK_DIALOG (priv->source_dialog));
}

static void
source_enabled (GcalManager *manager,
                ESource     *source,
                gboolean     enabled,
                gpointer     user_data)
{
  GcalWindowPrivate *priv;
  GList *children, *aux;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));
  children = gtk_container_get_children (GTK_CONTAINER (priv->calendar_listbox));

  for (aux = children; aux != NULL; aux = aux->next)
    {
      ESource *child_source = g_object_get_data (G_OBJECT (aux->data), "source");

      /*
       * Here we destroy the row and re-add it because there is no
       * way to actually bind the ::enabled state of source. It behaves
       * like a construct-only property, and we need to re-create the
       * row if there's any changes.
       */
      if (child_source != NULL && child_source == source)
        {
          gtk_widget_destroy (aux->data);
          add_source (manager, source, enabled, user_data);
          break;
        }
    }

  g_list_free (children);
}

static void
on_calendar_toggled (GObject    *object,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
  GcalWindowPrivate *priv;
  gboolean active;
  GtkWidget *row;
  ESource *source;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));
  active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
  row = gtk_widget_get_parent (gtk_widget_get_parent (GTK_WIDGET (object)));
  source = g_object_get_data (G_OBJECT (row), "source");

  if (source == NULL)
    return;

  /* Enable/disable the toggled calendar */
  if (active)
    gcal_manager_enable_source (priv->manager, source);
  else
    gcal_manager_disable_source (priv->manager, source);
}

static gboolean
refresh_sources (GcalWindow *window)
{
  GcalWindowPrivate *priv;
  static gint current_timeout = FAST_REFRESH_TIMEOUT;

  priv = gcal_window_get_instance_private (window);

  /* refresh sources */
  gcal_manager_refresh (priv->manager);

  /* check window state */
  if (current_timeout != priv->refresh_timeout)
    {
      current_timeout = priv->refresh_timeout;

      priv->refresh_timeout_id = g_timeout_add (priv->refresh_timeout, (GSourceFunc) refresh_sources, window);

      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static gboolean
window_state_changed (GtkWidget *window,
                      GdkEvent  *event,
                      gpointer   user_data)
{
  GcalWindowPrivate *priv;
  GdkEventWindowState *state;
  gboolean active;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (window));
  state = (GdkEventWindowState*) event;
  active = (state->new_window_state & GDK_WINDOW_STATE_FOCUSED);

  /* update timeout time according to the state */
  priv->refresh_timeout = (active ? FAST_REFRESH_TIMEOUT : SLOW_REFRESH_TIMEOUT);

  return FALSE;
}

static void
create_event (gpointer   user_data,
              GtkWidget *widget)
{
  GcalWindowPrivate *priv;

  ESource *source;
  ECalComponent *comp;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  /* reset and hide */
  set_new_event_mode (GCAL_WINDOW (user_data), FALSE);

  source = gcal_manager_get_default_source (priv->manager);
  comp = build_component_from_details (gtk_entry_get_text (GTK_ENTRY (priv->new_event_what_entry)),
                                       priv->event_creation_data->start_date,
                                       priv->event_creation_data->end_date);
  if (widget == priv->new_event_details_button)
    {
      GcalEventData *edata;

      edata = g_new0 (GcalEventData, 1);
      edata->source = source;
      edata->event_component = comp;

      gcal_edit_dialog_set_event_is_new (GCAL_EDIT_DIALOG (priv->edit_dialog),
                                         TRUE);
      gcal_edit_dialog_set_event_data (GCAL_EDIT_DIALOG (priv->edit_dialog),
                                       edata);
      g_object_unref (comp);
      g_free (edata);

      gtk_dialog_run (GTK_DIALOG (priv->edit_dialog));
    }
  else
    {
      /* create the event */
      gcal_manager_create_event (priv->manager, source, comp);
    }

  g_object_unref (source);
}

static void
create_event_detailed_cb (GcalView *view,
                          gpointer  start_span,
                          gpointer  end_span,
                          gpointer  user_data)
{
  GcalWindowPrivate *priv;

  GcalEventData *edata;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  edata = g_new0 (GcalEventData, 1);
  edata->source = gcal_manager_get_default_source (priv->manager);
  edata->event_component = build_component_from_details ("", (icaltimetype*) start_span, (icaltimetype*) end_span);

  gcal_edit_dialog_set_event_is_new (GCAL_EDIT_DIALOG (priv->edit_dialog), TRUE);
  gcal_edit_dialog_set_event_data (GCAL_EDIT_DIALOG (priv->edit_dialog), edata);

  g_object_unref (edata->event_component);
  g_free (edata);

  gtk_dialog_run (GTK_DIALOG (priv->edit_dialog));
}

static void
event_activated (GcalView        *view,
                 GcalEventWidget *event_widget,
                 gpointer         user_data)
{
  GcalWindowPrivate *priv;
  GcalEventData *data;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  data = gcal_event_widget_get_data (event_widget);
  gcal_edit_dialog_set_event_is_new (
      GCAL_EDIT_DIALOG (priv->edit_dialog),
      FALSE);
  gcal_edit_dialog_set_event_data (
      GCAL_EDIT_DIALOG (priv->edit_dialog),
      data);
  g_free (data);

  gtk_dialog_run (GTK_DIALOG (priv->edit_dialog));
}

static void
edit_dialog_closed (GtkDialog *dialog,
                    gint       response,
                    gpointer   user_data)
{
  GcalWindowPrivate *priv;

  GcalEditDialog *edit_dialog;
  ECalComponent *component;
  GcalView *view;

  GList *widgets;
  gchar *uuid;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  gtk_widget_hide (GTK_WIDGET (dialog));

  view = GCAL_VIEW (priv->views[priv->active_view]);
  edit_dialog = GCAL_EDIT_DIALOG (dialog);

  switch (response)
    {
    case GCAL_RESPONSE_CREATE_EVENT:
      /* retrieve the component from the dialog*/
      gcal_manager_create_event (priv->manager,
                                 gcal_edit_dialog_get_source (edit_dialog),
                                 gcal_edit_dialog_get_component (edit_dialog));

      break;

    case GCAL_RESPONSE_SAVE_EVENT:
      /* retrieve the component from the dialog*/
      component = gcal_edit_dialog_get_component (edit_dialog);

      gcal_manager_update_event (priv->manager,
                                 gcal_edit_dialog_get_source (edit_dialog),
                                 component);

      break;

    case GCAL_RESPONSE_DELETE_EVENT:
      if (priv->event_to_delete != NULL)
        {
          gcal_manager_remove_event (priv->manager, priv->event_to_delete->source, priv->event_to_delete->event_component);
          g_clear_pointer (&(priv->event_to_delete), g_free);

          create_notification (GCAL_WINDOW (user_data), _("Another event deleted"), _("Undo"));
        }
      else
        {
          create_notification (GCAL_WINDOW (user_data), _("Event deleted"), _("Undo"));
        }

      gtk_revealer_set_reveal_child (GTK_REVEALER (priv->notification), TRUE);
      if (priv->notification_timeout != 0)
        g_source_remove (priv->notification_timeout);
      priv->notification_timeout = g_timeout_add_seconds (5, hide_notification_scheduled, user_data);

      priv->event_to_delete = g_new0 (GcalEventData, 1);
      priv->event_to_delete->source = gcal_edit_dialog_get_source (edit_dialog);
      priv->event_to_delete->event_component = gcal_edit_dialog_get_component (edit_dialog);

      uuid = gcal_edit_dialog_get_event_uuid (edit_dialog);
      /* hide widget of the event */
      widgets = gcal_view_get_children_by_uuid (view, uuid);
      g_list_foreach (widgets, (GFunc) gtk_widget_hide, NULL);
      g_list_free (widgets);
      g_free (uuid);
      break;

    case GTK_RESPONSE_CANCEL:
      break;

    }
}

static void
search_toggled (GObject    *object,
                GParamSpec *pspec,
                gpointer    user_data)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  if (gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (priv->search_bar)))
    {
      g_debug ("Entering search mode");
      gtk_widget_show (priv->search_bar);

      /* update header_bar widget */
      gcal_search_view_search (GCAL_SEARCH_VIEW (priv->search_view), NULL, NULL);
    }
  else
    {
      g_debug ("Leaving search mode");
      /* update header_bar */
      priv->leaving_search_mode = TRUE;
      gtk_widget_hide (priv->search_view);
    }
}

static void
search_changed (GtkEditable *editable,
                gpointer     user_data)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  if (gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (priv->search_bar)))
    {
      /* perform the search */
      gcal_search_view_search (GCAL_SEARCH_VIEW (priv->search_view),
                               "summary", gtk_entry_get_text (GTK_ENTRY (priv->search_entry)));
    }
}

static void
search_bar_revealer_toggled (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  if (!gtk_revealer_get_child_revealed (GTK_REVEALER (object)))
    gtk_widget_hide (priv->search_bar);
  else
    gtk_widget_show (priv->search_view);
}

static void
remove_event (GtkWidget  *notification,
              GParamSpec *spec,
              gpointer    user_data)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  if (gtk_revealer_get_child_revealed (GTK_REVEALER (notification)))
      return;

  if (priv->event_to_delete != NULL)
    {
      gcal_manager_remove_event (priv->manager,
                                 priv->event_to_delete->source,
                                 priv->event_to_delete->event_component);

      g_clear_pointer (&(priv->event_to_delete), g_free);
    }
}

static void
undo_remove_action (GtkButton *button,
                    gpointer   user_data)
{
  GcalWindowPrivate *priv;
  gchar *uuid;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (user_data));

  if (priv->event_to_delete != NULL)
    {
      GList *widgets;
      uuid = get_uuid_from_component (priv->event_to_delete->source, priv->event_to_delete->event_component);
      widgets = gcal_view_get_children_by_uuid (GCAL_VIEW (priv->views[priv->active_view]), uuid);
      g_list_foreach (widgets, (GFunc) gtk_widget_show, NULL);

      g_object_unref (priv->event_to_delete->event_component);
      g_clear_pointer (&(priv->event_to_delete), g_free);
      g_list_free (widgets);
      g_free (uuid);
    }
}

static gboolean
schedule_open_edit_dialog_by_uuid (OpenEditDialogData *edit_dialog_data)
{
  GcalWindowPrivate *priv;
  GList *widgets;

  priv = gcal_window_get_instance_private (edit_dialog_data->window);
  widgets = gcal_view_get_children_by_uuid (GCAL_VIEW (priv->month_view), edit_dialog_data->uuid);
  if (widgets != NULL)
    {
      event_activated (NULL, widgets->data, edit_dialog_data->window);
      g_list_free (widgets);
      g_free (edit_dialog_data->uuid);
      g_free (edit_dialog_data);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static void
gcal_window_class_init(GcalWindowClass *klass)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructed = gcal_window_constructed;
  object_class->finalize = gcal_window_finalize;
  object_class->set_property = gcal_window_set_property;
  object_class->get_property = gcal_window_get_property;

  widget_class = GTK_WIDGET_CLASS (klass);
  widget_class->configure_event = gcal_window_configure_event;
  widget_class->window_state_event = gcal_window_state_event;
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/calendar/window.ui");

  g_object_class_install_property (
      object_class,
      PROP_ACTIVE_VIEW,
      g_param_spec_enum ("active-view",
                         "Active View",
                         "The active view, eg: month, week, etc.",
                         GCAL_WINDOW_VIEW_TYPE,
                         GCAL_WINDOW_VIEW_MONTH,
                         G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      PROP_MANAGER,
      g_param_spec_pointer ("manager",
                            "The manager object",
                            "A weak reference to the app manager object",
                            G_PARAM_CONSTRUCT_ONLY |
                            G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      PROP_ACTIVE_DATE,
      g_param_spec_boxed ("active-date",
                          "Date",
                          "The active/selected date",
                          ICAL_TIME_TYPE,
                          G_PARAM_CONSTRUCT |
                          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      PROP_NEW_EVENT_MODE,
      g_param_spec_boolean ("new-event-mode",
                            "New Event mode",
                            "Whether the window is in new-event-mode or not",
                            FALSE,
                            G_PARAM_READWRITE));

  /* widgets */
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, header_bar);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, main_box);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, menu_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, search_bar);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, search_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, calendars_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, calendar_listbox);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, calendar_popover);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, source_dialog);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, search_entry);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, back_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, today_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, forward_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, views_overlay);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, views_stack);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, month_view);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, year_view);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, views_switcher);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, popover);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, search_view);

  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, new_event_title_label);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, new_event_create_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, new_event_details_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, new_event_what_entry);

  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, notification);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, notification_label);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, notification_action_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalWindow, notification_close_button);

  gtk_widget_class_bind_template_callback (widget_class, show_source_dialog);
  gtk_widget_class_bind_template_callback (widget_class, source_row_activated);

  gtk_widget_class_bind_template_callback (widget_class, key_pressed);
  gtk_widget_class_bind_template_callback (widget_class, search_toggled);
  gtk_widget_class_bind_template_callback (widget_class, search_changed);
  gtk_widget_class_bind_template_callback (widget_class, view_changed);
  gtk_widget_class_bind_template_callback (widget_class, date_updated);

  /* Event removal related */
  gtk_widget_class_bind_template_callback (widget_class, hide_notification);
  gtk_widget_class_bind_template_callback (widget_class, remove_event);
  gtk_widget_class_bind_template_callback (widget_class, undo_remove_action);

  /* Event creation related */
  gtk_widget_class_bind_template_callback (widget_class, create_event);
  gtk_widget_class_bind_template_callback (widget_class, create_event_detailed_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_new_event_widget);
  gtk_widget_class_bind_template_callback (widget_class, close_new_event_widget);
  gtk_widget_class_bind_template_callback (widget_class, new_event_entry_text_changed);
  gtk_widget_class_bind_template_callback (widget_class, event_activated);

  /* Syncronization related */
  gtk_widget_class_bind_template_callback (widget_class, window_state_changed);

  /* search related */
  gtk_widget_class_bind_template_callback (widget_class, search_event_selected);
}

static void
gcal_window_init (GcalWindow *self)
{
  GcalWindowPrivate *priv = gcal_window_get_instance_private (self);

  /* source dialog */
  priv->source_dialog = gcal_source_dialog_new ();
  gtk_window_set_transient_for (GTK_WINDOW (priv->source_dialog), GTK_WINDOW (self));

  g_object_bind_property (self, "application", priv->source_dialog, "application",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  priv->active_date = g_new0 (icaltimetype, 1);

  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gcal_window_constructed (GObject *object)
{
  GcalWindowPrivate *priv;


  GtkBuilder *builder;
  GMenuModel *winmenu;

  GSettings *helper_settings;
  gchar *clock_format;
  gboolean use_24h_format;

  if (G_OBJECT_CLASS (gcal_window_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gcal_window_parent_class)->constructed (object);

  priv = gcal_window_get_instance_private (GCAL_WINDOW (object));

  helper_settings = g_settings_new ("org.gnome.desktop.interface");
  clock_format = g_settings_get_string (helper_settings, "clock-format");
  use_24h_format = (g_strcmp0 (clock_format, "24h") == 0);
  g_free (clock_format);
  g_object_unref (helper_settings);

  // Prevents nameless events' creation
  g_signal_handlers_block_by_func (priv->new_event_what_entry, create_event, object);

  /* header_bar: menu */
  builder = gtk_builder_new ();
  gtk_builder_add_from_resource (builder,
                                 "/org/gnome/calendar/menus.ui",
                                 NULL);

  winmenu = (GMenuModel *)gtk_builder_get_object (builder, "winmenu");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (priv->menu_button),
                                  winmenu);

  g_object_unref (builder);

  /* edit dialog initialization */
  priv->edit_dialog = gcal_edit_dialog_new (use_24h_format);
  gtk_window_set_transient_for (GTK_WINDOW (priv->edit_dialog), GTK_WINDOW (object));
  gcal_edit_dialog_set_manager (GCAL_EDIT_DIALOG (priv->edit_dialog), priv->manager);

  g_signal_connect (priv->edit_dialog, "response", G_CALLBACK (edit_dialog_closed), object);

  /* search bar */
  gtk_search_bar_connect_entry (GTK_SEARCH_BAR (priv->search_bar),
                                GTK_ENTRY (priv->search_entry));

  g_signal_connect (gtk_bin_get_child (GTK_BIN (priv->search_bar)), "notify::child-revealed",
                    G_CALLBACK (search_bar_revealer_toggled), object);

  /* XXX: Week view disabled until after the release when we restart the work on it*/
  //priv->views[GCAL_WINDOW_VIEW_WEEK] = gcal_week_view_new ();
  //gcal_week_view_set_manager (GCAL_WEEK_VIEW (priv->views[GCAL_WINDOW_VIEW_WEEK]), priv->manager);
  //gcal_week_view_set_first_weekday (GCAL_WEEK_VIEW (priv->views[GCAL_WINDOW_VIEW_WEEK]), get_first_weekday ());
  //gcal_week_view_set_use_24h_format (GCAL_WEEK_VIEW (priv->views[GCAL_WINDOW_VIEW_WEEK]), use_24h_format);
  //gtk_stack_add_titled (GTK_STACK (priv->views_stack), priv->views[GCAL_WINDOW_VIEW_WEEK], "week", _("Week"));

  priv->views[GCAL_WINDOW_VIEW_MONTH] = priv->month_view;
  priv->views[GCAL_WINDOW_VIEW_YEAR] = priv->year_view;

  gcal_month_view_set_first_weekday (GCAL_MONTH_VIEW (priv->views[GCAL_WINDOW_VIEW_MONTH]), get_first_weekday ());
  gcal_month_view_set_use_24h_format (GCAL_MONTH_VIEW (priv->views[GCAL_WINDOW_VIEW_MONTH]), use_24h_format);

  gcal_year_view_set_first_weekday (GCAL_YEAR_VIEW (priv->views[GCAL_WINDOW_VIEW_YEAR]), get_first_weekday ());
  gcal_year_view_set_use_24h_format (GCAL_YEAR_VIEW (priv->views[GCAL_WINDOW_VIEW_YEAR]), use_24h_format);

  /* search view */
  gcal_search_view_connect (GCAL_SEARCH_VIEW (priv->search_view), priv->manager);
  gcal_search_view_set_time_format (GCAL_SEARCH_VIEW (priv->search_view), use_24h_format);

  /* refresh timeout, first is fast */
  priv->refresh_timeout_id = g_timeout_add (FAST_REFRESH_TIMEOUT, (GSourceFunc) refresh_sources, object);

  /* calendars popover */
  gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->calendar_listbox), (GtkListBoxSortFunc) calendar_listbox_sort_func,
                              object, NULL);

  if (gcal_manager_load_completed (priv->manager))
    {
      GList *sources, *l;
      sources = gcal_manager_get_sources_connected (priv->manager);
      for (l = sources; l != NULL; l = g_list_next (l))
        add_source (priv->manager, l->data, gcal_manager_source_enabled (priv->manager, l->data), object);

      g_list_free (sources);
    }
}

static void
gcal_window_finalize (GObject *object)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (object));

  g_free (priv->active_date);
  g_free (priv->current_date);

  if (priv->views_switcher != NULL)
    g_object_unref (priv->views_switcher);

  G_OBJECT_CLASS (gcal_window_parent_class)->finalize (object);
}

static void
gcal_window_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (object));

  switch (property_id)
    {
    case PROP_ACTIVE_VIEW:
      priv->active_view = g_value_get_enum (value);
      gtk_widget_show (priv->views[priv->active_view]);
      gtk_stack_set_visible_child (GTK_STACK (priv->views_stack),
                                   priv->views[priv->active_view]);
      return;
    case PROP_ACTIVE_DATE:
      update_active_date (GCAL_WINDOW (object), g_value_dup_boxed (value));
      return;
    case PROP_NEW_EVENT_MODE:
      set_new_event_mode (GCAL_WINDOW (object), g_value_get_boolean (value));
      return;
    case PROP_MANAGER:
      priv->manager = g_value_get_pointer (value);
      g_signal_connect (priv->manager, "source-added", G_CALLBACK (add_source), object);
      g_signal_connect (priv->manager, "source-enabled", G_CALLBACK (source_enabled), object);
      g_signal_connect (priv->manager, "source-removed", G_CALLBACK (remove_source), object);

      gcal_year_view_set_manager (GCAL_YEAR_VIEW (priv->year_view), priv->manager);
      gcal_source_dialog_set_manager (GCAL_SOURCE_DIALOG (priv->source_dialog), priv->manager);
      update_current_date (GCAL_WINDOW (object));
      return;
    }

  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gcal_window_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (GCAL_WINDOW (object));

  switch (property_id)
    {
    case PROP_ACTIVE_VIEW:
      g_value_set_enum (value, priv->active_view);
      return;
    case PROP_ACTIVE_DATE:
      g_value_set_boxed (value, priv->active_date);
      return;
    case PROP_NEW_EVENT_MODE:
      g_value_set_boolean (value, priv->new_event_mode);
      return;
    }

  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static gboolean
gcal_window_configure_event (GtkWidget         *widget,
                             GdkEventConfigure *event)
{
  GcalWindow *window;
  GcalWindowPrivate *priv;
  gboolean retval;

  window = GCAL_WINDOW (widget);
  priv = gcal_window_get_instance_private (window);

  if (priv->save_geometry_timeout_id != 0)
    {
      g_source_remove (priv->save_geometry_timeout_id);
      priv->save_geometry_timeout_id = 0;
    }

  priv->save_geometry_timeout_id = g_timeout_add (SAVE_GEOMETRY_ID_TIMEOUT,
                                                  save_geometry,
                                                  window);

  retval = GTK_WIDGET_CLASS (gcal_window_parent_class)->configure_event (widget, event);

  return retval;
}

static gboolean
gcal_window_state_event (GtkWidget           *widget,
                         GdkEventWindowState *event)
{
  GcalWindow *window;
  GcalWindowPrivate *priv;
  gboolean retval;

  window = GCAL_WINDOW (widget);
  priv = gcal_window_get_instance_private (window);

  if (priv->save_geometry_timeout_id != 0)
    {
      g_source_remove (priv->save_geometry_timeout_id);
      priv->save_geometry_timeout_id = 0;
    }

  priv->save_geometry_timeout_id = g_timeout_add (SAVE_GEOMETRY_ID_TIMEOUT,
                                                  save_geometry,
                                                  window);

  retval = GTK_WIDGET_CLASS (gcal_window_parent_class)->window_state_event (widget, event);

  return retval;
}

/* Public API */
GtkWidget*
gcal_window_new_with_view_and_date (GcalApplication   *app,
                                    GcalWindowViewType view_type,
                                    icaltimetype      *date)
{
  GcalWindow *win;
  GcalManager *manager;

  manager = gcal_application_get_manager (GCAL_APPLICATION (app));

  win  =  g_object_new (GCAL_TYPE_WINDOW, "application", GTK_APPLICATION (app), "manager", manager, "active-date", date,
                        NULL);

  /* loading size */
  load_geometry (win);

  if (view_type == GCAL_WINDOW_VIEW_DAY)
    view_changed (NULL, NULL, win);

  return GTK_WIDGET (win);
}

/* new-event interaction: first variant */
void
gcal_window_new_event (GcalWindow *window)
{
  GcalWindowPrivate *priv;
  icaltimetype *start_date, *end_date;

  priv = gcal_window_get_instance_private (window);

  /* 1st and 2nd steps */
  set_new_event_mode (window, TRUE);

  start_date = gcal_dup_icaltime (priv->active_date);
  end_date = gcal_dup_icaltime (priv->active_date);

  /* adjusting dates according to the actual view */
  switch (priv->active_view)
    {
    case GCAL_WINDOW_VIEW_DAY:
    case GCAL_WINDOW_VIEW_WEEK:
      end_date->hour += 1;
      *end_date = icaltime_normalize (*end_date);
      break;
    case GCAL_WINDOW_VIEW_YEAR:
      start_date->day = 1;
      end_date->day = icaltime_days_in_month (end_date->month, end_date->year);
      break;
    case GCAL_WINDOW_VIEW_MONTH:
      start_date->is_date = 1;
      end_date->is_date = 1;
      end_date->day += 1;
      *end_date = icaltime_normalize (*end_date);
      break;
    case GCAL_WINDOW_VIEW_LIST:
    case GCAL_WINDOW_VIEW_SEARCH:
      break;
    }

  create_event_detailed_cb (NULL, start_date, end_date, window);
}

void
gcal_window_set_search_mode (GcalWindow *window,
                             gboolean    enabled)
{
  GcalWindowPrivate *priv;

  priv = gcal_window_get_instance_private (window);
  priv->search_mode = enabled;
  gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (priv->search_bar),
                                  enabled);
}

void
gcal_window_set_search_query (GcalWindow  *window,
                              const gchar *query)
{
  GcalWindowPrivate *priv = gcal_window_get_instance_private (window);
  gtk_entry_set_text (GTK_ENTRY (priv->search_entry), query);
}

void
gcal_window_open_event_by_uuid (GcalWindow  *window,
                                const gchar *uuid)
{
  GcalWindowPrivate *priv;
  GList *widgets;

  priv = gcal_window_get_instance_private (window);

  /* XXX: show events on month view */
  gtk_stack_set_visible_child (GTK_STACK (priv->views_stack), priv->month_view);
  widgets = gcal_view_get_children_by_uuid (GCAL_VIEW (priv->month_view), uuid);
  if (widgets != NULL)
    {
      event_activated (NULL, widgets->data, window);
      g_list_free (widgets);
    }
  else
    {
      OpenEditDialogData *edit_dialog_data = g_new0 (OpenEditDialogData, 1);
      edit_dialog_data->window = window;
      edit_dialog_data->uuid = g_strdup (uuid);
      g_timeout_add_seconds (2, (GSourceFunc) schedule_open_edit_dialog_by_uuid, edit_dialog_data);
    }
}
