EXTRA_DIST = config.xml.in xkeyb.xml.in
pkgdata_DATA = config.xml xkeyb.xml
edit = sed \
       -e 's,@PACKAGE_VERSION\@,$(PACKAGE_VERSION),g'

config.xml: config.xml.in
	$(edit) $< >$@

xkeyb.xml: xkeyb.xml.in
	$(edit) $< >$@
