/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-date-selector.c
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

#include "gcal-date-selector.h"

#include <locale.h>
#include <langinfo.h>
#include <stdlib.h>
#include <glib/gi18n.h>

enum
{
  DAY,
  MONTH,
  YEAR,
  NUM_ENTRIES
};

struct _GcalDateSelector
{
  GtkMenuButton parent;

  /* widgets */
  GtkWidget   *date_label;
  GtkWidget   *calendar;
  GtkWidget   *grid;

  GtkWidget   *entries[NUM_ENTRIES];

  /* date */
  GDate       *date;

  /* misc */
  const gchar *mask;

  /* index in the mask starting by 0 */
  guint        day_pos;
  guint        month_pos;
  guint        year_pos;
} GcalDateSelectorPrivate;

enum
{
  MODIFIED,
  NUM_SIGNALS
};

enum
{
  PROP_0,
  PROP_DATE,
  NUM_PROPS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void     calendar_day_selected                             (GtkCalendar          *calendar,
                                                                   gpointer              user_data);

static gboolean date_entry_focus_out                              (GtkWidget            *widget,
                                                                   GdkEvent             *event,
                                                                   gpointer              user_data);

static void     entry_activated                                   (GtkEntry             *entry,
                                                                   gpointer              user_data);

static void     parse_entries                                     (GcalDateSelector     *selector);

static void     text_inserted                                     (GtkEditable          *editable,
                                                                   gchar                *new_text,
                                                                   gint                  new_text_length,
                                                                   gint                 *position,
                                                                   gpointer              user_data);

static void     gcal_date_selector_constructed                    (GObject              *object);

G_DEFINE_TYPE (GcalDateSelector, gcal_date_selector, GTK_TYPE_MENU_BUTTON);

static void
calendar_day_selected (GtkCalendar *calendar,
                       gpointer     user_data)
{
  GcalDateSelector *selector;
  guint day, month, year;

  selector = GCAL_DATE_SELECTOR (user_data);
  gtk_calendar_get_date (calendar, &year, &month, &day);

  /**
   * Block signal handler to avoid an infinite
   * recursion, exploding the proccess stack.
   */
  g_signal_handlers_block_by_func (selector->calendar, calendar_day_selected, user_data);

  gcal_date_selector_set_date (GCAL_DATE_SELECTOR (user_data), day, month + 1, year);

  g_signal_handlers_unblock_by_func (selector->calendar, calendar_day_selected, user_data);
}

static gboolean
date_entry_focus_out (GtkWidget *widget,
                      GdkEvent  *event,
                      gpointer   user_data)
{
  parse_entries (GCAL_DATE_SELECTOR (user_data));
  return FALSE;
}

static void
entry_activated (GtkEntry *entry,
                 gpointer  user_data)
{
  parse_entries (GCAL_DATE_SELECTOR (user_data));
}

static void
parse_entries (GcalDateSelector *selector)
{
  gint day, month, year;

  day = atoi (gtk_entry_get_text (GTK_ENTRY (selector->entries[DAY])));
  month = atoi (gtk_entry_get_text (GTK_ENTRY (selector->entries[MONTH])));
  year = atoi (gtk_entry_get_text (GTK_ENTRY (selector->entries[YEAR])));

  /* select the date */
  g_signal_handlers_block_by_func (selector->calendar, calendar_day_selected, selector);

  gcal_date_selector_set_date (selector, day, month, year);

  g_signal_handlers_unblock_by_func (selector->calendar, calendar_day_selected, selector);
}

static void
text_inserted (GtkEditable *editable,
               gchar       *new_text,
               gint         new_text_length,
               gint        *position,
               gpointer     user_data)
{
  GtkEntryBuffer *buffer;
  gchar* new_label;
  gint i;
  gint current_length, max_length;
  gboolean valid;

  valid = TRUE;
  buffer = gtk_entry_get_buffer (GTK_ENTRY (editable));
  current_length = gtk_entry_buffer_get_length (buffer);
  max_length = gtk_entry_get_max_length (GTK_ENTRY (editable));

  /* honor max length property */
  if (current_length + new_text_length > max_length)
    return;

  /* stop the default implementation */
  g_signal_stop_emission (editable, g_signal_lookup ("insert-text", GTK_TYPE_ENTRY), 0);

  for (i = 0; i < new_text_length; i++)
    {
      gchar c;
      c = *(new_text + i);

      /* trying to insert a non-numeric char */
      if (c < '0' || c > '9')
        {
          valid = FALSE;
          break;
        }
    }

  if (!valid)
    return;

  new_label = g_strdup_printf ("%s%s", gtk_entry_buffer_get_text (buffer), new_text);
  gtk_entry_buffer_set_text (buffer, new_label, current_length + new_text_length);
  *position = *position + new_text_length;

  g_free (new_label);
}

static void
gcal_date_selector_finalize (GObject *object)
{
  GcalDateSelector *self = GCAL_DATE_SELECTOR (object);

  g_clear_pointer (&self->date, g_date_free);

  G_OBJECT_CLASS (gcal_date_selector_parent_class)->finalize (object);
}

static void
gcal_date_selector_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GcalDateSelector *self = GCAL_DATE_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_DATE:
      g_value_set_boxed (value, self->date);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gcal_date_selector_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GcalDateSelector *self = GCAL_DATE_SELECTOR (object);
  GDate *date;

  switch (prop_id)
    {
    case PROP_DATE:
      date = g_value_get_boxed (value);
      gcal_date_selector_set_date (self,
                                   g_date_get_year (date),
                                   g_date_get_month (date),
                                   g_date_get_day (date));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gcal_date_selector_class_init (GcalDateSelectorClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gcal_date_selector_finalize;
  object_class->constructed = gcal_date_selector_constructed;
  object_class->get_property = gcal_date_selector_get_property;
  object_class->set_property = gcal_date_selector_set_property;

  /**
   * GcalDateSelector::date:
   *
   * The current #GDate selected by the date selector.
   */
  g_object_class_install_property (object_class,
                                   PROP_DATE,
                                   g_param_spec_boxed ("date",
                                                       "Date of the selector",
                                                       "The date of the selector",
                                                       G_TYPE_DATE,
                                                       G_PARAM_READWRITE));

  signals[MODIFIED] = g_signal_new ("modified",
                                    GCAL_TYPE_DATE_SELECTOR,
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/calendar/date-selector.ui");

  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GcalDateSelector, date_label);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GcalDateSelector, calendar);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GcalDateSelector, grid);

  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), text_inserted);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), date_entry_focus_out);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), entry_activated);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), calendar_day_selected);
}

static void
gcal_date_selector_init (GcalDateSelector *self)
{
  gint i, d_index, max;

  gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->date = g_date_new_dmy (1, 1, 1970);

  /* This string represents day/month/year order for each of the different
   * languages. It could possibly be default value, %m/%d/%y placing the month
   * before, or any ordering according to the translator's environment. */
  self->mask = g_dpgettext2 ("glib20", "GDateTime", "%m/%d/%y");

  /**
   * Translators: Select the day, month and year indexes. This will
   * be used later on to map the date entries to the
   * corresponding indexes. I should have added more validations here.
   */
  max = strlen (self->mask);
  if (max != 8)
    {
      /* I'll assume an error and bail out with the default values */
      self->day_pos = 0;
      self->month_pos = 1;
      self->year_pos = 2;
      return;
    }

  d_index = 0;

  for (i = 0; i < max; i++)
    {
      gchar c;

      c = *(self->mask + i);

      /* No need to check these common separators */
      if (c == '%' || c == '-' || c == '/' || c == '.')
        continue;

      switch (c)
        {
          /* day */
          case 'a':
          case 'A':
          case 'd':
          case 'e':
          case 'j':
          case 'u':
          case 'w':
            self->day_pos = d_index++;
            break;

          /* month */
          case 'b':
          case 'B':
          case 'm':
            self->month_pos = d_index++;
            break;

          /* year */
          case 'y':
            self->year_pos = d_index++;
            break;

          case 'Y':
            self->year_pos = d_index++;
            break;
        }
    }
}

static void
gcal_date_selector_constructed (GObject *object)
{
  GcalDateSelector *selector;

  GtkWidget *label, *box;
  GList *l, *aux;

  selector = GCAL_DATE_SELECTOR (object);

  /* chaining up */
  G_OBJECT_CLASS (gcal_date_selector_parent_class)->constructed (object);

  /* set labels, on the first row */
  label = gtk_grid_get_child_at (GTK_GRID (selector->grid), selector->day_pos, 0);
  gtk_label_set_text (GTK_LABEL (label), _("Day"));
  label = gtk_grid_get_child_at (GTK_GRID (selector->grid), selector->month_pos, 0);
  gtk_label_set_text (GTK_LABEL (label), _("Month"));
  label = gtk_grid_get_child_at (GTK_GRID (selector->grid), selector->year_pos, 0);
  gtk_label_set_text (GTK_LABEL (label), _("Year"));

  /* retrieve components from UI definition: entries */
  box = gtk_grid_get_child_at (GTK_GRID (selector->grid), 0, 1);
  aux = gtk_container_get_children (GTK_CONTAINER (box));
  for (l = aux; l != NULL; l = g_list_next (l))
    {
      gint position;
      gtk_container_child_get (GTK_CONTAINER (box), l->data, "position", &position, NULL);
      if (position == selector->day_pos)
        selector->entries[DAY] = l->data;
      if (position == selector->month_pos)
        selector->entries[MONTH] = l->data;
      if (position == selector->year_pos)
        selector->entries[YEAR] = l->data;

      if (position == selector->day_pos || position == selector->month_pos)
        gtk_entry_set_max_length (GTK_ENTRY (l->data), 2);
    }
  g_list_free (aux);

  gtk_widget_set_direction (box, GTK_TEXT_DIR_LTR);
  gtk_widget_set_direction (selector->grid, GTK_TEXT_DIR_LTR);
}

/* Public API */
GtkWidget*
gcal_date_selector_new (void)
{
  return g_object_new (GCAL_TYPE_DATE_SELECTOR, NULL);
}

/**
 * gcal_date_selector_set_date:
 * @selector:
 * @day:  (nullable): The day number.
 * @month: (nullable): The month number starting with 1
 * @year: (nullable): The year number
 *
 * Set the value of the date shown
 **/
void
gcal_date_selector_set_date (GcalDateSelector *selector,
                             gint              day,
                             gint              month,
                             gint              year)
{
  GDateTime *dt;
  gchar *label;

  g_return_if_fail (GCAL_IS_DATE_SELECTOR (selector));
  day = CLAMP (day, 1, 31);
  month = CLAMP (month, 1, 12);
  /* since we're dealing only with the date, the tz shouldn't be a problem */
  dt = g_date_time_new_local (year, month, day, 0, 0, 0);

  /**
   * When it fails to be instances, it's
   * because edit dialog is cleaning it's
   * data. Thus, we should stop here.
   */
  if (dt == NULL)
    return;

  g_clear_pointer (&selector->date, g_date_free);
  selector->date = g_date_new_dmy (day, month, year);

  /* set calendar's date */
  g_signal_handlers_block_by_func (selector->calendar, calendar_day_selected, selector);
  g_object_set (selector->calendar, "day", day, "month", month - 1, "year", year, NULL);
  g_signal_handlers_unblock_by_func (selector->calendar, calendar_day_selected, selector);

  /* rebuild the date label */
  label = g_date_time_format (dt, selector->mask);

  gtk_label_set_label (GTK_LABEL (selector->date_label), label);
  g_free (label);

  /* set date entries' text */
  /* day entry */
  label = g_strdup_printf ("%.2d", day);

  gtk_entry_set_text (GTK_ENTRY (selector->entries[DAY]), label);
  g_free (label);

  /* month entry */
  label = g_strdup_printf ("%.2d", month);

  gtk_entry_set_text (GTK_ENTRY (selector->entries[MONTH]), label);
  g_free (label);

  /* year entry */
  label = g_strdup_printf ("%.4d", year);

  gtk_entry_set_text (GTK_ENTRY (selector->entries[YEAR]), label);

  /* emit the MODIFIED signal */
  g_signal_emit (selector, signals[MODIFIED], 0);

  g_free (label);
  g_date_time_unref (dt);
}

/**
 * gcal_date_selector_get_date:
 * @selector:
 * @day:  (nullable): An out argument to hold the day number
 * @month: (nullable): An out argument to hold the month number starting with 1
 * @year: (nullable): An out argument to hold the year number
 *
 * Get the value of the date shown
 **/
void
gcal_date_selector_get_date (GcalDateSelector *selector,
                             gint             *day,
                             gint             *month,
                             gint             *year)
{
  gint dday;
  gint dmonth;
  gint dyear;

  g_return_if_fail (GCAL_IS_DATE_SELECTOR (selector));

  dday = 1;
  dmonth = 1;
  dyear = 1970;

  if (selector->date)
    {
      dday = g_date_get_day (selector->date);
      dmonth = g_date_get_month (selector->date);
      dyear = g_date_get_year (selector->date);
    }

  if (day != NULL)
    *day = dday;
  if (month != NULL)
    *month = dmonth;
  if (year != NULL)
    *year = dyear;
}
