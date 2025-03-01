/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-edit-dialog.h
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

#ifndef __GCAL_EDIT_DIALOG_H__
#define __GCAL_EDIT_DIALOG_H__

#include "gcal-manager.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCAL_RESPONSE_DELETE_EVENT 2
#define GCAL_RESPONSE_SAVE_EVENT   4
#define GCAL_RESPONSE_CREATE_EVENT 6

#define GCAL_TYPE_EDIT_DIALOG                (gcal_edit_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GcalEditDialog, gcal_edit_dialog, GCAL, EDIT_DIALOG, GtkDialog);

GtkWidget*           gcal_edit_dialog_new                     (gboolean format_24h);

void                 gcal_edit_dialog_set_event_is_new        (GcalEditDialog *dialog,
                                                               gboolean       event_is_new);

void                 gcal_edit_dialog_set_event_data          (GcalEditDialog *dialog,
                                                               GcalEventData  *data);

void                 gcal_edit_dialog_set_manager             (GcalEditDialog *dialog,
                                                               GcalManager    *manager);

ECalComponent*       gcal_edit_dialog_get_component           (GcalEditDialog *dialog);

ESource*             gcal_edit_dialog_get_source              (GcalEditDialog *dialog);

const gchar*         gcal_edit_dialog_peek_source_uid         (GcalEditDialog *dialog);

const gchar*         gcal_edit_dialog_peek_event_uid          (GcalEditDialog *dialog);

gchar*               gcal_edit_dialog_get_event_uuid          (GcalEditDialog *dialog);

icaltimetype*        gcal_edit_dialog_get_start_date          (GcalEditDialog *dialog);

icaltimetype*        gcal_edit_dialog_get_end_date            (GcalEditDialog *dialog);

G_END_DECLS

#endif /* __GCAL_EDIT_DIALOG_H__ */
