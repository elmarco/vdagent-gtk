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

#include "utils.h"

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GStrv strv_from_data(char *data, gsize len, gssize *pos)
{
    GStrv strv;
    gssize i, n;

    g_return_val_if_fail(len >= 2, NULL);

    for (i = 0, n = 0; i < len; i++) {
        if (data[i])
            continue;

        if (i >= 1 && !data[i-1])
            break;
        n++;
    }

    g_return_val_if_fail(i < len, NULL);

    strv = g_new0(gchar *, n + 1);
    strv[n] = NULL;

    for (i = 0, n = 0; i < len; i++) {
        if (!data[i] && i >= 1 && !data[i-1])
            break;

        if (!strv[n])
            strv[n] = g_strndup(data + i, len - i);

        if (!data[i])
            n++;
    }

    if (pos)
        *pos = i + 1;

    return strv;
}

gchar* str_from_data(char *data, gsize len, gssize *pos)
{
    gchar *str;
    int i;

    g_return_val_if_fail(len >= 1, NULL);

    for (i = 0; i < len; i++)
        if (!data[i])
            break;

    g_return_val_if_fail(i < len, NULL);

    str = g_strdup(data);
    if (pos)
        *pos = strlen(str) + 1;

    return str;
}

static void
log_handler(const gchar *log_domain, GLogLevelFlags log_level,
            const gchar *message, gpointer user_data)
{
    int level;

    /* Note that crit and err are the other way around in syslog */

    switch (G_LOG_LEVEL_MASK & log_level) {
    case G_LOG_LEVEL_ERROR:
        level = LOG_CRIT;
        break;
    case G_LOG_LEVEL_CRITICAL:
        level = LOG_ERR;
        break;
    case G_LOG_LEVEL_WARNING:
        level = LOG_WARNING;
        break;
    case G_LOG_LEVEL_MESSAGE:
        level = LOG_NOTICE;
        break;
    case G_LOG_LEVEL_INFO:
        level = LOG_INFO;
        break;
    case G_LOG_LEVEL_DEBUG:
        level = -1;
        break;
    default:
        level = LOG_ERR;
        break;
    }

    /* Log to syslog first */
    if (level != -1) {
        if (log_domain)
            syslog (level, "%s: %s", log_domain, message);
        else
            syslog (level, "%s", message);
    }

    /* And then to default handler for aborting and stuff like that */
    g_log_default_handler (log_domain, log_level, message, user_data);
}

static void
printerr_handler(const gchar *string)
{
    /* Print to syslog and stderr */
    syslog(LOG_WARNING, "%s", string);
    fprintf(stderr, "%s", string);
}

void
gsyslog_install(const char *domain)
{
    GLogLevelFlags flags = G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR |
        G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING |
        G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO;

    openlog (domain, LOG_PID, LOG_AUTH);

    g_log_set_handler (NULL, flags, log_handler, NULL);
    g_log_set_handler ("Glib", flags, log_handler, NULL);
    g_log_set_handler ("Gtk", flags, log_handler, NULL);
    g_log_set_default_handler (log_handler, NULL);
    g_set_printerr_handler (printerr_handler);
}

typedef struct
{
    void         *buffer;
    gsize         count;
    int           io_priority;
    GCancellable *cancellable;
    gsize         bytes_read;
} ReadAllData;

static void
free_read_all_data (ReadAllData *read_data)
{
    if (read_data->cancellable)
        g_object_unref (read_data->cancellable);

    g_slice_free (ReadAllData, read_data);
}

static void
read_all_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GInputStream *stream = G_INPUT_STREAM (source_object);
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);

    gsize bytes_read;
    GError *err = NULL;
    ReadAllData *read_data;

    bytes_read = g_input_stream_read_finish (stream, res, &err);
    g_debug("bytes read %" G_GSIZE_FORMAT, bytes_read);
    if (bytes_read == -1) {
        g_simple_async_result_take_error (simple, err);
        goto done;
    } else if (bytes_read == 0) {
        g_simple_async_result_set_error (simple, G_IO_ERROR, G_IO_ERROR_CLOSED, "stream closed");
        goto done;
    }

    read_data = g_simple_async_result_get_op_res_gpointer (simple);
    read_data->bytes_read += bytes_read;
    if (read_data->bytes_read < read_data->count) {
        g_input_stream_read_async (stream,
                                   (guint8 *)read_data->buffer + read_data->bytes_read,
                                   read_data->count - read_data->bytes_read, 0,
                                   read_data->cancellable, read_all_cb, simple);
        return;
    }

 done:
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

/* See also https://bugzilla.gnome.org/show_bug.cgi?id=679662 */
void
input_stream_read_all_async (GInputStream        *stream,
                             void                *buffer,
                             gsize                count,
                             int                  io_priority,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    ReadAllData *read_data;
    GSimpleAsyncResult *simple;

    read_data = g_slice_new0 (ReadAllData);
    read_data->buffer = buffer;
    read_data->count = count;
    read_data->io_priority = io_priority;
    if (cancellable)
        read_data->cancellable = g_object_ref (cancellable);

    simple = g_simple_async_result_new (G_OBJECT (stream), callback, user_data,
                                        input_stream_read_all_async);
    g_simple_async_result_set_op_res_gpointer (simple, read_data,
                                               (GDestroyNotify)free_read_all_data);

    g_debug("reading %" G_GSIZE_FORMAT, count);
    g_input_stream_read_async (stream, buffer, count, io_priority, cancellable,
                               read_all_cb, simple);
}

gboolean
input_stream_read_all_finish (GInputStream *stream,
                              GAsyncResult *res,
                              gsize        *bytes_read,
                              GError      **error)
{
    GSimpleAsyncResult *simple;

    g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (stream),
                                                          input_stream_read_all_async),
                          FALSE);

    simple = (GSimpleAsyncResult *)res;

    if (g_simple_async_result_propagate_error (simple, error))
        return FALSE;

    if (bytes_read) {
        ReadAllData *read_data;

        read_data = g_simple_async_result_get_op_res_gpointer (simple);
        *bytes_read = read_data->bytes_read;
    }

    return TRUE;
}
