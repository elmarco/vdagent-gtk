NULL =
SUBDIRS = data src

bin_PROGRAMS = src/spice-vdagent
sbin_PROGRAMS = src/spice-vdagentd

src_spice_vdagent_CFLAGS =						\
	$(SPICE_CFLAGS) $(GLIB2_CFLAGS) $(GIO_CFLAGS) $(GTK_CFLAGS)	\
	-I$(srcdir)/src -DG_LOG_DOMAIN=\"vdagent\" 			\
	$(NULL)
src_spice_vdagent_LDADD =					\
	$(SPICE_LIBS) $(GLIB2_LIBS) $(GIO_LIBS) $(GTK_LIBS)	\
	$(NULL)
src_spice_vdagent_SOURCES =			\
	$(common_sources)			\
	src/vdagent/vdagent-clipboard.c		\
	src/vdagent/vdagent-clipboard.h		\
	src/vdagent/vdagent.c			\
	src/vdagent/vdagent.h			\
	src/vdagent/utils.c			\
	src/vdagent/utils.h			\
	$(NULL)

src_spice_vdagentd_CFLAGS =				\
	$(DBUS_CFLAGS) $(LIBSYSTEMD_LOGIN_CFLAGS)	\
	$(SPICE_CFLAGS) $(GLIB2_CFLAGS) $(PIE_CFLAGS)	\
	-I$(srcdir)/src					\
	$(NULL)
src_spice_vdagentd_LDADD =				\
	$(DBUS_LIBS) $(LIBSYSTEMD_LOGIN_LIBS)		\
	$(SPICE_LIBS) $(GLIB2_LIBS) $(PIE_LDFLAGS)	\
	$(NULL)
src_spice_vdagentd_SOURCES =			\
	src/vdagentd/udscs.c			\
	src/vdagentd/udscs.h			\
	src/vdagentd/vdagentd-proto-strings.h	\
	src/vdagentd/vdagentd-proto.h		\
	src/vdagentd/session-info.h		\
	src/vdagentd/vdagent-virtio-port.c	\
	src/vdagentd/vdagent-virtio-port.h	\
	src/vdagentd/vdagentd-uinput.c		\
	src/vdagentd/vdagentd-uinput.h		\
	src/vdagentd/vdagentd.c			\
	$(NULL)

if HAVE_CONSOLE_KIT
src_spice_vdagentd_SOURCES += src/vdagentd/console-kit.c
else
if HAVE_LIBSYSTEMD_LOGIN
src_spice_vdagentd_SOURCES += src/vdagentd/systemd-login.c
else
src_spice_vdagentd_SOURCES += src/vdagentd/dummy-session-info.c
endif
endif

xdgautostartdir = $(sysconfdir)/xdg/autostart
xdgautostart_DATA = $(top_srcdir)/data/spice-vdagent.desktop

gdmautostartdir = $(datadir)/gdm/greeter/autostart
gdmautostart_DATA = $(top_srcdir)/data/spice-vdagent.desktop

gdmautostart2dir = $(datadir)/gdm/autostart/LoginWindow
gdmautostart2_DATA = $(top_srcdir)/data/spice-vdagent.desktop

install-data-local:
	$(mkdir_p) $(DESTDIR)$(localstatedir)/run/spice-vdagentd

rsyslogdir = $(sysconfdir)/rsyslog.d
rsyslog_DATA = $(top_srcdir)/data/rsyslog.d/spice-vdagentd.conf

if INIT_SCRIPT_RED_HAT
initdir = $(sysconfdir)/rc.d/init.d
init_SCRIPTS = $(top_srcdir)/data/spice-vdagentd
endif

if INIT_SCRIPT_SYSTEMD
systemdunitdir = $(SYSTEMDSYSTEMUNITDIR)
systemdunit_DATA = \
	$(top_srcdir)/data/spice-vdagentd.service \
	$(top_srcdir)/data/spice-vdagentd.target

udevrulesdir = /lib/udev/rules.d
udevrules_DATA = $(top_srcdir)/data/70-spice-vdagentd.rules

tmpfilesdir = $(prefix)/lib/tmpfiles.d
tmpfiles_DATA = $(top_srcdir)/data/tmpfiles.d/spice-vdagentd.conf
endif

manpagedir = $(mandir)/man1
manpage_DATA =					\
	data/spice-vdagent.1			\
	data/spice-vdagentd.1			\
	$(NULL)

EXTRA_DIST =					\
	data/70-spice-vdagentd.rules		\
	data/rsyslog.d/spice-vdagentd.conf	\
	data/spice-vdagent.desktop		\
	data/spice-vdagentd			\
	data/spice-vdagentd.service		\
	data/spice-vdagentd.target		\
	data/tmpfiles.d/spice-vdagentd.conf	\
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS =			\
	--with-init-script=redhat		\
	$(NULL)

MAINTAINERCLEANFILES =					\
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	$(NULL)

-include $(top_srcdir)/git.mk
