CC       ?= cc
# These flags are required. An environment CFLAGS must not drop them.
REQUIRED  = -std=c17 -Wall -Wextra -Wpedantic -Werror
CFLAGS   ?= -O2
DEFINES   = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS  ?=
VERSION  := $(shell cat VERSION)

SRC = src/main.c src/arena.c src/error.c src/ast.c \
      src/lexer.c src/lex_word.c src/lex_param.c \
      src/parser.c src/parse_cmd.c \
      src/string.c src/value.c src/array.c src/map.c src/env.c src/process.c \
      src/eval_expr.c \
      src/builtins.c src/builtin_printf.c src/builtin_test.c src/builtin_cd.c \
      src/builtin_var.c src/builtin_proc.c src/builtin_cmd.c src/builtin_posix.c \
      src/builtin_fc.c src/builtin_limit.c \
      src/eval.c src/eval_sh.c src/expand.c src/arith_text.c src/redir.c src/hash.c \
      src/chunk.c src/mod_scan.c src/mod_expr.c src/mod_parse.c \
      src/compiler.c src/compile_expr.c src/compile_flow.c src/verify.c src/disasm.c \
      src/vm.c src/vm_ops.c src/vm_method.c src/vm_exec.c src/interp.c src/interp_expr.c \
      src/module.c src/host.c src/host_term.c src/modern.c \
      src/shjob.c src/interact.c src/vi_line.c
OBJ = $(SRC:.c=.o)

PREFIX  ?= /usr/local
bindir   = $(PREFIX)/bin
# The binary finds std/ in its own directory and in the parent of that directory.
stdlibdir = $(PREFIX)/std
STD = $(wildcard std/*.bsh)

.PHONY: all test test-host bench debug clean install uninstall

all: bettersh

bettersh: $(OBJ)
	$(CC) $(REQUIRED) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) -lm

tests/value_test: tests/value_test.c src/value.o src/string.o src/array.o src/map.o src/arena.o
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -iquote src -o $@ tests/value_test.c \
		src/value.o src/string.o src/array.o src/map.o src/arena.o $(LDFLAGS)

tests/verify_test: tests/verify_test.c src/verify.o src/chunk.o src/value.o src/string.o \
		src/array.o src/map.o src/arena.o
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -iquote src -o $@ tests/verify_test.c \
		src/verify.o src/chunk.o src/value.o src/string.o src/array.o src/map.o src/arena.o $(LDFLAGS)

src/%.o: src/%.c src/*.h
	$(CC) $(REQUIRED) $(CFLAGS) $(DEFINES) -DBETTERSH_VERSION='"$(VERSION)"' -iquote src -c $< -o $@

test: bettersh tests/value_test tests/verify_test
	./tests/run.sh

# Network, the absolute perf threshold, and Apple leaks. Not part of test.
test-host: bettersh tests/value_test tests/verify_test
	BETTERSH_NET=1 BETTERSH_PERF=1 BETTERSH_LEAKS=1 ./tests/run.sh

# Wall times. Not part of test, and not the 0.2s perf check.
bench: bettersh
	sh bench/vs-sh.sh

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O0 -g -fsanitize=address,undefined" \
		LDFLAGS="-fsanitize=address,undefined" bettersh tests/value_test tests/verify_test

clean:
	rm -f $(OBJ) bettersh tests/value_test tests/verify_test

install: bettersh
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(stdlibdir)
	cp bettersh $(DESTDIR)$(bindir)/bettersh
	cp $(STD) $(DESTDIR)$(stdlibdir)/

uninstall:
	rm -f $(DESTDIR)$(bindir)/bettersh $(addprefix $(DESTDIR)$(stdlibdir)/,$(notdir $(STD)))
	-rmdir $(DESTDIR)$(stdlibdir)
