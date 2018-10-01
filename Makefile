#
# Makefile for all UNIX style platforms including Cygwin
#
# (C) 2009-2017, Kees Verruijt, Harlingen, The Netherlands
#
# $Id:$
#

# s. https://www.gnu.org/prep/standards/html_node/Directory-Variables.html#Directory-Variables
DESTDIR ?= ""
PREFIX ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR=$(EXEC_PREFIX)/bin
SYSCONFDIR=$(PREFIX)/etc

PLATFORM=$(shell uname | tr '[A-Z]' '[a-z]')-$(shell uname -m)
OS=$(shell uname -o 2>&1)
SUBDIRS= actisense-serial analyzer n2kd nmea0183 ip group-function candump2analyzer socketcan-writer

MKDIR = mkdir -p

CONFDIR=$(SYSCONFDIR)/default

ROOT_UID=0
ROOT_GID=0
ROOT_MOD=0644

all:	bin
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir; done
	$(MAKE) -C analyzer json

bin:	rel/$(PLATFORM)

rel/$(PLATFORM):
	$(MKDIR) rel/$(PLATFORM)

clean:
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done
	
install: rel/$(PLATFORM)/analyzer $(DESTDIR)$(BINDIR) $(DESTDIR)$(CONFDIR)
	for i in rel/$(PLATFORM)/* util/* */*_monitor; do f=`basename $$i`; echo $$f; rm -f $(DESTDIR)$(BINDIR)/$$f; cp $$i $(DESTDIR)$(BINDIR); done
	for i in config/*; do install -g $(ROOT_GID) -o $(ROOT_UID) -m $(ROOT_MOD) $$i $(DESTDIR)$(CONFDIR); done
	-killall -9 actisense-serial n2kd socketcan-writer || echo 'No running processes killed'

zip:
	(cd rel; zip -r ../packetlogger_`date +%Y%m%d`.zip *)
	./rel/$(PLATFORM)/analyzer -explain > packetlogger_`date +%Y%m%d`_explain.txt
	./rel/$(PLATFORM)/analyzer -explain-xml > packetlogger_`date +%Y%m%d`_explain.xml

.PHONY : $(SUBDIRS) clean install zip bin

$(DESTDIR)$(BINDIR):
	$(MKDIR) $(DESTDIR)$(BINDIR)

$(DESTDIR)$(CONFDIR):
	$(MKDIR) $(DESTDIR)$(CONFDIR)
