/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright (C) Carl-Anton Ingmarsson 2011 <ca.ingmarsson@gmail.com>
 * Copyright 2010-2014 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef UTILS_H_
#define UTILS_H_

#include <gio/gio.h>

G_BEGIN_DECLS

void
gsyslog_install(const gchar *domain);

void
input_stream_read_all_async (GInputStream        *stream,
                             void                *buffer,
                             gsize                count,
                             int                  io_priority,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data);

gboolean
input_stream_read_all_finish (GInputStream *stream,
                              GAsyncResult *res,
                              gsize        *bytes_read,
                              GError      **error);

G_END_DECLS

#endif
