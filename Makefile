# html2md - HTML to Markdown converter
#
# Targets:
#   make              build the library and the html2md tool (release)
#   make test         build and run every test suite
#   make debug        unoptimised build with debug symbols
#   make sanitize     run all tests under AddressSanitizer + UBSan
#   make fuzz         run the mutation fuzzer (FUZZ_ITERATIONS=N)
#   make roundtrip    CommonMark round-trip conformance (needs cmark-gfm; CMARK=path)
#   make examples     build examples/library_usage.c
#   make install      install to $(PREFIX) (DESTDIR supported)
#   make uninstall    remove installed files
#   make clean        remove build products
#
# Variables: CC, CFLAGS, LDFLAGS, PREFIX, DESTDIR, BUILD

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
LIBDIR     ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
MANDIR     ?= $(PREFIX)/share/man

CC      ?= cc
AR      ?= ar
BUILD   ?= build
CFLAGS  ?= -O2
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Wformat=2 -Wundef -Wvla -Wpointer-arith \
           -Wcast-qual -Wwrite-strings -Wno-unused-parameter
ALL_CFLAGS = $(WARNINGS) $(CFLAGS) -Iinclude -Isrc
PYTHON  ?= python3

LIB_SRCS = src/arena.c src/buf.c src/convert.c src/dom.c src/encoding.c \
           src/entities.c src/html2md.c src/parser.c src/selector.c \
           src/sink.c src/tags.c src/url.c src/utf8.c
CLI_SRCS = src/cli.c src/main.c
TEST_SRCS = tests/unit/test_main.c tests/unit/test_utils.c tests/unit/test_parser.c \
            tests/unit/test_selector.c tests/unit/test_url.c tests/unit/test_convert.c \
            tests/unit/test_cli.c

LIB_OBJS  = $(LIB_SRCS:src/%.c=$(BUILD)/obj/%.o)
CLI_OBJS  = $(CLI_SRCS:src/%.c=$(BUILD)/obj/%.o)
TEST_OBJS = $(TEST_SRCS:tests/unit/%.c=$(BUILD)/obj/tests/%.o)
HEADERS   = $(wildcard src/*.h) include/html2md.h

LIB  = $(BUILD)/libhtml2md.a
BIN  = $(BUILD)/html2md
UNIT = $(BUILD)/unit_tests
FUZZ = $(BUILD)/fuzz_html2md
EXAMPLE = $(BUILD)/library_usage
PC   = $(BUILD)/html2md.pc
VERSION = 1.0.0

.PHONY: all debug test unit fixtures cli-tests roundtrip sanitize fuzz examples install uninstall clean entities

all: $(BIN)

$(BUILD)/obj/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD)/obj/tests/%.o: tests/unit/%.c $(HEADERS) tests/unit/testlib.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -Itests/unit -c $< -o $@

$(LIB): $(LIB_OBJS)
	@rm -f $@
	$(AR) rcs $@ $(LIB_OBJS)

$(BIN): $(CLI_OBJS) $(LIB)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $(CLI_OBJS) $(LIB)

# The CLI parser is linked into the unit tests (main.c is not).
$(UNIT): $(TEST_OBJS) $(BUILD)/obj/cli.o $(LIB)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $(TEST_OBJS) $(BUILD)/obj/cli.o $(LIB)

$(EXAMPLE): examples/library_usage.c $(LIB)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ examples/library_usage.c $(LIB)

examples: $(EXAMPLE)

$(PC): html2md.pc.in
	@mkdir -p $(dir $@)
	sed -e 's|@CMAKE_INSTALL_LIBDIR@|lib|' -e 's|@CMAKE_INSTALL_INCLUDEDIR@|include|' \
	    -e 's|@PROJECT_VERSION@|$(VERSION)|' $< > $@

$(FUZZ): tests/fuzz/fuzz_html2md.c $(LIB)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -DH2M_STANDALONE_FUZZER -o $@ tests/fuzz/fuzz_html2md.c $(LIB)

debug:
	$(MAKE) BUILD=build-debug CFLAGS="-O0 -g3"

unit: $(UNIT)
	$(UNIT)

fixtures: $(BIN)
	sh tests/run_fixtures.sh $(BIN)

cli-tests: $(BIN)
	sh tests/run_cli_tests.sh $(BIN)

test: unit fixtures cli-tests

# Round-trip test against the CommonMark spec; needs cmark-gfm (CMARK=path).
roundtrip: $(BIN)
	$(PYTHON) tests/roundtrip.py --matrix --html2md $(BIN) $(if $(CMARK),--cmark $(CMARK))

sanitize:
	$(MAKE) BUILD=build-sanitize \
	    CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" \
	    LDFLAGS="-fsanitize=address,undefined" test

FUZZ_ITERATIONS ?= 20000
fuzz:
	$(MAKE) BUILD=build-sanitize \
	    CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" \
	    LDFLAGS="-fsanitize=address,undefined" build-sanitize/fuzz_html2md
	build-sanitize/fuzz_html2md $(FUZZ_ITERATIONS) tests/fixtures/*.html

# Regenerate the named character reference table from tools/entities.json.
entities:
	$(PYTHON) tools/gen_entities.py tools/entities.json > src/entities_data.h

install: $(BIN) $(LIB) $(PC)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)/pkgconfig $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(MANDIR)/man1
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/html2md
	install -m 644 $(LIB) $(DESTDIR)$(LIBDIR)/libhtml2md.a
	install -m 644 $(PC) $(DESTDIR)$(LIBDIR)/pkgconfig/html2md.pc
	install -m 644 include/html2md.h $(DESTDIR)$(INCLUDEDIR)/html2md.h
	install -m 644 docs/html2md.1 $(DESTDIR)$(MANDIR)/man1/html2md.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/html2md $(DESTDIR)$(LIBDIR)/libhtml2md.a \
	      $(DESTDIR)$(LIBDIR)/pkgconfig/html2md.pc $(DESTDIR)$(INCLUDEDIR)/html2md.h \
	      $(DESTDIR)$(MANDIR)/man1/html2md.1

clean:
	rm -rf build build-debug build-sanitize
