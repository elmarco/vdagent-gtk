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
    if (selection == VD_AGENT_CLIPBOARD_SELECTION_DND)
        return gdk_atom_intern("XdndSelection", FALSE);

    g_return_val_if_reached(GDK_NONE);
}

static guint8
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
    if (selection == gdk_atom_intern("XdndSelection", FALSE))
        return VD_AGENT_CLIPBOARD_SELECTION_DND;

    g_return_val_if_reached(0);
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

    gint len = 0;
    guint8 selection;
    int max_clipboard = -1;

    selection = get_selection_from_clipboard(clipboard);

    /* FIXME: get max_clipboard from agentd */
    len = gtk_selection_data_get_length(selection_data);
    if (len == 0 || (max_clipboard != -1 && len > max_clipboard)) {
        g_warning("discarded clipboard of size %d (max: %d)", len, max_clipboard);
        return;
    } else if (len == -1) {
        g_debug("empty clipboard");
        len = 0;
    } else {
    }

    gchar *target = gdk_atom_name(gtk_selection_data_get_target(selection_data));
    gchar *data = g_malloc(len);
    memcpy(data, gtk_selection_data_get_data(selection_data), len);

    spice_vdagent_write_header(agent, VDAGENTD_CLIPBOARD_DATA, selection, 0,
                               len + strlen(target) + 1);
    spice_vdagent_write(agent, target, strlen(target) + 1, g_free);

    spice_vdagent_write(agent, data, len, g_free);
}


void
vdagent_clipboard_request(SpiceVDAgent *agent, guint8 selection,
                          const gchar *type)
{
    GtkClipboard *clipboard;
    GdkAtom selat, target;

    g_debug("client clipboard request");
    g_return_if_fail(SPICE_IS_VDAGENT(agent));

    selat = selection_atom(selection);
    target = gdk_atom_intern(type, FALSE);
    if (selat == GDK_NONE || target == GDK_NONE) {
        g_debug("unknown selection:%d or target:%s", selection, type);
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
    spice_vdagent_write_header(agent, VDAGENTD_CLIPBOARD_DATA, selection, 0, 0);
}

static void
clipboard_get_cb(GtkClipboard *clipboard,
                 GtkSelectionData *selection_data,
                 guint info, gpointer user_data)
{
    SpiceVDAgent *agent = user_data;
    guint8 selection = get_selection_from_clipboard(clipboard);

    g_debug("clipboard get");
    g_return_if_fail(agent->clipboard_get.loop == NULL);

    gchar *target = gdk_atom_name(gtk_selection_data_get_target(selection_data));
    spice_vdagent_write_msg(agent, VDAGENTD_CLIPBOARD_REQUEST,
                            selection, 0, target, strlen(target) + 1, g_free);

    agent->clipboard_get.selection_data = selection_data;
    agent->clipboard_get.loop = g_main_loop_new(NULL, FALSE);
    agent->clipboard_get.selection = selection;

    g_main_loop_run(agent->clipboard_get.loop);

    g_main_loop_unref(agent->clipboard_get.loop);
    agent->clipboard_get.loop = NULL;
}

void
vdagent_clipboard_data(SpiceVDAgent *agent, guint8 selection,
                       const gchar *type,
                       gpointer data, gsize size)
{
    g_return_if_fail(SPICE_IS_VDAGENT(agent));
    g_return_if_fail(agent->clipboard_get.loop != NULL);

    g_debug("client clipboard data");

    gtk_selection_data_set(agent->clipboard_get.selection_data,
                           gdk_atom_intern(type, FALSE),
                           8, data, size);

    g_main_loop_quit(agent->clipboard_get.loop);
}

void
vdagent_clipboard_grab(SpiceVDAgent *agent, guint8 selection,
                       const GStrv types)
{
    GtkTargetEntry *targets;
    GtkClipboard* clipboard;
    GdkAtom selat;
    int n, i, j;

    g_debug("client clipboard grab");
    g_return_if_fail(SPICE_IS_VDAGENT(agent));
    g_return_if_fail(types);

    g_debug("selection grab %u", selection);
    for (i = 0; types[i]; i++)
        g_debug("%s", types[i]);

    selat = selection_atom(selection);
    if (selat == GDK_NONE) {
        g_debug("unknown selection:%d", selection);
        return;
    }

    clipboard = clipboard_get(selat);
    g_return_if_fail(clipboard != NULL);

    n = g_strv_length(types);
    targets = g_new0(GtkTargetEntry, n);
    for (i = 0, j = 0; i < n; i++) {
        if (!strcmp(types[i], "TARGETS"))
            continue;

        targets[j].target = types[i];
        targets[j].info = j;
        j++;
    }

    if (!gtk_clipboard_set_with_owner(clipboard, targets, j,
                                      clipboard_get_cb, NULL, G_OBJECT(agent))) {
        g_warning("clipboard grab failed");
    } else {
        agent->clipboard_owner[selection] = OWNER_CLIENT;
    }

    g_free(targets);
}

void
vdagent_clipboard_release(SpiceVDAgent *agent, guint8 selection)
{
    GtkClipboard *clipboard;
    GdkAtom selat;

    g_return_if_fail(SPICE_IS_VDAGENT(agent));
    g_debug("client clipboard release");

    selat = selection_atom(selection);
    if (selat == GDK_NONE) {
        g_debug("unknown selection:%d", selection);
        return;
    }

    if (agent->clipboard_owner[selection] == OWNER_GUEST)
        return;

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

    char *name;
    int a;
    guint8 selection;
    gsize size = 2;
    GStrv targets = g_new0(gchar*, n_atoms + 1);

    selection = get_selection_from_clipboard(clipboard);

    for (a = 0; a < n_atoms; a++) {
        name = gdk_atom_name(atoms[a]);
        g_debug(" \"%s\"", name);
        targets[a] = name;
        size += strlen(name) + 1;
    }

    spice_vdagent_write_header(self, VDAGENTD_CLIPBOARD_GRAB,
                               selection, 0, size);

    for (a = 0; a < n_atoms; a++)
        spice_vdagent_write(self, targets[a], strlen(targets[a]) + 1, g_free);

    spice_vdagent_write(self, "\0\0", 2, NULL);

    self->clipboard_owner[selection] = OWNER_GUEST;
    g_free(targets);
}

static void
owner_change(GtkClipboard        *clipboard,
             GdkEventOwnerChange *event,
             gpointer            user_data)
{
    SpiceVDAgent *self = user_data;
    guint8 selection = get_selection_from_clipboard(clipboard);

    g_return_if_fail(SPICE_IS_VDAGENT(self));

    if (self->clipboard_owner[selection] == OWNER_GUEST) {
        g_debug("sending release");
        spice_vdagent_write_header(self, VDAGENTD_CLIPBOARD_RELEASE, selection, 0, 0);
        self->clipboard_owner[selection] = OWNER_NONE;
    }

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
    g_signal_connect(G_OBJECT(clipboard_get(gdk_atom_intern("XdndSelection", FALSE))),
                     "owner-change",
                     G_CALLBACK(owner_change), self);
}
