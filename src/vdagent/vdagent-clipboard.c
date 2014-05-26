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

#include <string.h>

#include "vdagent-clipboard.h"

static const struct {
    uint32_t    vdagent;
    const char  *xatom;
} atom2agent[] = {
    {
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "UTF8_STRING",
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "text/plain;charset=utf-8"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "STRING"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "TEXT"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "text/plain"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_PNG,
        .xatom   = "image/png"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/bmp"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/x-bmp"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/x-MS-bmp"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/x-win-bitmap"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_TIFF,
        .xatom   = "image/tiff"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_JPG,
        .xatom   = "image/jpeg"
    }
};

static GtkClipboard*
clipboard_get(GdkAtom selection)
{
    GtkClipboard* cb = gtk_clipboard_get(selection);

    /* FIXME: https://bugzilla.gnome.org/show_bug.cgi?id=730821 */
    g_object_set_data(G_OBJECT(cb), "selection", selection);

    return cb;
}

static GdkAtom
selection_atom(guint8 selection)
{
    if (selection == VD_AGENT_CLIPBOARD_SELECTION_PRIMARY)
        return GDK_SELECTION_PRIMARY;
    if (selection == VD_AGENT_CLIPBOARD_SELECTION_SECONDARY)
        return GDK_SELECTION_SECONDARY;
    if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD)
        return GDK_SELECTION_CLIPBOARD;

    g_return_val_if_reached(GDK_NONE);
}

static gint
get_selection_from_clipboard(GtkClipboard* cb)
{
    GdkAtom selection;

    selection = g_object_get_data(G_OBJECT(cb), "selection");
    if (selection == GDK_SELECTION_PRIMARY)
        return VD_AGENT_CLIPBOARD_SELECTION_PRIMARY;
    if (selection == GDK_SELECTION_SECONDARY)
        return VD_AGENT_CLIPBOARD_SELECTION_SECONDARY;
    if (selection == GDK_SELECTION_CLIPBOARD)
        return VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD;

    g_return_val_if_reached(-1);
}


static GdkAtom
target_atom(guint32 type)
{
    GdkAtom atom;
    int m;

    for (m = 0; m < G_N_ELEMENTS(atom2agent); m++) {
        if (atom2agent[m].vdagent == type)
            break;
    }

    g_return_val_if_fail(m < G_N_ELEMENTS(atom2agent), GDK_NONE);

    atom = gdk_atom_intern_static_string(atom2agent[m].xatom);
    return atom;
}

typedef struct _WeakRef {
    GObject *object;
} WeakRef;

static void weak_notify_cb(WeakRef *weakref, GObject *object)
{
    weakref->object = NULL;
}

static WeakRef* weak_ref(GObject *gobject)
{
    WeakRef *weakref = g_new(WeakRef, 1);

    g_object_weak_ref(gobject, (GWeakNotify)weak_notify_cb, weakref);
    weakref->object = gobject;

    return weakref;
}

static void weak_unref(WeakRef* weakref)
{
    if (weakref->object)
        g_object_weak_unref(weakref->object, (GWeakNotify)weak_notify_cb, weakref);

    g_free(weakref);
}

static void
received_cb(GtkClipboard *clipboard,
            GtkSelectionData *selection_data,
            gpointer user_data)
{
    WeakRef *weakref = user_data;
    SpiceVDAgent *agent = SPICE_VDAGENT(weakref->object);
    weak_unref(weakref);

    if (agent == NULL)
        return;

    gint len = 0, m;
    guint32 type = VD_AGENT_CLIPBOARD_NONE;
    gchar* name;
    GdkAtom atom;
    int selection;
    int max_clipboard = -1;

    selection = get_selection_from_clipboard(clipboard);
    g_return_if_fail(selection != -1);

    /* FIXME: get max_clipboard from agentd */
    len = gtk_selection_data_get_length(selection_data);
    if (len == 0 || (max_clipboard != -1 && len > max_clipboard)) {
        g_warning("discarded clipboard of size %d (max: %d)", len, max_clipboard);
        return;
    } else if (len == -1) {
        g_debug("empty clipboard");
        len = 0;
    } else {
        atom = gtk_selection_data_get_data_type(selection_data);
        name = gdk_atom_name(atom);
        for (m = 0; m < G_N_ELEMENTS(atom2agent); m++) {
            if (!g_strcmp0(name, atom2agent[m].xatom))
                break;
        }

        if (m >= G_N_ELEMENTS(atom2agent)) {
            g_warning("clipboard_received for unsupported type: %s", name);
        } else {
            type = atom2agent[m].vdagent;
        }

        g_free(name);
    }

    spice_vdagent_write(agent, VDAGENTD_CLIPBOARD_DATA, selection, type,
                        gtk_selection_data_get_data(selection_data), len);
}


void
vdagent_clipboard_request(SpiceVDAgent *agent, guint8 selection, guint32 type)
{
    GtkClipboard *clipboard;
    GdkAtom selat, target;

    g_return_if_fail(SPICE_IS_VDAGENT(agent));

    selat = selection_atom(selection);
    target = target_atom(type);
    if (selat == GDK_NONE || target == GDK_NONE) {
        g_debug("unknown selection:%d or target:%d", selection, type);
        goto none;
    }

    if (agent->clipboard_owner[selection] != OWNER_GUEST) {
        g_debug("received clipboard req while not owning guest clipboard");
        goto none;
    }

    clipboard = clipboard_get(selat);

    gtk_clipboard_request_contents(clipboard, target,
                                   received_cb, weak_ref(G_OBJECT(agent)));
    return;

none:
    spice_vdagent_write(agent, VDAGENTD_CLIPBOARD_DATA,
                        selection, VD_AGENT_CLIPBOARD_NONE, NULL, 0);
}

static void
clipboard_get_cb(GtkClipboard *clipboard,
                 GtkSelectionData *selection_data,
                 guint info, gpointer user_data)
{
    SpiceVDAgent *agent = user_data;
    gint selection = get_selection_from_clipboard(clipboard);

    g_debug("clipboard get");
    g_return_if_fail(agent->clipboard_get.loop == NULL);

    g_return_if_fail(selection != -1);

    spice_vdagent_write(agent, VDAGENTD_CLIPBOARD_REQUEST,
                        selection, atom2agent[info].vdagent,
                        NULL, 0);

    agent->clipboard_get.selection_data = selection_data;
    agent->clipboard_get.info = info;
    agent->clipboard_get.loop = g_main_loop_new(NULL, FALSE);
    agent->clipboard_get.selection = selection;

    g_main_loop_run(agent->clipboard_get.loop);

    g_main_loop_unref(agent->clipboard_get.loop);
    agent->clipboard_get.loop = NULL;
}

void
vdagent_clipboard_data(SpiceVDAgent *agent, guint8 selection, guint32 types,
                       gpointer data, gsize size)
{
    g_return_if_fail(SPICE_IS_VDAGENT(agent));
    g_return_if_fail(agent->clipboard_get.loop != NULL);

    g_debug("clipboard data");

    gtk_selection_data_set(agent->clipboard_get.selection_data,
                           gdk_atom_intern_static_string(atom2agent[agent->clipboard_get.info].xatom),
                           8, data, size);

    g_main_loop_quit(agent->clipboard_get.loop);
}

void
vdagent_clipboard_grab(SpiceVDAgent *agent, guint8 selection,
                       guint32 *types, gsize ntypes)
{
    GtkTargetEntry targets[G_N_ELEMENTS(atom2agent)];
    gboolean target_selected[G_N_ELEMENTS(atom2agent)] = { FALSE, };
    gboolean found;
    GtkClipboard* clipboard;
    GdkAtom selat;
    int m, n, i;

    g_return_if_fail(SPICE_IS_VDAGENT(agent));

    selat = selection_atom(selection);
    if (selat == GDK_NONE) {
        g_debug("unknown selection:%d", selection);
        return;
    }

    clipboard = clipboard_get(selat);
    g_return_if_fail(clipboard != NULL);

    i = 0;
    for (n = 0; n < ntypes; ++n) {
        found = FALSE;
        for (m = 0; m < G_N_ELEMENTS(atom2agent); m++) {
            if (atom2agent[m].vdagent == types[n] && !target_selected[m]) {
                found = TRUE;
                g_return_if_fail(i < G_N_ELEMENTS(atom2agent));
                targets[i].target = (gchar*)atom2agent[m].xatom;
                targets[i].info = m;
                target_selected[m] = TRUE;
                i += 1;
            }
        }
        if (!found) {
            g_warning("couldn't find a matching type for: %d", types[n]);
        }
    }

    if (!gtk_clipboard_set_with_owner(clipboard, targets, i,
                                      clipboard_get_cb, NULL, G_OBJECT(agent))) {
        g_warning("clipboard grab failed");
        return;
    }

    agent->clipboard_owner[selection] = OWNER_CLIENT;
}

void
vdagent_clipboard_release(SpiceVDAgent *agent, guint8 selection)
{
    GtkClipboard *clipboard;
    GdkAtom selat;

    g_return_if_fail(SPICE_IS_VDAGENT(agent));

    selat = selection_atom(selection);
    if (selat == GDK_NONE) {
        g_debug("unknown selection:%d", selection);
        return;
    }

    if (agent->clipboard_owner[selection] != OWNER_GUEST) {
        g_debug("received clipboard release while not owning guest clipboard");
        return;
    }

    clipboard = clipboard_get(selat);
    gtk_clipboard_clear(clipboard);
    agent->clipboard_owner[selection] = OWNER_NONE;
}

void
vdagent_clipboard_release_all(SpiceVDAgent *agent)
{
    int i;

    g_return_if_fail(SPICE_IS_VDAGENT(agent));

    for (i = 0; i < G_N_ELEMENTS(agent->clipboard_owner); i++) {
        if (agent->clipboard_owner[i] == OWNER_CLIENT)
            vdagent_clipboard_release(agent, i);
    }
}

static void
got_targets(GtkClipboard *clipboard,
            GdkAtom *atoms,
            gint n_atoms,
            gpointer user_data)
{
    WeakRef *weakref = user_data;
    SpiceVDAgent *self = SPICE_VDAGENT(weakref->object);
    weak_unref(weakref);

    if (!self)
        return;

    guint32 types[G_N_ELEMENTS(atom2agent)];
    char *name;
    int a, m, t;
    int selection;

    selection = get_selection_from_clipboard(clipboard);
    g_return_if_fail(selection != -1);

    for (a = 0; a < n_atoms; a++) {
        name = gdk_atom_name(atoms[a]);
        g_debug(" \"%s\"", name);
        g_free(name);
    }

    memset(types, 0, sizeof(types));
    for (a = 0; a < n_atoms; a++) {
        name = gdk_atom_name(atoms[a]);
        for (m = 0; m < G_N_ELEMENTS(atom2agent); m++) {
            if (g_strcmp0(name, atom2agent[m].xatom)) {
                continue;
            }
            /* found match */
            for (t = 0; t < G_N_ELEMENTS(atom2agent); t++) {
                if (types[t] == atom2agent[m].vdagent) {
                    /* type already in list */
                    break;
                }
                if (types[t] == 0) {
                    /* add type to empty slot */
                    types[t] = atom2agent[m].vdagent;
                    break;
                }
            }
            break;
        }
        g_free(name);
    }
    for (t = 0; t < G_N_ELEMENTS(atom2agent); t++) {
        if (types[t] == 0) {
            break;
        }
    }

    if (t == 0)
        return;

    spice_vdagent_write(self,
                        VDAGENTD_CLIPBOARD_GRAB, selection, 0,
                        (guint8*)types, t * sizeof(guint32));
    self->clipboard_owner[selection] = OWNER_GUEST;
}

static void
owner_change(GtkClipboard        *clipboard,
             GdkEventOwnerChange *event,
             gpointer            user_data)
{
    SpiceVDAgent *self = user_data;
    gint selection = get_selection_from_clipboard(clipboard);

    g_return_if_fail(SPICE_IS_VDAGENT(self));

    if (self->clipboard_owner[selection] == OWNER_CLIENT)
        vdagent_clipboard_release(self, selection);

    switch (event->reason) {
    case GDK_OWNER_CHANGE_NEW_OWNER:
        if (gtk_clipboard_get_owner(clipboard) == G_OBJECT(self))
            break;

        gtk_clipboard_request_targets(clipboard, got_targets,
                                      weak_ref(G_OBJECT(self)));
        break;
    default:
        break;
    }
}

void
vdagent_clipboard_init(SpiceVDAgent *self)
{
    g_return_if_fail(SPICE_IS_VDAGENT(self));

    g_signal_connect(G_OBJECT(clipboard_get(GDK_SELECTION_CLIPBOARD)),
                     "owner-change",
                     G_CALLBACK(owner_change), self);
    g_signal_connect(G_OBJECT(clipboard_get(GDK_SELECTION_PRIMARY)),
                     "owner-change",
                     G_CALLBACK(owner_change), self);
}
