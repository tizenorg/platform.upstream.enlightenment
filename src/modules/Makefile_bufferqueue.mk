EXTRA_DIST += \
src/modules/bufferqueue/bq_mgr_protocol.h \
src/modules/bufferqueue/bq_mgr_protocol.c

if USE_MODULE_BUFFERQUEUE
bufferqueuedir = $(MDIR)/bufferqueue

bufferqueuepkgdir = $(MDIR)/bufferqueue/$(MODULE_ARCH)
bufferqueuepkg_LTLIBRARIES = src/modules/bufferqueue/module.la

src_modules_bufferqueue_module_la_DEPENDENCIES = $(MDEPENDENCIES)
src_modules_bufferqueue_module_la_CPPFLAGS  = $(MOD_CPPFLAGS) @BUFFERQUEUE_CFLAGS@ @WAYLAND_CFLAGS@ -DNEED_WL
src_modules_bufferqueue_module_la_LIBADD   = $(LIBS) @BUFFERQUEUE_LIBS@ @WAYLAND_LIBS@
src_modules_bufferqueue_module_la_LDFLAGS = $(MOD_LDFLAGS)

src_modules_bufferqueue_module_la_SOURCES = src/modules/bufferqueue/e_mod_main.c \
                                            src/modules/bufferqueue/e_mod_main.h \
                                            src/modules/bufferqueue/bq_mgr_protocol.h \
                                            src/modules/bufferqueue/bq_mgr_protocol.c

PHONIES += bufferqueue install-bufferqueue
bufferqueue: $(bufferqueuepkg_LTLIBRARIES) $(bufferqueue_DATA)
install-bufferqueue: install-bufferqueuepkgLTLIBRARIES
endif
