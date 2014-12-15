EET_EET = @eet_eet@

configfilesdir = $(datadir)/enlightenment/data/config

SUFFIXES = .cfg

.src.cfg:
	$(MKDIR_P) $(@D)
	$(EET_EET) -e \
	$(top_builddir)/$@ config \
	$< 1

include config/default/Makefile.mk
include config/tiling/Makefile.mk
include config/standard/Makefile.mk
include config/mobile/Makefile.mk
