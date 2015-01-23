ETC_CPPFLAGS = -I. \
-I$(top_builddir) \
-I$(top_builddir)/src/bin \
-I$(top_srcdir) \
-I$(top_srcdir)/src/bin \
@e_cflags@ \
@cf_cflags@ \
@VALGRIND_CFLAGS@ \
-DPACKAGE_BIN_DIR=\"@PACKAGE_BIN_DIR@\" \
-DPACKAGE_LIB_DIR=\"@PACKAGE_LIB_DIR@\" \
-DPACKAGE_DATA_DIR=\"@PACKAGE_DATA_DIR@\" \
-DLOCALE_DIR=\"@LOCALE_DIR@\" \
-DPACKAGE_SYSCONF_DIR=\"@PACKAGE_SYSCONF_DIR@\"

ETC_LIBS = @e_libs@

etc_bindir = $(libdir)/enlightenment/utils
etc_bin_PROGRAMS = \
src/TC/enlightenment_tc

src_TC_enlightenment_tc_SOURCES = \
src/TC/e_tc_main.c \
src/TC/e_tc_main.h \
src/TC/test_case_basic.c \
src/TC/test_case_basic.h \
src/TC/test_case_basic_stack.c \
src/TC/test_case_easy.c

src_TC_enlightenment_tc_LDADD = @e_libs@ $(ETC_LIBS)
src_TC_enlightenment_tc_CPPFLAGS = $(ETC_CPPFLAGS)
