#!/usr/bin/env python3
"""Write deterministic modern scripts. The runner compares --interp with the VM."""

import os
import random
import sys


def main():
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    os.makedirs(out, exist_ok=True)
    for i in range(count):
        path = os.path.join(out, "s%04d.bsh" % i)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(script(i))


def script(i):
    rng = random.Random(0xB477 + i)
    lines = ["#!/usr/bin/env bettersh"]
    names = []
    nlets = rng.randint(1, 4)
    for k in range(nlets):
        name = "v%d" % k
        lines.append("let %s = %d" % (name, rng.randint(-20, 40)))
        names.append(name)
    kind = i % 10
    if kind == 0:
        logic(lines, rng, names)
    elif kind == 1:
        branches(lines, rng, names)
    elif kind == 2:
        loops(lines, rng, names)
    elif kind == 3:
        ranges(lines, rng)
    elif kind == 4:
        arrays(lines, rng)
    elif kind == 5:
        cases(lines, rng, names)
    elif kind == 6:
        functions(lines, rng, names)
    elif kind == 7:
        defaults(lines, rng, names)
    elif kind == 8:
        closures(lines, rng)
    else:
        recur(lines, rng)
    if rng.randrange(3) == 0:
        shared(lines)
    a = names[0]
    b = names[-1]
    lines.append("println(%s + %s)" % (a, b))
    lines.append("println(%s == %s)" % (a, b))
    lines.append("println(-%s)" % a)
    lines.append("println(!%s)" % a)
    return "\n".join(lines) + "\n"


def logic(lines, rng, names):
    a = names[0]
    b = names[-1]
    lit = rng.randint(-3, 5)
    lines.append("println(%s && %s)" % (a, b))
    lines.append("println(%s || %d)" % (a, lit))
    lines.append("println(%d && %s)" % (lit, b))
    lines.append("println(0 && 5)")
    lines.append("println(1 && 5)")
    lines.append("println(1 || 5)")
    lines.append("println(0 || 5)")
    lines.append('println("a" < "b")')
    lines.append("println(1 == 1.0)")


def branches(lines, rng, names):
    a = names[0]
    lines.append("if %s < 0 {" % a)
    lines.append("  println(1)")
    lines.append("} else if %s == 0 {" % a)
    lines.append("  println(2)")
    lines.append("} else {")
    lines.append("  println(3)")
    lines.append("}")
    bound = rng.randint(0, 3)
    lines.append("let c = 0")
    lines.append("let w = 0")
    lines.append("while c < %d {" % bound)
    lines.append("  let w = w + %s" % a)
    lines.append("  let c = c + 1")
    lines.append("}")
    lines.append("println(w)")


def loops(lines, rng, names):
    a = rng.randint(0, 4)
    b = rng.randint(0, 5)
    lines.append("let rs = 0")
    lines.append("for ri in %d..%d {" % (a, b))
    lines.append("  let rs = rs + ri")
    lines.append("}")
    lines.append("println(rs)")
    lines.append("let n0 = 7")
    lines.append("for n0 in 4..2 {")
    lines.append("  println(n0)")
    lines.append("}")
    lines.append("println(n0)")


def ranges(lines, rng):
    lines.append("let rs = 0")
    lines.append("for ri in %d..%d { let rs = rs + ri }" % (rng.randint(-2, 2), rng.randint(0, 6)))
    lines.append("println(rs)")
    lines.append("for zj in 3..1 { println(zj) }")
    lines.append("println(zj)")


def arrays(lines, rng):
    vals = [str(rng.randint(-4, 6)) for _ in range(rng.randint(0, 5))]
    lines.append("let arr = [%s]" % ", ".join(vals))
    lines.append("let sum = 0")
    lines.append("for el in arr { let sum = sum + el }")
    lines.append("println(sum)")
    lines.append("println(el)")


def cases(lines, rng, names):
    src = names[rng.randrange(len(names))]
    lines.append("case %s {" % src)
    lines.append("  0: println(0)")
    lines.append("  -1: println(4)")
    lines.append("  1..9: println(1)")
    lines.append("  10..: println(2)")
    lines.append("  *: println(3)")
    lines.append("}")
    lines.append('case "a" { "a": println(1) *: println(0) }')


def functions(lines, rng, names):
    arg = names[0]
    lines.append("fn plus(a, b) { (a + b) }")
    lines.append("println(plus(%s, %d))" % (arg, rng.randint(-2, 4)))
    lines.append("fn pick(n) { if n < 0 { return 0 - n }; return n }")
    lines.append("println(pick(%s))" % arg)


def defaults(lines, rng, names):
    lines.append("fn add(a, b = a) { (a + b) }")
    lines.append("println(add(%s))" % names[0])
    lines.append("println(add(%s, %d))" % (names[-1], rng.randint(0, 3)))
    lines.append('fn greet(name, greeting = "Hello") { (greeting) }')
    lines.append('println(greet("Ada"))')
    lines.append('println(greet("Ada", "Hi"))')
    lines.append("fn opt(a = 1) { (a) }")
    lines.append("println(opt(null))")
    lines.append("println(opt())")


def closures(lines, rng):
    x = rng.randint(-3, 6)
    y = rng.randint(-3, 6)
    lines.append("fn adder(n) { fn(x) { (x + n) } }")
    lines.append("println(adder(%d)(%d))" % (x, y))


def recur(lines, rng):
    n = rng.randint(0, 6)
    lines.append("fn fact(n) { if n < 2 { return n }; return n * fact(n - 1) }")
    lines.append("println(fact(%d))" % n)


def shared(lines):
    lines.append("let cell = 1")
    lines.append("fn reader() { return cell }")
    lines.append("let cell = 4")
    lines.append("println(reader())")
    lines.append("fn shade() { let cell = 8; return cell }")
    lines.append("println(shade())")
    lines.append("println(cell)")


if __name__ == "__main__":
    main()
