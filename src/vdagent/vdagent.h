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

#ifndef VDAGENT_H_
#define VDAGENT_H_

#include <spice/vd_agent.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#ifdef G_OS_UNIX
#include <gio/gunixsocketaddress.h>
#endif

G_BEGIN_DECLS

#define SPICE_TYPE_VDAGENT            (spice_vdagent_get_type ())
#define SPICE_VDAGENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPICE_TYPE_VDAGENT, SpiceVDAgent))
#define SPICE_VDAGENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPICE_TYPE_VDAGENT, SpiceVDAgentClass))
#define SPICE_IS_VDAGENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPICE_TYPE_VDAGENT))
#define SPICE_IS_VDAGENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPICE_TYPE_VDAGENT))
#define SPICE_VDAGENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPICE_TYPE_VDAGENT, SpiceVDAgentClass))

enum {
    VDAGENTD_GUEST_XORG_RESOLUTION,
    VDAGENTD_MONITORS_CONFIG,
    VDAGENTD_CLIPBOARD_GRAB,
    VDAGENTD_CLIPBOARD_REQUEST,
    VDAGENTD_CLIPBOARD_DATA,
    VDAGENTD_CLIPBOARD_RELEASE,
    VDAGENTD_VERSION,
    VDAGENTD_FILE_XFER_START,
    VDAGENTD_FILE_XFER_STATUS,
    VDAGENTD_FILE_XFER_DATA,
    VDAGENTD_CLIENT_DISCONNECTED,

    VDAGENTD_LAST
};

enum {
    OWNER_NONE,
    OWNER_GUEST,
    OWNER_CLIENT
};

typedef struct _VDAgentdHeader {
    guint32 type;
    guint32 arg1;
    guint32 arg2;
    guint32 size;
} VDAgentdHeader;

typedef struct _SpiceVDAgent {
    GObject parent;

    GCancellable *cancellable;
    GSocket *socket;
    GSocketConnectable *connectable;
    GIOStream *connection;
    GQueue *outq;
    gboolean writing;
    gsize pos;

    VDAgentdHeader header;
    gpointer data;

    int clipboard_owner[G_MAXUINT8];
    struct {
        GMainLoop *loop;
        GtkSelectionData *selection_data;
        guint info;
        gint selection;
    } clipboard_get;
} SpiceVDAgent;

typedef struct _SpiceVDAgentClass {
    GObjectClass parent;
} SpiceVDAgentClass;

GType spice_vdagent_get_type(void);

void spice_vdagent_write(SpiceVDAgent *agent,
                         guint32 type, guint32 arg1, guint32 arg2,
                         gconstpointer data, guint32 size);

G_END_DECLS

#endif /* VDAGENT_H_ */
