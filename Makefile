CC       ?= cc
# These flags are required. An environment CFLAGS must not drop them.
REQUIRED  = -std=c17 -Wall -Wextra -Wpedantic -Werror
CFLAGS   ?= -O2
DEFINES   = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS  ?=
UNAME_S  := $(shell uname -s)
PTY_LIBS =
ifeq ($(UNAME_S),Linux)
PTY_LIBS = -lutil
endif
VERSION  := $(shell cat VERSION)
OBJDIR   ?= src
OUT      ?= bettersh
SDK      := $(shell xcrun --show-sdk-path 2>/dev/null)

SRC = src/main.c src/arena.c src/error.c src/ast.c \
      src/lexer.c src/lex_word.c src/lex_param.c \
      src/parser.c src/parse_cmd.c \
      src/string.c src/value.c src/array.c src/map.c src/env.c src/process.c \
      src/eval_expr.c \
      src/builtins.c src/builtin_printf.c src/builtin_test.c src/builtin_cd.c \
      src/builtin_var.c src/builtin_proc.c src/builtin_cmd.c src/builtin_posix.c \
      src/builtin_fc.c src/builtin_limit.c src/builtin_edit.c src/editor_launch.c \
      src/eval.c src/eval_sh.c src/eval_flow.c src/expand.c src/arith_text.c src/redir.c src/hash.c \
      src/chunk.c src/mod_scan.c src/mod_expr.c src/mod_parse.c \
      src/compiler.c src/compile_expr.c src/compile_flow.c src/verify.c src/disasm.c \
      src/vm.c src/vm_ops.c src/vm_method.c src/vm_exec.c src/interp.c src/interp_expr.c \
      src/module.c src/host.c src/host_term.c src/host_fs.c src/host_pipe.c src/modern.c \
      src/shjob.c src/interact.c src/vi_line.c src/vi_draw.c src/vi_complete.c src/vi_opts.c src/vi_hist.c \
      src/btr_prompt.c \
      src/utf8.c src/suggest.c src/trace.c src/bcode.c
OBJ = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC))

PREFIX  ?= /usr/local
bindir   = $(PREFIX)/bin
# The binary finds std/ in ./std, beside itself, under lib/bettersh/std
# in the parent of its directory, and in the parent's std/.
stdlibdir = $(PREFIX)/lib/bettersh/std
STD = $(wildcard std/*.btr)
FUZZ_OBJ = $(filter-out src/main.o,$(OBJ))

.PHONY: all test test-host bench debug clean install uninstall dist fuzz-nightly \
	cross cross-macos-x86_64 cross-linux-x86_64 cross-linux-arm64

all: $(OUT)

$(OUT): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(REQUIRED) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) -lm

tests/value_test: tests/value_test.c src/value.o src/string.o src/array.o src/map.o src/arena.o src/trace.o
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -iquote src -o $@ tests/value_test.c \
		src/value.o src/string.o src/array.o src/map.o src/arena.o src/trace.o $(LDFLAGS)

tests/verify_test: tests/verify_test.c src/verify.o src/chunk.o src/value.o src/string.o \
		src/array.o src/map.o src/arena.o src/trace.o
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -iquote src -o $@ tests/verify_test.c \
		src/verify.o src/chunk.o src/value.o src/string.o src/array.o src/map.o src/arena.o \
		src/trace.o $(LDFLAGS)

tests/chunk_fuzz: tests/chunk_fuzz.c $(FUZZ_OBJ)
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -iquote src -o $@ tests/chunk_fuzz.c \
		$(FUZZ_OBJ) $(LDFLAGS) -lm

tests/osrun: tests/osrun.c
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -o $@ tests/osrun.c $(LDFLAGS)

tests/lsp_peer: tests/lsp_peer.c
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -o $@ tests/lsp_peer.c $(LDFLAGS)

tests/pty_edit: tests/pty_edit.c tests/pty_util.c tests/pty_util.h
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -o $@ tests/pty_edit.c tests/pty_util.c $(LDFLAGS) $(PTY_LIBS)

tests/pty_interact: tests/pty_interact.c tests/pty_util.c tests/pty_util.h
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -o $@ tests/pty_interact.c tests/pty_util.c $(LDFLAGS) $(PTY_LIBS)

tests/pty_btr: tests/pty_btr.c tests/pty_util.c tests/pty_util.h
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -o $@ tests/pty_btr.c tests/pty_util.c $(LDFLAGS) $(PTY_LIBS)

TEST_BINS = tests/value_test tests/verify_test tests/chunk_fuzz \
	tests/osrun tests/lsp_peer tests/pty_edit tests/pty_interact tests/pty_btr

$(OBJDIR)/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -DBETTERSH_VERSION='"$(VERSION)"' -iquote src -c $< -o $@

test: bettersh $(TEST_BINS)
	./tests/run.sh

# Network, the perf script, and Apple leaks. Not part of test.
# The perf script prints a time. It fails only when BETTERSH_PERF_LIMIT is set.
test-host: bettersh $(TEST_BINS)
	BETTERSH_NET=1 BETTERSH_PERF=1 BETTERSH_LEAKS=1 ./tests/run.sh

# Wall-clock benchmarks. They report results and do not gate `make test`.
bench: bettersh
	sh bench/vs-sh.sh

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O0 -g -fsanitize=address,undefined" \
		LDFLAGS="-fsanitize=address,undefined" bettersh $(TEST_BINS)

clean:
	rm -f $(OBJ) bettersh $(TEST_BINS)
	rm -rf build

# Compare ten thousand generated programs on the interpreter and VM.
fuzz-nightly: bettersh
	BETTERSH_FUZZ_COUNT=10000 sh tests/differential/run.sh ./bettersh

# Separate object directories, so a cross build does not replace ./bettersh.
cross: cross-macos-x86_64 cross-linux-x86_64 cross-linux-arm64

cross-macos-x86_64:
	$(MAKE) OBJDIR=build/macos-x86_64 OUT=build/macos-x86_64/bettersh \
		CC="clang -target x86_64-apple-macos -isysroot $(SDK)"

cross-linux-x86_64:
	$(MAKE) OBJDIR=build/linux-x86_64 OUT=build/linux-x86_64/bettersh \
		CC="zig cc -target x86_64-linux-musl"

cross-linux-arm64:
	$(MAKE) OBJDIR=build/linux-arm64 OUT=build/linux-arm64/bettersh \
		CC="zig cc -target aarch64-linux-musl"

install: bettersh
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(stdlibdir)
	cp bettersh $(DESTDIR)$(bindir)/bettersh
	cp bettersh $(DESTDIR)$(bindir)/btr
	cp bettersh $(DESTDIR)$(bindir)/bsh
	cp $(STD) $(DESTDIR)$(stdlibdir)/

uninstall:
	rm -f $(DESTDIR)$(bindir)/bettersh $(DESTDIR)$(bindir)/btr $(DESTDIR)$(bindir)/bsh \
		$(addprefix $(DESTDIR)$(stdlibdir)/,$(notdir $(STD)))
	-rmdir $(DESTDIR)$(stdlibdir)
	-rmdir $(DESTDIR)$(PREFIX)/lib/bettersh
	-rmdir $(DESTDIR)$(PREFIX)/lib

# Archive of HEAD. Run this after the release commit.
dist:
	mkdir -p dist
	git archive --format=tar.gz --prefix=bettersh-$(VERSION)/ -o dist/bettersh-$(VERSION).tar.gz HEAD
	(shasum -a 256 dist/bettersh-$(VERSION).tar.gz || sha256sum dist/bettersh-$(VERSION).tar.gz) \
		> dist/bettersh-$(VERSION).tar.gz.sha256
