/*
 * vdagent session
 * Copyright (C) 2014  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef VDAGENT_CLIPBOARD_H_
#define VDAGENT_CLIPBOARD_H_

#include <gtk/gtk.h>
#include "vdagent.h"

G_BEGIN_DECLS

void vdagent_clipboard_init             (SpiceVDAgent *agent);
void vdagent_clipboard_request          (SpiceVDAgent *agent, guint8 selection, guint32 type);
void vdagent_clipboard_grab             (SpiceVDAgent *agent, guint8 selection,
                                         guint32 *types, gsize ntypes);
void vdagent_clipboard_data             (SpiceVDAgent *agent, guint8 selection, guint32 types,
                                         gpointer data, gsize len);
void vdagent_clipboard_release          (SpiceVDAgent *agent, guint8 selection);
void vdagent_clipboard_release_all      (SpiceVDAgent *agent);

G_END_DECLS

#endif
