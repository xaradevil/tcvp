skindir = $(pkgdatadir)/skins
nobase_dist_skin_DATA = $(shell find $(srcdir) \( -name '*.png' -o -name '*.conf' \) ! -path '*/SCCS/*' | sed s,^$(srcdir).,,)
install_sh_DATA = echo "  INSTALL $$p" && true " >/dev/null " && $(install_sh) -c -m 644

.SILENT:
