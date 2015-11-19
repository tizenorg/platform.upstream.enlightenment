e_comp_gesturespkgdir = $(MDIR)/e_comp_gestures/$(MODULE_ARCH)
e_comp_gesturespkg_LTLIBRARIES = src/modules/e_comp_gestures/module.la

src_modules_e_comp_gestures_module_la_DEPENDENCIES = $(MDEPENDENCIES)
src_modules_e_comp_gestures_module_la_CPPFLAGS  = $(MOD_CPPFLAGS)
src_modules_e_comp_gestures_module_la_LIBADD   = $(LIBS)
src_modules_e_comp_gestures_module_la_LDFLAGS = $(MOD_LDFLAGS)

src_modules_e_comp_gestures_module_la_SOURCES = \
  src/modules/e_comp_gestures/e_mod_main.c

PHONIES += e_comp_gestures install-e_comp_gestures
e_comp_gestures: $(e_comp_gesturespkg_LTLIBRARIES) $(e_comp_gestures_DATA)
install-e_comp_gestures: install-e_comp_gesturesDATA install-e_comp_gesturespkgLTLIBRARIES

