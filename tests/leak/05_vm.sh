#!/usr/bin/env bettersh
let f = ["a", "b", "c"]
let g = f.map(fn(x) { x.upper() })
println(g.join("-"))
let m = { name: "Ada", n: 1 }
println(m.keys.join(","))
let t = "  Hi  "
println(t.trim().lower())
fn id(x) { x.upper() }
println(id("ab"))
let s = "hi @{ 2 + 2 }"
println(s)
let xs = ["b", "a"]
println(xs.sort().join(","))
let a = ["x"]
a.push(a)
println(a.length)
fn fib(n) { if n < 2 { return n }; return fib(n - 1) + fib(n - 2) }
println(fib(8))
let s = 0
for i in 1..20 { let s = s + i }
println(s)
fn adder(n) { fn(x) { (x + n) } }
println(adder(2)(3))
import std/strings
println(strings.shout("yo"))
import std/math
println(math.gcd(12, 18))
import std/path
println(path.join("a", "b"))
import std/ui
import std/edit
let e = edit.app(30, 8)
edit.load(e, "ab\n", "n.btr")
edit.type_text(e, "pri")
edit.keys_in(e, ["escape", "ctrl-d", "ctrl-shift-d", "ctrl-z", "ctrl-p", "escape"])
println(edit.text(e))
println(edit.frame(e).length > 0)
let screen = ui.blank(8, 3)
let screen = ui.box(screen, 0, 0, 3, 8, "z")
let screen = ui.put(screen, 1, 2, "ok")
println(ui.paint(screen))
