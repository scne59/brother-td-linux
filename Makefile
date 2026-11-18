# brother-td-linux - CUPS driver for Brother TD-4D label printers
#
# SPDX-License-Identifier: MIT

VERSION        := 1.0.0
PREFIX         ?= /usr/local
DESTDIR        ?=
CC             ?= cc
PKG_CONFIG     ?= pkg-config

CUPS_CFLAGS    := $(shell $(PKG_CONFIG) --cflags cups 2>/dev/null || cups-config --cflags 2>/dev/null)
CUPS_LIBS      := $(shell $(PKG_CONFIG) --libs cups 2>/dev/null || cups-config --libs 2>/dev/null)
CUPS_SERVERBIN ?= $(shell cups-config --serverbin 2>/dev/null || echo /usr/lib/cups)
CUPS_DATADIR   ?= $(shell cups-config --datadir 2>/dev/null || echo /usr/share/cups)
PAPPL_CFLAGS   ?= $(shell $(PKG_CONFIG) --cflags pappl 2>/dev/null)
PAPPL_LIBS     ?= $(shell $(PKG_CONFIG) --libs pappl 2>/dev/null)
ifeq ($(shell $(PKG_CONFIG) --atleast-version=1.4.0 pappl 2>/dev/null && echo yes),yes)
CPPFLAGS       += -DHAVE_PAPPL_CREATE_PRINTERS=1
endif
MANDIR         ?= $(PREFIX)/share/man
SYSTEMD_UNIT_DIR ?= $(PREFIX)/lib/systemd/system
APPDATA_DIR    ?= $(PREFIX)/share/brother-td-app
DOCDIR         ?= $(PREFIX)/share/doc/brother-td

CPPFLAGS       += -Iinclude -D_GNU_SOURCE -DTD_VERSION=\"$(VERSION)\"
CFLAGS         ?= -O2 -g
CFLAGS         += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
                  -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
LDFLAGS        +=
LDLIBS         += $(CUPS_LIBS) -lm

BUILD_DIR      := build
OBJ_DIR        := $(BUILD_DIR)/obj
BIN            := $(BUILD_DIR)/brother-td-filter
APP            := $(BUILD_DIR)/brother-td-app
SERVICE        := $(BUILD_DIR)/brother-td-app.service

CORE_SRCS      := \
	src/brother-td-filter.c \
	src/td_dither.c \
	src/td_image.c \
	src/td_job.c \
	src/td_media.c \
	src/td_model.c \
	src/td_models.c \
	src/td_options.c \
	src/td_packbits.c \
	src/td_protocol.c \
	src/td_raster.c \
	src/td_util.c \
	src/td_writer.c

CORE_OBJS      := $(CORE_SRCS:src/%.c=$(OBJ_DIR)/%.o)
LIB_SRCS       := $(filter-out src/brother-td-filter.c,$(CORE_SRCS))
LIB_OBJS       := $(LIB_SRCS:src/%.c=$(OBJ_DIR)/%.o)

TEST_BINS      := \
	$(BUILD_DIR)/test_packbits \
	$(BUILD_DIR)/test_image \
	$(BUILD_DIR)/test_media \
	$(BUILD_DIR)/test_dither \
	$(BUILD_DIR)/test_protocol

PPD_FILES      := $(wildcard ppd/*.ppd)
MAN_FILES      := man/brother-td.7 man/brother-td-filter.7
DOC_FILES      := README.md LICENSE NOTICE CHANGELOG.md AUTHORS docs/protocol.md

.PHONY: all all-pappl filter ppd pappl tests check install install-pappl uninstall clean dist regen help FORCE

all: filter ppd

all-pappl: all pappl

filter: $(BIN)

pappl: $(APP) $(SERVICE)

ppd: $(PPD_FILES)

$(BIN): $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(CORE_OBJS) $(LDLIBS)

$(APP): pappl/brother-td-app.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) $(PAPPL_CFLAGS) $(LDFLAGS) \
		-o $@ $< $(LIB_OBJS) $(PAPPL_LIBS) $(LDLIBS)

$(SERVICE): pappl/brother-td-app.service.in FORCE
	@mkdir -p $(dir $@)
	sed 's|@BINDIR@|$(PREFIX)/bin|g' $< > $@

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) -c -o $@ $<

# Unit tests ---------------------------------------------------------------
$(BUILD_DIR)/test_packbits: tests/test_packbits.c $(OBJ_DIR)/td_packbits.o $(OBJ_DIR)/td_util.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/test_image: tests/test_image.c $(OBJ_DIR)/td_image.o $(OBJ_DIR)/td_util.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/test_media: tests/test_media.c $(OBJ_DIR)/td_media.o $(OBJ_DIR)/td_model.o \
	$(OBJ_DIR)/td_models.o $(OBJ_DIR)/td_util.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/test_dither: tests/test_dither.c $(OBJ_DIR)/td_dither.o $(OBJ_DIR)/td_image.o \
	$(OBJ_DIR)/td_util.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/test_protocol: tests/test_protocol.c $(OBJ_DIR)/td_protocol.o \
	$(OBJ_DIR)/td_options.o $(OBJ_DIR)/td_media.o $(OBJ_DIR)/td_model.o \
	$(OBJ_DIR)/td_models.o $(OBJ_DIR)/td_image.o $(OBJ_DIR)/td_dither.o \
	$(OBJ_DIR)/td_packbits.o \
	$(OBJ_DIR)/td_writer.o $(OBJ_DIR)/td_util.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) $(LDFLAGS) -o $@ $^ $(CUPS_LIBS) -lm

$(BUILD_DIR)/make_raster: tests/make_raster.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CUPS_CFLAGS) $(LDFLAGS) -o $@ $< $(CUPS_LIBS)

tests: $(TEST_BINS) $(BUILD_DIR)/make_raster

check: tests $(BIN)
	@for test in $(TEST_BINS); do \
		echo "== $$test"; \
		$$test || exit 1; \
	done
	@echo "== PPD validation"
	@tests/check-ppd.sh
	@echo "== filter integration"
	@BROTHER_TD_FILTER=$(BIN) BROTHER_TD_RASTER=$(BUILD_DIR)/make_raster \
		python3 tests/integration.py
	@echo "== real gstoraster chain"
	@BROTHER_TD_FILTER=$(BIN) BROTHER_TD_RASTER=$(BUILD_DIR)/make_raster \
		python3 tests/real-raster.py
	@echo "== PAPPL printer application"
	@if [ -x $(APP) ]; then \
		BROTHER_TD_APP=$(APP) BROTHER_TD_RASTER=$(BUILD_DIR)/make_raster \
			python3 tests/pappl-integration.py || exit 1; \
		BROTHER_TD_APP=$(APP) \
			python3 tests/pappl-port.py || exit 1; \
	else \
		echo "PAPPL app not built; run 'make pappl' to test it"; \
	fi

# Installation -------------------------------------------------------------
install: all $(SERVICE)
	install -d $(DESTDIR)$(CUPS_SERVERBIN)/filter
	install -m 0755 $(BIN) $(DESTDIR)$(CUPS_SERVERBIN)/filter/brother-td-filter
	install -d $(DESTDIR)$(CUPS_DATADIR)/model
	install -m 0644 $(PPD_FILES) $(DESTDIR)$(CUPS_DATADIR)/model/
	install -d $(DESTDIR)$(MANDIR)/man7
	install -m 0644 $(MAN_FILES) $(DESTDIR)$(MANDIR)/man7/
	install -d $(DESTDIR)$(DOCDIR)
	install -m 0644 $(DOC_FILES) $(DESTDIR)$(DOCDIR)/
	@if [ -x $(APP) ]; then \
		install -d $(DESTDIR)$(PREFIX)/bin; \
		install -m 0755 $(APP) $(DESTDIR)$(PREFIX)/bin/brother-td-app; \
		install -d $(DESTDIR)$(MANDIR)/man1; \
		install -m 0644 man/brother-td-app.1 $(DESTDIR)$(MANDIR)/man1/; \
		install -D -m 0644 $(SERVICE) \
			$(DESTDIR)$(SYSTEMD_UNIT_DIR)/brother-td-app.service; \
		install -D -m 0644 pappl/brother-td.css \
			$(DESTDIR)$(APPDATA_DIR)/style.css; \
	fi

install-pappl: pappl
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(APP) $(DESTDIR)$(PREFIX)/bin/brother-td-app
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 0644 man/brother-td-app.1 $(DESTDIR)$(MANDIR)/man1/
	install -D -m 0644 $(SERVICE) \
		$(DESTDIR)$(SYSTEMD_UNIT_DIR)/brother-td-app.service
	install -D -m 0644 pappl/brother-td.css $(DESTDIR)$(APPDATA_DIR)/style.css

uninstall:
	-rm -f $(DESTDIR)$(PREFIX)/bin/brother-td-app
	-rm -f $(DESTDIR)$(MANDIR)/man1/brother-td-app.1
	-rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/brother-td-app.service
	-rm -f $(DESTDIR)$(APPDATA_DIR)/style.css
	-rm -f $(DESTDIR)$(CUPS_SERVERBIN)/filter/brother-td-filter
	-rm -f $(addprefix $(DESTDIR)$(CUPS_DATADIR)/model/,$(notdir $(PPD_FILES)))
	-rm -f $(addprefix $(DESTDIR)$(MANDIR)/man7/,$(notdir $(MAN_FILES)))
	-rm -rf $(DESTDIR)$(DOCDIR)

regen:
	./tools/generate.py

clean:
	rm -rf $(BUILD_DIR)

dist:
	@rm -rf /tmp/brother-td-linux-$(VERSION)
	@mkdir -p /tmp/brother-td-linux-$(VERSION)
	@tar -cf - --exclude=.git --exclude=build --exclude=__pycache__ --exclude='*.pyc' \
		--exclude=debian/.debhelper --exclude=debian/tmp \
		--exclude=debian/printer-driver-brother-td --exclude=debian/files \
		--exclude='debian/*.substvars' . | \
		tar -xf - -C /tmp/brother-td-linux-$(VERSION)
	@tar -czf brother-td-linux-$(VERSION).tar.gz -C /tmp brother-td-linux-$(VERSION)
	@rm -rf /tmp/brother-td-linux-$(VERSION)
	@echo "created brother-td-linux-$(VERSION).tar.gz"

FORCE:

help:
	@echo "Targets:"
	@echo "  all       build the filter and PPDs (default)"
	@echo "  pappl     build the PAPPL/IPP Everywhere printer application"
	@echo "  check     run unit, PPD, and end-to-end tests"
	@echo "  install   install into CUPS (DESTDIR/PREFIX supported)"
	@echo "  uninstall remove installed files"
	@echo "  regen     regenerate PPDs and src/td_models.c from models.json"
	@echo "  clean     remove build products"
	@echo "  dist      create a source tarball"
