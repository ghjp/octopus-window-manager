EXTRA_DIST = octopus.1.in
man_MANS = octopus.1

edit = sed \
       -e 's,@PACKAGE_VERSION\@,$(PACKAGE_VERSION),g' \
       -e 's,@pkgdatadir\@,$(pkgdatadir),g'

octopus.1: octopus.1.in
	$(edit) $< >$@

MOSTLYCLEANFILES = $(man_MANS)
