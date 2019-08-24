#
# (C) 2009-2015, Kees Verruijt, Harlingen, The Netherlands.
#
# This file is part of CANboat.
#
# CANboat is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CANboat is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CANboat.  If not, see <http://www.gnu.org/licenses/>.
#

PLATFORM=$(shell uname | tr '[A-Z]' '[a-z]')-$(shell uname -m)
TARGETDIR=../rel/$(PLATFORM)
ANALYZER=$(TARGETDIR)/analyzer
ANALYZER_J1939=$(TARGETDIR)/analyzer-j1939
TARGETS=$(ANALYZER) $(ANALYZER_J1939)
XMLFILE=pgns.xml
JSONFILE=pgns.json
XMLFILE_J1939=pgns-j1939.xml
JSONFILE_J1939=pgns-j1939.json

all: $(TARGETS)

$(ANALYZER): analyzer.c pgn.c analyzer.h pgn.h ../common/common.c ../common/common.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $(ANALYZER) -I../common pgn.c analyzer.c ../common/common.c $(LDLIBS$(LDLIBS-$(@)))

json: $(ANALYZER) pgns2json.xslt
	$(ANALYZER) -explain-xml >$(XMLFILE) && xsltproc pgns2json.xslt $(XMLFILE) >$(JSONFILE)


$(ANALYZER_J1939): analyzer.c pgn.c analyzer.h pgn.h j1939.h ../common/common.c ../common/common.h Makefile
	$(CC) -DJ1939 $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $(ANALYZER_J1939) -I../common pgn.c analyzer.c ../common/common.c $(LDLIBS$(LDLIBS-$(@)))

json-j1939: $(ANALYZER) pgns2json.xslt
	$(ANALYZER_J1939) -explain-xml >$(XMLFILE_J1939) && xsltproc pgns2json.xslt $(XMLFILE_J1939) >$(JSONFILE_J1939)


clean:
	-rm -f $(TARGETS) *.elf *.gdb
	-rm -f $(XMLFILE)
	-rm -f $(JSONFILE)

