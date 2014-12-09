## Process this file with automake to produce Makefile.in

SUBDIRS = data

NULL =

INCLUDES = \
	-I$(top_builddir) -I$(top_srcdir)	 		\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DUDISKS_COMPILATION					\
	$(POLKIT_GOBJECT_1_CFLAGS)				\
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(GUDEV_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(NULL)

$(dbus_built_sources) : Makefile.am data/org.freedesktop.UDisks2.dummy.xml
	gdbus-codegen									\
		--interface-prefix org.freedesktop.UDisks2.				\
		--c-namespace Dummy							\
		--generate-c-code dummy-generated					\
		data/org.freedesktop.UDisks2.dummy.xml					\
		$(NULL)


module_flags = -export_dynamic -avoid-version -module -no-undefined -export-symbols-regex '^udisks_module_.*|g_io_module_load|g_io_module_unload|g_io_module_query'

dbus_built_sources =									\
	dummy-generated.h			dummy-generated.c			\
	$(NULL)

BUILT_SOURCES =										\
	$(dbus_built_sources)								\
	$(NULL)

libudisks2_dummydir=$(libdir)/udisks2/modules
libudisks2_dummy_LTLIBRARIES=libudisks2_dummy.la

libudisks2_dummyincludedir=$(includedir)/udisks2/udisks

libudisks2_dummyinclude_HEADERS=							\
	$(top_srcdir)/modules/udisksmoduleiface.h					\
	$(top_srcdir)/modules/udisksmoduleifacetypes.h					\
	$(top_srcdir)/modules/udisksmoduleobject.h					\
	$(NULL)

libudisks2_dummy_la_SOURCES =								\
	$(BUILT_SOURCES)								\
	$(top_srcdir)/modules/udisksmoduleiface.h					\
	$(top_srcdir)/modules/udisksmoduleifacetypes.h					\
	$(top_srcdir)/modules/udisksmoduleobject.h					\
	$(top_srcdir)/modules/udisksmoduleobject.c					\
	dummytypes.h									\
	dummymoduleiface.c								\
	dummylinuxblock.h			dummylinuxblock.c			\
	dummylinuxdrive.h			dummylinuxdrive.c			\
	dummyloopobject.h			dummyloopobject.c			\
	dummylinuxloop.h			dummylinuxloop.c			\
	dummylinuxmanager.h			dummylinuxmanager.c			\
	$(NULL)

libudisks2_dummy_la_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"libudisks2-dummy\"		\
	$(NULL)

libudisks2_dummy_la_CFLAGS = 				\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(POLKIT_GOBJECT_1_CFLAGS)			\
	$(NULL)

libudisks2_dummy_la_LDFLAGS = $(module_flags)

libudisks2_dummy_la_LIBADD = 				\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(GUDEV_LIBS)					\
	$(POLKIT_GOBJECT_1_LIBS)			\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

CLEANFILES = dummy-generated.[ch]

EXTRA_DIST = 						\
	$(NULL)

dist-hook :
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local :
	rm -f *~ $(BUILT_SOURCES)