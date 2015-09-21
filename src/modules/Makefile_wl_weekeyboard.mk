DEFAULT_FILES = \
   src/modules/wl_weekeyboard/themes/default/default.edc \
   src/modules/wl_weekeyboard/themes/default/ignorekeys.txt \
   src/modules/wl_weekeyboard/themes/default/fonts/SourceSansPro-Regular.ttf \
   src/modules/wl_weekeyboard/themes/default/fonts/SourceSansPro-Semibold.ttf \
   src/modules/wl_weekeyboard/themes/default/images/key-hint-bg.png \
   src/modules/wl_weekeyboard/themes/default/images/icon-language.png \
   src/modules/wl_weekeyboard/themes/default/images/icon-backspace.png \
   src/modules/wl_weekeyboard/themes/default/images/key-special.png \
   src/modules/wl_weekeyboard/themes/default/images/icon-space.png \
   src/modules/wl_weekeyboard/themes/default/images/key-hint.png \
   src/modules/wl_weekeyboard/themes/default/images/icon-shift.png \
   src/modules/wl_weekeyboard/themes/default/images/icon-shift-active.png \
   src/modules/wl_weekeyboard/themes/default/images/icon-enter.png \
   src/modules/wl_weekeyboard/themes/default/images/key-special-pressed.png \
   src/modules/wl_weekeyboard/themes/default/images/key-default.png \
   src/modules/wl_weekeyboard/themes/default/images/key-default-pressed.png

EXTRA_DIST += \
   $(DEFAULT_FILES) \
   src/modules/wl_weekeyboard/themes/default/default_600.edc \
   src/modules/wl_weekeyboard/themes/default/default_720.edj \
   src/modules/wl_weekeyboard/themes/default/default_1080.edj

if USE_MODULE_WL_WEEKEYBOARD
wl_weekeyboarddir = $(MDIR)/wl_weekeyboard
wl_weekeyboard_DATA = \
   src/modules/wl_weekeyboard/themes/default/default_600.edj \
   src/modules/wl_weekeyboard/themes/default/default_720.edj \
   src/modules/wl_weekeyboard/themes/default/default_1080.edj

CLEANFILES += \
   src/modules/wl_weekeyboard/themes/default/default_600.edj \
   src/modules/wl_weekeyboard/themes/default/default_720.edj \
   src/modules/wl_weekeyboard/themes/default/default_1080.edj

src/modules/wl_weekeyboard/themes/default/default_600.edj: src/modules/wl_weekeyboard/themes/default/default_600.edc $(DEFAULT_FILES) Makefile
	$(EDJE_CC) $(EDJE_FLAGS) \
	    -dd $(top_srcdir)/src/modules/wl_weekeyboard/themes/default \
	    -id $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/images \
	    -fd $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/fonts \
	    $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/default_600.edc \
	    $(top_builddir)/src/modules/wl_weekeyboard/themes/default/default_600.edj

src/modules/wl_weekeyboard/themes/default/default_720.edj: src/modules/wl_weekeyboard/themes/default/default_720.edc $(DEFAULT_FILES) Makefile
	$(EDJE_CC) $(EDJE_FLAGS) \
	    -dd $(top_srcdir)/src/modules/wl_weekeyboard/themes/default \
	    -id $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/images \
	    -fd $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/fonts \
	    $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/default_720.edc \
	    $(top_builddir)/src/modules/wl_weekeyboard/themes/default/default_720.edj

src/modules/wl_weekeyboard/themes/default/default_1080.edj: src/modules/wl_weekeyboard/themes/default/default_1080.edc $(DEFAULT_FILES) Makefile
	$(EDJE_CC) $(EDJE_FLAGS) \
	    -dd $(top_srcdir)/src/modules/wl_weekeyboard/themes/default \
	    -id $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/images \
	    -fd $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/fonts \
	    $(top_srcdir)/src/modules/wl_weekeyboard/themes/default/default_1080.edc \
	    $(top_builddir)/src/modules/wl_weekeyboard/themes/default/default_1080.edj

wl_weekeyboardpkgdir = $(MDIR)/wl_weekeyboard/$(MODULE_ARCH)
wl_weekeyboardpkg_LTLIBRARIES = src/modules/wl_weekeyboard/module.la

src_modules_wl_weekeyboard_module_la_DEPENDENCIES = $(MDEPENDENCIES)
src_modules_wl_weekeyboard_module_la_CPPFLAGS  = \
   $(MOD_CPPFLAGS)               \
   @WL_WEEKEYBOARD_CFLAGS@       \
   @WAYLAND_CFLAGS@              \
   -DPKGDATADIR='"$pkgdatadir)"'
src_modules_wl_weekeyboard_module_la_LIBADD   = $(LIBS) @WL_WEEKEYBOARD_LIBS@ @WAYLAND_LIBS@
src_modules_wl_weekeyboard_module_la_LDFLAGS = $(MOD_LDFLAGS)
src_modules_wl_weekeyboard_module_la_SOURCES =                 \
   src/modules/wl_weekeyboard/e_mod_main.c                     \
   src/modules/wl_weekeyboard/wkb-log.h                        \
   src/modules/wl_weekeyboard/wkb-log.c                        \
   src/modules/wl_weekeyboard/input-method-protocol.c          \
   src/modules/wl_weekeyboard/input-method-client-protocol.h   \
   src/modules/wl_weekeyboard/text-protocol.c                  \
   src/modules/wl_weekeyboard/text-client-protocol.h

PHONIES += wl_weekeyboard install-wl_weekeyboard
wl_weekeyboard: $(wl_weekeyboardpkg_LTLIBRARIES) $(wl_weekeyboard_DATA)
install-wl_weekeyboard: install-wl_weekeyboardpkgLTLIBRARIES
endif
