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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "utils.h"
#include "vdagent.h"
#include "vdagent-clipboard.h"

G_DEFINE_TYPE (SpiceVDAgent, spice_vdagent, G_TYPE_OBJECT);

static const char *vdagentd_socket = "/var/run/spice-vdagentd/spice-vdagent-sock";
static gboolean version_mismatch = FALSE;
static gboolean quit = FALSE;

static void read_new_message(SpiceVDAgent *agent);
static void send_xorg_config(SpiceVDAgent *self);

static void
spice_vdagent_init(SpiceVDAgent *self)
{
    self->cancellable = g_cancellable_new();

    vdagent_clipboard_init(self);

    self->outq = g_queue_new();

    g_signal_connect_swapped(gdk_screen_get_default(), "monitors-changed",
                             G_CALLBACK(send_xorg_config), self);
}

static void
spice_vdagent_finalize(GObject *gobject)
{
    SpiceVDAgent *self = SPICE_VDAGENT(gobject);

    g_signal_handlers_disconnect_by_data(gdk_screen_get_default(), self);

    g_clear_object(&self->connection);
#ifdef G_OS_UNIX
    g_clear_object(&self->connectable);
    g_clear_object(&self->socket);
#endif

    g_free(self->data);
    g_queue_free_full(self->outq, g_free);

    if (G_OBJECT_CLASS(spice_vdagent_parent_class)->finalize)
        G_OBJECT_CLASS(spice_vdagent_parent_class)->finalize(gobject);
}

static void spice_vdagent_class_init(SpiceVDAgentClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = spice_vdagent_finalize;
}

typedef struct _Msg {
    gsize size;
    guint8 *data;
    GFreeFunc free_func;
} Msg;

static void kick_write(SpiceVDAgent *self);

static void
msg_write_cb(GObject *source_object,
             GAsyncResult *res,
             gpointer user_data)
{
    SpiceVDAgent *self = SPICE_VDAGENT(user_data);
    GError *error = NULL;
    Msg *msg;
    gssize ret;

    msg = g_queue_peek_head(self->outq);
    g_return_if_fail(msg != NULL);

    ret = g_output_stream_write_finish(G_OUTPUT_STREAM(source_object), res, &error);
    self->writing = FALSE;

    if (ret == -1) {
        g_warning("failed to write: %s", error->message);
        g_clear_error(&error);
    } else {
        self->pos += ret;
        g_assert(self->pos <= msg->size);

        g_debug("wrote %" G_GSSIZE_FORMAT "/%" G_GSSIZE_FORMAT, self->pos,  msg->size);
        if (self->pos == msg->size) {
            if (msg->free_func)
                msg->free_func(msg->data);
            g_slice_free(Msg, g_queue_pop_head(self->outq));
            self->pos = 0;
        }
        kick_write(self);
    }
}

static void
kick_write(SpiceVDAgent *self)
{
    Msg *msg = g_queue_peek_head(self->outq);

    if (!msg || self->writing)
        return;

    GOutputStream *out = g_io_stream_get_output_stream(self->connection);
    g_output_stream_write_async(out, msg->data + self->pos,
                                msg->size - self->pos,
                                G_PRIORITY_DEFAULT, self->cancellable,
                                msg_write_cb, self);
    self->writing = TRUE;
}

void
spice_vdagent_write(SpiceVDAgent *self,
                    gpointer data, guint32 size,
                    GFreeFunc free_func)
{
    g_return_if_fail(SPICE_IS_VDAGENT(self));
    g_return_if_fail(data && size);

    Msg *msg = g_slice_new(Msg);

    msg->size = size;
    msg->data = data;
    msg->free_func = free_func;

    g_queue_push_tail(self->outq, msg);
    kick_write(self);
}

void
spice_vdagent_write_header(SpiceVDAgent *self,
                           guint32 type, guint32 arg1, guint32 arg2, guint32 size)
{
    g_return_if_fail(SPICE_IS_VDAGENT(self));

    VDAgentdHeader *header = g_new(VDAgentdHeader, 1);
    header->type = type;
    header->arg1 = arg1;
    header->arg2 = arg2;
    header->size = size;

    spice_vdagent_write(self, header, sizeof(*header), g_free);
}

void
spice_vdagent_write_msg(SpiceVDAgent *self,
                        guint32 type, guint32 arg1, guint32 arg2,
                        gpointer data, guint32 size, GFreeFunc free_func)
{

    g_return_if_fail(SPICE_IS_VDAGENT(self));

    spice_vdagent_write_header(self, type, arg1, arg2, size);

    if (size)
        spice_vdagent_write(self, data, size, free_func);
}

typedef struct _VDAgentdRes {
    int width;
    int height;
    int x;
    int y;
} VDAgentdRes;

static void
send_xorg_config(SpiceVDAgent *self)
{
    GdkRectangle mon;
    int i, nres = gdk_screen_get_n_monitors(gdk_screen_get_default());
    VDAgentdRes *res = g_new(VDAgentdRes, nres);

    for (i = 0; i < nres; i++) {
        gdk_screen_get_monitor_geometry(gdk_screen_get_default(), i, &mon);
        res[i].width = mon.width;
        res[i].height = mon.height;
        res[i].x = mon.x;
        res[i].y = mon.y;
    }

    spice_vdagent_write_msg(self, VDAGENTD_GUEST_XORG_RESOLUTION,
                            gdk_screen_width(), gdk_screen_height(),
                            res, sizeof(*res) * nres, g_free);
}

static void
dispatch_message(SpiceVDAgent *agent)
{
    VDAgentdHeader *header = &agent->header;
    gpointer data = agent->data;
    gchar *type = NULL;
    GStrv types = NULL;
    gssize pos = 0;

    switch (header->type) {
    case VDAGENTD_MONITORS_CONFIG:
        g_warning("monitors config is not handled by agent anymore");
        break;
    case VDAGENTD_FILE_XFER_START:
    case VDAGENTD_FILE_XFER_STATUS:
    case VDAGENTD_FILE_XFER_DATA:
        g_warning("file-xfer is deprecated");
        break;
    case VDAGENTD_CLIPBOARD_REQUEST:
        type = str_from_data(data, header->size, &pos);
        vdagent_clipboard_request(agent, header->arg1, type);
        break;
    case VDAGENTD_CLIPBOARD_GRAB:
        types = strv_from_data(data, header->size, &pos);
        vdagent_clipboard_grab(agent, header->arg1, types);
        break;
    case VDAGENTD_CLIPBOARD_DATA:
        type = str_from_data(data, header->size, &pos);
        vdagent_clipboard_data(agent, header->arg1, type, data + pos, header->size - pos);
        break;
    case VDAGENTD_CLIPBOARD_RELEASE:
        vdagent_clipboard_release(agent, header->arg1);
        break;
    case VDAGENTD_VERSION:
        if (g_strcmp0(data, VERSION)) {
            g_message("vdagentd version mismatch: got %s expected %s", (gchar*)data, VERSION);
            gtk_main_quit();
            version_mismatch = TRUE;
        }
        break;
    case VDAGENTD_CLIENT_DISCONNECTED:
        vdagent_clipboard_release_all(agent);
        break;
    default:
        g_warning("Unknown message from vdagentd type: %d, ignoring", header->type);
    }

    g_free(type);
    g_strfreev(types);
}

static void
message_data_cb(GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
    SpiceVDAgent *agent = user_data;
    VDAgentdHeader *header = &agent->header;
    GError *error = NULL;
    gboolean success;
    gsize bread;

    success = input_stream_read_all_finish(G_INPUT_STREAM(source_object), res, &bread, &error);
    if (!success)
        g_warning("%s", error->message);

    if (bread != header->size) {
        g_warning("failed to read all data, quit: %" G_GSIZE_FORMAT, bread);
        gtk_main_quit();
    } else {
        dispatch_message(agent);
        read_new_message(agent);
    }

    g_clear_error(&error);
}

static void
message_header_cb(GObject *source_object,
                  GAsyncResult *res,
                  gpointer user_data)
{
    SpiceVDAgent *agent = user_data;
    VDAgentdHeader *header = &agent->header;
    GError *error = NULL;
    gboolean success;
    gsize bread;

    success = input_stream_read_all_finish (G_INPUT_STREAM(source_object), res, &bread, &error);
    if (!success)
        g_warning("%s", error->message);

    if (bread != sizeof(*header)) {
        g_warning("failed to read all header, quit");
        gtk_main_quit();
    } else {
        g_debug("Header type:%u size:%u (%u, %u)",
                header->type, header->size, header->arg1, header->arg2);

        if (header->size != 0) {
            agent->data = g_realloc(agent->data, header->size);
            input_stream_read_all_async(g_io_stream_get_input_stream(agent->connection),
                                        agent->data, header->size,
                                        G_PRIORITY_DEFAULT,
                                        agent->cancellable,
                                        message_data_cb,
                                        agent);
        } else {
            dispatch_message(agent);
            read_new_message(agent);
        }
    }

    g_clear_error(&error);
}

static void
read_new_message(SpiceVDAgent *agent)
{
    input_stream_read_all_async(g_io_stream_get_input_stream(agent->connection),
                                &agent->header, sizeof(agent->header),
                                G_PRIORITY_DEFAULT,
                                agent->cancellable,
                                message_header_cb,
                                agent);
}

/* TODO: could use dbus, but for portability maybe not..... */
static void
do_connect(SpiceVDAgent *agent)
{
    GError *error = NULL;
    GSocketAddressEnumerator *enumerator;
    GSocketAddress *address;

#ifdef G_OS_UNIX
    agent->socket = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, 0, &error);
    agent->connectable = G_SOCKET_CONNECTABLE(g_unix_socket_address_new(vdagentd_socket));
#endif

    enumerator = g_socket_connectable_enumerate (agent->connectable);
    while (TRUE) {
        address = g_socket_address_enumerator_next (enumerator, agent->cancellable, NULL);
        if (!address) {
            g_error("can't connect to socket");
            exit(1);
        }

        if (g_socket_connect(agent->socket, address, agent->cancellable, &error))
            break;

        g_debug("Connection failed, trying next");
        g_clear_error (&error);
        g_object_unref (address);
    }
    g_object_unref(enumerator);

    agent->connection =
        G_IO_STREAM(g_socket_connection_factory_create_connection(agent->socket));
    g_debug("Connected to %s %p", vdagentd_socket, agent->connection);

    send_xorg_config(agent);
}

static void
quit_handler(int sig)
{
    quit = TRUE;
    gtk_main_quit();
}

static void
run_agent(void)
{
    SpiceVDAgent *agent = g_object_new(SPICE_TYPE_VDAGENT, NULL);

    g_debug("Starting up new session");

    do_connect(agent);
    read_new_message(agent);

    gtk_main();

    g_debug("Finishing session");
    g_clear_object(&agent);
}

static GOptionEntry options[] =
{
  { "socket", 'S', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &vdagentd_socket, "vdagentd socket", "PATH" },
  { NULL }
};

int
main(int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new("- spice-vdagent " VERSION);
    g_option_context_add_main_entries(context, options, NULL);
    g_option_context_add_group(context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }

    gsyslog_install("spice-vdagent");

    {
        struct sigaction act = { .sa_flags = SA_RESTART, .sa_handler = quit_handler };
        sigaction(SIGINT, &act, NULL);
        sigaction(SIGTERM, &act, NULL);
        sigaction(SIGQUIT, &act, NULL);
    }

    while (!quit) {
        if (version_mismatch) {
            g_warning("Version mismatch, restarting");
            sleep(1);
            execvp(argv[0], argv);
        }

        run_agent();
    }

    return 0;
}
