/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-event-widget.c
 * Copyright (C) 2015 Erick Pérez Castellanos <erickpc@gnome.org>
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

#include "gcal-event-widget.h"
#include "gcal-utils.h"

#define INTENSITY(r, g, b) ((r) * 0.30 + (g) * 0.59 + (b) * 0.11)

struct _GcalEventWidget
{
  GtkWidget      parent;

  /* properties */
  gchar         *uuid;
  gchar         *summary;
  GdkRGBA       *color;
  GDateTime     *dt_start;
  GDateTime     *dt_end; /* could be NULL, meaning dt_end is the same as start_date */
  gboolean       all_day;
  gboolean       has_reminders;

  /* internal data */
  gboolean       read_only;

  /* weak ESource reference */
  ESource       *source;
  /* ECalComponent data */
  ECalComponent *component;

  GdkWindow     *event_window;
  gboolean       button_pressed;
};

enum
{
  PROP_0,
  PROP_UUID,
  PROP_SUMMARY,
  PROP_COLOR,
  PROP_DTSTART,
  PROP_DTEND,
  PROP_ALL_DAY,
  PROP_HAS_REMINDERS
};

enum
{
  ACTIVATE,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void     gcal_event_widget_set_property         (GObject        *object,
                                                        guint           property_id,
                                                        const GValue   *value,
                                                        GParamSpec     *pspec);

static void     gcal_event_widget_get_property         (GObject        *object,
                                                        guint           property_id,
                                                        GValue         *value,
                                                        GParamSpec     *pspec);

static void     gcal_event_widget_finalize             (GObject        *object);

static void     gcal_event_widget_get_preferred_width  (GtkWidget      *widget,
                                                        gint           *minimum,
                                                        gint           *natural);

static void     gcal_event_widget_get_preferred_height (GtkWidget      *widget,
                                                        gint           *minimum,
                                                        gint           *natural);

static void     gcal_event_widget_realize              (GtkWidget      *widget);

static void     gcal_event_widget_unrealize            (GtkWidget      *widget);

static void     gcal_event_widget_map                  (GtkWidget      *widget);

static void     gcal_event_widget_unmap                (GtkWidget      *widget);

static void     gcal_event_widget_size_allocate        (GtkWidget      *widget,
                                                        GtkAllocation  *allocation);

static gboolean gcal_event_widget_draw                 (GtkWidget      *widget,
                                                        cairo_t        *cr);

static gboolean gcal_event_widget_button_press_event   (GtkWidget      *widget,
                                                        GdkEventButton *event);

static gboolean gcal_event_widget_button_release_event (GtkWidget      *widget,
                                                        GdkEventButton *event);

G_DEFINE_TYPE (GcalEventWidget, gcal_event_widget, GTK_TYPE_WIDGET)

static void
gcal_event_widget_class_init(GcalEventWidgetClass *klass)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->set_property = gcal_event_widget_set_property;
  object_class->get_property = gcal_event_widget_get_property;
  object_class->finalize = gcal_event_widget_finalize;

  widget_class = GTK_WIDGET_CLASS (klass);
  widget_class->get_preferred_width = gcal_event_widget_get_preferred_width;
  widget_class->get_preferred_height = gcal_event_widget_get_preferred_height;
  widget_class->realize = gcal_event_widget_realize;
  widget_class->unrealize = gcal_event_widget_unrealize;
  widget_class->map = gcal_event_widget_map;
  widget_class->unmap = gcal_event_widget_unmap;
  widget_class->size_allocate = gcal_event_widget_size_allocate;
  widget_class->draw = gcal_event_widget_draw;
  widget_class->button_press_event = gcal_event_widget_button_press_event;
  widget_class->button_release_event = gcal_event_widget_button_release_event;

  g_object_class_install_property (object_class,
                                   PROP_UUID,
                                   g_param_spec_string ("uuid",
                                                        "Unique uid",
                                                        "The unique-unique id composed of source_uid:event_uid",
                                                        NULL,
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_SUMMARY,
                                   g_param_spec_string ("summary",
                                                        "Summary",
                                                        "The event summary",
                                                        NULL,
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_COLOR,
                                   g_param_spec_boxed ("color",
                                                       "Color",
                                                       "The color to render",
                                                       GDK_TYPE_RGBA,
                                                       G_PARAM_CONSTRUCT |
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_DTSTART,
                                   g_param_spec_boxed ("date-start",
                                                       "Date Start",
                                                       "The starting date of the event",
                                                       G_TYPE_DATE_TIME,
                                                       G_PARAM_CONSTRUCT |
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_DTEND,
                                   g_param_spec_boxed ("date-end",
                                                       "Date End",
                                                       "The end date of the event",
                                                       G_TYPE_DATE_TIME,
                                                       G_PARAM_CONSTRUCT |
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_ALL_DAY,
                                   g_param_spec_boolean ("all-day",
                                                         "All day",
                                                         "Wheter the event is all-day or not",
                                                         FALSE,
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_HAS_REMINDERS,
                                   g_param_spec_boolean ("has-reminders",
                                                         "Event has reminders",
                                                         "Wheter the event has reminders set or not",
                                                         FALSE,
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_READWRITE));

  signals[ACTIVATE] = g_signal_new ("activate",
                                     GCAL_TYPE_EVENT_WIDGET,
                                     G_SIGNAL_RUN_LAST,
                                     0,
                                     NULL, NULL,
                                     g_cclosure_marshal_VOID__VOID,
                                     G_TYPE_NONE,
                                     0);
}

static void
gcal_event_widget_init(GcalEventWidget *self)
{
  gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (self), TRUE);
}

static void
gcal_event_widget_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GcalEventWidget *self = GCAL_EVENT_WIDGET (object);

  switch (property_id)
    {
    case PROP_UUID:
      g_clear_pointer (&self->uuid, g_free);
      self->uuid = g_value_dup_string (value);
      g_object_notify (object, "uuid");
      break;

    case PROP_SUMMARY:
      g_clear_pointer (&self->summary, g_free);
      self->summary = g_value_dup_string (value);
      g_object_notify (object, "summary");
      break;

    case PROP_COLOR:
      {
        GtkStyleContext *context;

        context = gtk_widget_get_style_context (GTK_WIDGET (object));

        g_clear_pointer (&self->color, gdk_rgba_free);

        self->color = g_value_dup_boxed (value);

        if (self->color == NULL)
          break;

        if (INTENSITY (self->color->red,
                       self->color->green,
                       self->color->blue) > 0.5)
          {
            gtk_style_context_remove_class (context, "color-dark");
            gtk_style_context_add_class (context, "color-light");
          }
        else
          {
            gtk_style_context_remove_class (context, "color-light");
            gtk_style_context_add_class (context, "color-dark");
          }

        g_object_notify (object, "color");
        break;
      }

    case PROP_DTSTART:
      g_clear_pointer (&self->dt_start, g_free);
      self->dt_start = g_value_dup_boxed (value);
      g_object_notify (object, "date-start");
      break;

    case PROP_DTEND:
      g_clear_pointer (&self->dt_end, g_free);
      self->dt_end = g_value_dup_boxed (value);
      g_object_notify (object, "date-end");
      break;

    case PROP_ALL_DAY:
      if (self->all_day != g_value_get_boolean (value))
        {
          self->all_day = g_value_get_boolean (value);
          g_object_notify (object, "all-day");
        }
      break;

    case PROP_HAS_REMINDERS:
      if (self->has_reminders != g_value_get_boolean (value))
        {
          self->has_reminders = g_value_get_boolean (value);
          g_object_notify (object, "has-reminders");
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gcal_event_widget_get_property (GObject      *object,
                                guint         property_id,
                                GValue       *value,
                                GParamSpec   *pspec)
{
  GcalEventWidget *self = GCAL_EVENT_WIDGET (object);

  switch (property_id)
    {
    case PROP_UUID:
      g_value_set_string (value, self->uuid);
      return;
    case PROP_SUMMARY:
      g_value_set_string (value, self->summary);
      return;
    case PROP_COLOR:
      g_value_set_boxed (value, self->color);
      return;
    case PROP_DTSTART:
      g_value_set_boxed (value, self->dt_start);
      return;
    case PROP_DTEND:
      g_value_set_boxed (value, self->dt_end);
      return;
    case PROP_ALL_DAY:
      g_value_set_boolean (value, self->all_day);
      return;
    case PROP_HAS_REMINDERS:
      g_value_set_boolean (value, self->has_reminders);
      return;
    }

  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gcal_event_widget_finalize (GObject *object)
{
  GcalEventWidget *self = GCAL_EVENT_WIDGET (object);

  g_clear_object (&self->component);

  /* releasing properties */
  g_clear_pointer (&self->uuid, g_free);
  g_clear_pointer (&self->summary, g_free);
  g_clear_pointer (&self->dt_start, g_date_time_unref);
  g_clear_pointer (&self->dt_end, g_date_time_unref);
  g_clear_pointer (&self->color, gdk_rgba_free);

  G_OBJECT_CLASS (gcal_event_widget_parent_class)->finalize (object);
}

static void
gcal_event_widget_get_preferred_width (GtkWidget *widget,
                                       gint      *minimum,
                                       gint      *natural)
{
  GtkBorder border, padding;
  PangoLayout *layout;
  gint layout_width;

  layout = gtk_widget_create_pango_layout (widget, "00:00:00 00:00");
  pango_layout_get_pixel_size (layout, &layout_width, NULL);
  g_object_unref (layout);

  gtk_style_context_get_border (gtk_widget_get_style_context (widget), gtk_widget_get_state_flags (widget), &border);
  gtk_style_context_get_padding (gtk_widget_get_style_context (widget), gtk_widget_get_state_flags (widget), &padding);

  if (minimum != NULL)
    *minimum = layout_width + padding.left + padding.right + border.left + border.right;
  if (natural != NULL)
    *natural = layout_width + padding.left + padding.right + border.left + border.right;
}

static void
gcal_event_widget_get_preferred_height (GtkWidget *widget,
                                        gint      *minimum,
                                        gint      *natural)
{
  GtkBorder border, padding;
  PangoLayout *layout;
  gint layout_height;

  layout = gtk_widget_create_pango_layout (widget, NULL);
  pango_layout_get_pixel_size (layout, NULL, &layout_height);
  g_object_unref (layout);

  gtk_style_context_get_border (gtk_widget_get_style_context (widget), gtk_widget_get_state_flags (widget), &border);
  gtk_style_context_get_padding (gtk_widget_get_style_context (widget), gtk_widget_get_state_flags (widget), &padding);

  if (minimum != NULL)
    *minimum = layout_height + padding.top + padding.bottom + border.top + border.bottom;
  if (natural != NULL)
    *natural = layout_height + padding.top + padding.bottom + border.top + border.bottom;
}

static void
gcal_event_widget_realize (GtkWidget *widget)
{
  GcalEventWidget *self;
  GdkWindow *parent_window;
  GdkWindowAttr attributes;
  gint attributes_mask;
  GtkAllocation allocation;

  GdkCursor* pointer_cursor;

  self = GCAL_EVENT_WIDGET (widget);
  gtk_widget_set_realized (widget, TRUE);

  parent_window = gtk_widget_get_parent_window (widget);
  gtk_widget_set_window (widget, parent_window);
  g_object_ref (parent_window);

  gtk_widget_get_allocation (widget, &allocation);

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.wclass = GDK_INPUT_ONLY;
  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.event_mask = gtk_widget_get_events (widget);
  attributes.event_mask |= (GDK_BUTTON_PRESS_MASK |
                            GDK_BUTTON_RELEASE_MASK |
                            GDK_BUTTON1_MOTION_MASK |
                            GDK_POINTER_MOTION_HINT_MASK |
                            GDK_POINTER_MOTION_MASK |
                            GDK_ENTER_NOTIFY_MASK |
                            GDK_LEAVE_NOTIFY_MASK);
  attributes_mask = GDK_WA_X | GDK_WA_Y;

  self->event_window = gdk_window_new (parent_window,
                                       &attributes,
                                       attributes_mask);
  gtk_widget_register_window (widget, self->event_window);
  gdk_window_show (self->event_window);

  pointer_cursor = gdk_cursor_new_for_display (gdk_display_get_default (),
                                               GDK_HAND1);
  gdk_window_set_cursor (self->event_window, pointer_cursor);
}

static void
gcal_event_widget_unrealize (GtkWidget *widget)
{
  GcalEventWidget *self;

  self = GCAL_EVENT_WIDGET (widget);

  if (self->event_window != NULL)
    {
      gtk_widget_unregister_window (widget, self->event_window);
      gdk_window_destroy (self->event_window);
      self->event_window = NULL;
    }

  GTK_WIDGET_CLASS (gcal_event_widget_parent_class)->unrealize (widget);
}

static void
gcal_event_widget_map (GtkWidget *widget)
{
  GcalEventWidget *self;

  self = GCAL_EVENT_WIDGET (widget);

  GTK_WIDGET_CLASS (gcal_event_widget_parent_class)->map (widget);

  if (self->event_window != NULL)
    gdk_window_show (self->event_window);
}

static void
gcal_event_widget_unmap (GtkWidget *widget)
{
  GcalEventWidget *self;

  self = GCAL_EVENT_WIDGET (widget);

  GTK_WIDGET_CLASS (gcal_event_widget_parent_class)->unmap (widget);

  if (self->event_window != NULL)
    gdk_window_hide (self->event_window);
}

static void
gcal_event_widget_size_allocate (GtkWidget     *widget,
                                 GtkAllocation *allocation)
{
  GcalEventWidget *self;

  self = GCAL_EVENT_WIDGET (widget);
  gtk_widget_set_allocation (widget, allocation);

  if (gtk_widget_get_realized (widget))
    {
      gdk_window_move_resize (self->event_window,
                              allocation->x,
                              allocation->y,
                              allocation->width,
                              allocation->height);
    }
}

static gboolean
gcal_event_widget_draw (GtkWidget *widget,
                        cairo_t   *cr)
{
  GcalEventWidget *self;

  GtkStyleContext *context;
  GtkStateFlags state;
  GtkBorder padding;

  gint width, height, layout_height;
  gint left_gap, right_gap, icon_size = 0;

  PangoLayout *layout;
  PangoFontDescription *font_desc;

  self = GCAL_EVENT_WIDGET (widget);
  context = gtk_widget_get_style_context (widget);
  state = gtk_widget_get_state_flags (widget);

  gtk_style_context_get_padding (context, state, &padding);

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);

  gtk_render_background (context, cr, 0, 0, width, height);
  gtk_render_frame (context, cr, 0, 0, width, height);

  /* FIXME for RTL alignment and icons positions */
  gtk_style_context_get (context, state, "font", &font_desc, NULL);
  layout = gtk_widget_create_pango_layout (widget, self->summary);
  pango_layout_set_font_description (layout, font_desc);
  pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_width (layout, (width - (padding.left + padding.right) ) * PANGO_SCALE);
  pango_cairo_update_layout (cr, layout);

  left_gap = 0;
  if (self->has_reminders)
    {
      pango_layout_get_pixel_size (layout, NULL, &layout_height);
      icon_size = layout_height;
      left_gap = icon_size + padding.left;
      pango_layout_set_width (layout, (width - (left_gap + padding.left + padding.right) ) * PANGO_SCALE);
    }

  right_gap = 0;
  if (self->read_only)
    {
      if (icon_size == 0)
        {
          pango_layout_get_pixel_size (layout, NULL, &layout_height);
          icon_size = layout_height;
        }

      right_gap = icon_size + padding.right;
      pango_layout_set_width (layout, (width - (left_gap + padding.left + padding.right + right_gap) ) * PANGO_SCALE);
    }

  gtk_render_layout (context, cr, padding.left + left_gap, padding.top, layout);

  /* render reminder icon */
  if (self->has_reminders)
    {
      GtkIconTheme *icon_theme;
      GtkIconInfo *icon_info;
      GdkPixbuf *pixbuf;
      gboolean was_symbolic;
      gint multiplier;

      multiplier = icon_size / 16;
      icon_theme = gtk_icon_theme_get_default ();
      icon_info = gtk_icon_theme_lookup_icon (icon_theme,
                                              "alarm-symbolic",
                                              16 * multiplier,
                                              0);
      pixbuf = gtk_icon_info_load_symbolic_for_context (icon_info,
                                                        context,
                                                        &was_symbolic,
                                                        NULL);

      gdk_cairo_set_source_pixbuf (cr, pixbuf, padding.left, padding.top + ((icon_size - (16 * multiplier)) / 2));
      g_object_unref (pixbuf);
      cairo_paint (cr);
    }

  /* render locked icon */
  if (self->read_only)
    {
      GtkIconTheme *icon_theme;
      GtkIconInfo *icon_info;
      GdkPixbuf *pixbuf;
      gboolean was_symbolic;
      gint multiplier;

      multiplier = icon_size / 16;
      icon_theme = gtk_icon_theme_get_default ();
      icon_info = gtk_icon_theme_lookup_icon (icon_theme,
                                              "changes-prevent-symbolic",
                                              16 * multiplier,
                                              0);
      pixbuf = gtk_icon_info_load_symbolic_for_context (icon_info,
                                                        context,
                                                        &was_symbolic,
                                                        NULL);

      gdk_cairo_set_source_pixbuf (cr, pixbuf, width - right_gap, padding.top + ((icon_size - (16 * multiplier)) / 2));
      g_object_unref (pixbuf);
      cairo_paint (cr);
    }

  pango_font_description_free (font_desc);
  g_object_unref (layout);

  return FALSE;
}

static gboolean
gcal_event_widget_button_press_event (GtkWidget      *widget,
                                      GdkEventButton *event)
{
  GcalEventWidget *self;

  self = GCAL_EVENT_WIDGET (widget);
  self->button_pressed = TRUE;

  return TRUE;
}

static gboolean
gcal_event_widget_button_release_event (GtkWidget      *widget,
                                        GdkEventButton *event)
{
  GcalEventWidget *self;

  self = GCAL_EVENT_WIDGET (widget);

  if (self->button_pressed)
    {
      self->button_pressed = FALSE;
      g_signal_emit (widget, signals[ACTIVATE], 0);
      return TRUE;
    }

  return FALSE;
}

GtkWidget*
gcal_event_widget_new (gchar *uuid)
{
  return g_object_new (GCAL_TYPE_EVENT_WIDGET, "uuid", uuid, NULL);
}

/**
 * gcal_event_widget_new_from_data:
 * @data: a #GcalEventData instance
 *
 * Create an event widget by passing its #ECalComponent and #ESource
 *
 * Returns: a #GcalEventWidget as #GtkWidget
 **/
GtkWidget*
gcal_event_widget_new_from_data (GcalEventData *data)
{
  GtkWidget *widget;
  GcalEventWidget *event;

  gchar *uuid;
  ECalComponentId *id;
  ECalComponentText e_summary;

  GQuark color_id;
  GdkRGBA color;
  gchar *color_str, *custom_css_class;

  ECalComponentDateTime dt;
  GDateTime *date;
  gboolean start_is_date, end_is_date;

  id = e_cal_component_get_id (data->event_component);
  if (id->rid != NULL)
    {
      uuid = g_strdup_printf ("%s:%s:%s",
                              e_source_get_uid (data->source),
                              id->uid,
                              id->rid);
    }
  else
    {
      uuid = g_strdup_printf ("%s:%s",
                              e_source_get_uid (data->source),
                              id->uid);
    }
  widget = g_object_new (GCAL_TYPE_EVENT_WIDGET, "uuid", uuid, NULL);
  e_cal_component_free_id (id);
  g_free (uuid);

  event = GCAL_EVENT_WIDGET (widget);
  event->component = data->event_component;
  event->source = data->source;

  /* summary */
  e_cal_component_get_summary (event->component, &e_summary);
  gcal_event_widget_set_summary (event, (gchar*) e_summary.value);

  /* color */
  get_color_name_from_source (event->source, &color);
  gcal_event_widget_set_color (event, &color);

  color_str = gdk_rgba_to_string (&color);
  color_id = g_quark_from_string (color_str);
  custom_css_class = g_strdup_printf ("color-%d", color_id);
  gtk_style_context_add_class (gtk_widget_get_style_context (widget), custom_css_class);
  g_free (custom_css_class);
  g_free (color_str);

  /* start date */
  e_cal_component_get_dtstart (event->component, &dt);
  date = icaltimetype_to_datetime (dt.value);

  start_is_date = datetime_is_date (date);
  if (!start_is_date)
    {
      GTimeZone *tz;
      GDateTime *tz_date;

      tz = g_time_zone_new (dt.tzid);
      tz_date = g_date_time_to_timezone (date, tz);

      g_clear_pointer (&date, g_date_time_unref);

      date = tz_date;
    }

  gcal_event_widget_set_date (event, date);
  e_cal_component_free_datetime (&dt);
  g_clear_pointer (&date, g_date_time_unref);

  /* end date */
  e_cal_component_get_dtend (event->component, &dt);
  if (dt.value != NULL)
    {
      date = icaltimetype_to_datetime (dt.value);

      end_is_date = datetime_is_date (date);
      if (!end_is_date)
        {
          GTimeZone *tz;
          GDateTime *tz_date;

          tz = g_time_zone_new (dt.tzid);
          tz_date = g_date_time_to_timezone (date, tz);

          g_clear_pointer (&date, g_date_time_unref);

          date = tz_date;
        }

      gcal_event_widget_set_end_date (event, date);
      e_cal_component_free_datetime (&dt);
      g_clear_pointer (&date, g_date_time_unref);

      /* set_all_day */
      gcal_event_widget_set_all_day (event, start_is_date && end_is_date);
    }

  /* set_has_reminders */
  gcal_event_widget_set_has_reminders (
      event,
      e_cal_component_has_alarms (event->component));

  return widget;
}

GtkWidget*
gcal_event_widget_new_with_summary_and_color (const gchar   *summary,
                                              const GdkRGBA *color)
{
  return g_object_new (GCAL_TYPE_EVENT_WIDGET,
                       "summary",
                       summary,
                       "color",
                       color,
                       NULL);
}

GtkWidget*
gcal_event_widget_clone (GcalEventWidget *widget)
{
  GtkWidget *new_widget;
  GcalEventData *data;

  data = gcal_event_widget_get_data (widget);
  g_object_ref (data->event_component);

  new_widget = gcal_event_widget_new_from_data (data);
  g_free (data);

  gcal_event_widget_set_read_only(GCAL_EVENT_WIDGET (new_widget), gcal_event_widget_get_read_only (widget));
  return new_widget;
}

const gchar*
gcal_event_widget_peek_uuid (GcalEventWidget *event)
{
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), NULL);

  return event->uuid;
}

void
gcal_event_widget_set_read_only (GcalEventWidget *event,
                                 gboolean         read_only)
{
  g_return_if_fail (GCAL_IS_EVENT_WIDGET (event));

  event->read_only = read_only;
}

gboolean
gcal_event_widget_get_read_only (GcalEventWidget *event)
{
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), FALSE);

  return event->read_only;
}

/**
 * gcal_event_widget_set_date:
 * @event: a #GcalEventWidget
 * @date: a #GDateTime object with the date
 *
 * Set the start-date of the event
 **/
void
gcal_event_widget_set_date (GcalEventWidget *event,
                            GDateTime       *date)
{
  g_object_set (event, "date-start", date, NULL);
}

/**
 * gcal_event_widget_get_date:
 * @event: a #GcalEventWidget
 *
 * Return the starting date of the event
 *
 * Returns: (transfer full): Release with g_free()
 **/
GDateTime*
gcal_event_widget_get_date (GcalEventWidget *event)
{
  GDateTime *dt;

  g_object_get (event, "date-start", &dt, NULL);
  return dt;
}

/**
 * gcal_event_widget_peek_start_date:
 * @event:
 *
 * Return the starting date of the event.
 *
 * Returns: (Transfer none): An #GDateTimeinstance
 **/
GDateTime*
gcal_event_widget_peek_start_date (GcalEventWidget *event)
{
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), NULL);

  return event->dt_start;
}

/**
 * gcal_event_widget_set_end_date:
 * @event: a #GcalEventWidget
 * @date: a #GDateTime object with the date
 *
 * Set the end date of the event
 **/
void
gcal_event_widget_set_end_date (GcalEventWidget *event,
                                GDateTime       *date)
{
  g_object_set (event, "date-end", date, NULL);
}

/**
 * gcal_event_widget_get_end_date:
 * @event: a #GcalEventWidget
 *
 * Return the end date of the event. If the event has no end_date
 * (as Google does on 0 sec events) %NULL will be returned
 *
 * Returns: (transfer full): Release with g_free()
 **/
GDateTime*
gcal_event_widget_get_end_date (GcalEventWidget *event)
{
  GDateTime *dt;

  g_object_get (event, "date-end", &dt, NULL);
  return dt;
}

/**
 * gcal_event_widget_peek_end_date:
 * @event:
 *
 * Return the end date of the event.
 *
 * Returns: (Transfer none): A #GDateTime instance
 **/
GDateTime*
gcal_event_widget_peek_end_date (GcalEventWidget *event)
{
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), NULL);

  return event->dt_end != NULL ? event->dt_end : event->dt_start;
}

void
gcal_event_widget_set_summary (GcalEventWidget *event,
                               gchar           *summary)
{
  g_return_if_fail (GCAL_IS_EVENT_WIDGET (event));

  g_object_set (event, "summary", summary, NULL);
}

gchar*
gcal_event_widget_get_summary (GcalEventWidget *event)
{
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), NULL);

  return g_strdup (event->summary);
}

void
gcal_event_widget_set_color (GcalEventWidget *event,
                             GdkRGBA         *color)
{
  g_return_if_fail (GCAL_IS_EVENT_WIDGET (event));

  g_object_set (event, "color", color, NULL);
}

GdkRGBA*
gcal_event_widget_get_color (GcalEventWidget *event)
{
  GdkRGBA *color;
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), NULL);

  color = NULL;
  g_object_get (event, "color", color, NULL);
  return color;
}

void
gcal_event_widget_set_all_day (GcalEventWidget *event,
                               gboolean         all_day)
{
  g_return_if_fail (GCAL_IS_EVENT_WIDGET (event));

  g_object_set (event, "all-day", all_day, NULL);
}

gboolean
gcal_event_widget_get_all_day (GcalEventWidget *event)
{
  gboolean all_day;
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), FALSE);

  g_object_get (event, "all-day", &all_day, NULL);
  return all_day;
}

gboolean
gcal_event_widget_is_multiday (GcalEventWidget *event)
{
  GDateTime *year_forward;
  guint days_in_year;
  gint start_day_of_year, end_day_of_year;

  if (event->dt_end == NULL)
    return FALSE;

  /* Calculates the number of days in the event's start date year */
  year_forward = g_date_time_add_years (event->dt_start, 1);
  days_in_year = g_date_time_difference (event->dt_start, year_forward) / G_TIME_SPAN_DAY;
  g_clear_pointer (&year_forward, g_date_time_unref);

  start_day_of_year = g_date_time_get_day_of_year (event->dt_start);
  end_day_of_year = g_date_time_get_day_of_year (event->dt_end);

  if (event->all_day && start_day_of_year + 1 == end_day_of_year)
    return FALSE;

  if (event->all_day &&
      start_day_of_year == days_in_year &&
      end_day_of_year == 1 &&
      g_date_time_get_year (event->dt_start) + 1 == g_date_time_get_year (event->dt_end))
    {
      return FALSE;
    }

  return start_day_of_year != end_day_of_year;
}

void
gcal_event_widget_set_has_reminders (GcalEventWidget *event,
                                     gboolean         has_reminders)
{
  g_return_if_fail (GCAL_IS_EVENT_WIDGET (event));

  g_object_set (event, "has-reminders", has_reminders, NULL);
}

gboolean
gcal_event_widget_get_has_reminders (GcalEventWidget *event)
{
  gboolean has_reminders;
  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), FALSE);

  g_object_get (event, "has-reminders", &has_reminders, NULL);
  return has_reminders;
}

/**
 * gcal_event_widget_get_data:
 * @event: a #GcalEventWidget instance
 *
 * Returns a #GcalEventData with shallows members, meaning the members
 * are owned but the struct should be freed.
 *
 * Returns: (transfer full): a #GcalEventData
 **/
GcalEventData*
gcal_event_widget_get_data (GcalEventWidget *event)
{
  GcalEventData *data;

  g_return_val_if_fail (GCAL_IS_EVENT_WIDGET (event), NULL);

  data = g_new0 (GcalEventData, 1);
  data->source = event->source;
  data->event_component = event->component;

  return data;
}

/**
 * gcal_event_widget_equal:
 * @widget1: an #GcalEventWidget representing an event
 * @widget2: an #GcalEventWidget representing an event
 *
 * Check if two widget represent the same event.
 *
 * Returns: %TRUE if both widget represent the same event,
 *          false otherwise
 **/
gboolean
gcal_event_widget_equal (GcalEventWidget *widget1,
                         GcalEventWidget *widget2)
{
  ECalComponentId *id1;
  ECalComponentId *id2;

  gboolean same_id = FALSE;

  if (!e_source_equal (widget1->source, widget2->source))
    return FALSE;

  id1 = e_cal_component_get_id (widget1->component);
  id2 = e_cal_component_get_id (widget2->component);
  same_id = e_cal_component_id_equal (id1, id2);

  e_cal_component_free_id (id1);
  e_cal_component_free_id (id2);

  return same_id;
}

/**
 * gcal_event_widget_compare_by_length:
 * @widget1:
 * @widget2:
 *
 * Compare two widgets by the duration of the events they represent. From shortest to longest span.
 *
 * Returns: negative value if a < b ; zero if a = b ; positive value if a > b
 **/
gint
gcal_event_widget_compare_by_length (GcalEventWidget *widget1,
                                     GcalEventWidget *widget2)
{
  GDateTime *start1, *start2;
  GDateTime *end1, *end2;

  start1 = end1 = widget1->dt_start;
  start2 = end2 = widget2->dt_start;

  if (widget1->dt_end)
    end1 = widget1->dt_end;
  if (widget2->dt_end)
    end2 = widget2->dt_end;

  return g_date_time_difference (start2, end2) - g_date_time_difference (start1, end1);
}

gint
gcal_event_widget_compare_by_start_date (GcalEventWidget *widget1,
                                         GcalEventWidget *widget2)
{
  return g_date_time_compare (widget1->dt_start, widget2->dt_start);
}

/**
 * gcal_event_widget_compare_for_single_day:
 * @widget1:
 * @widget2:
 *
 * Compare widgets by putting those that span over a day before the rest, and between those
 * who last less than a day by its start time/date
 *
 * Returns:
 **/
gint
gcal_event_widget_compare_for_single_day (GcalEventWidget *widget1,
                                          GcalEventWidget *widget2)
{
  if (gcal_event_widget_is_multiday (widget1) && gcal_event_widget_is_multiday (widget2))
    {
      gint result;

      result = gcal_event_widget_compare_by_length (widget1, widget2);

      if (result != 0)
        return result;
      else
        return g_date_time_compare (widget1->dt_start, widget2->dt_start);
    }
  else
    {
      if (gcal_event_widget_is_multiday (widget1))
        return -1;
      else if (gcal_event_widget_is_multiday (widget2))
        return 1;
      else
        {
          if (widget1->all_day && widget2->all_day)
            return 0;
          else if (widget1->all_day)
            return -1;
          else if (widget2->all_day)
            return 1;
          else
            return g_date_time_compare (widget1->dt_start, widget2->dt_start);
        }
    }
}
