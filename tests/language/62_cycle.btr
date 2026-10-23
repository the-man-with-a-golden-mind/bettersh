#!/usr/bin/env bettersh
fn make() {
  let a = null
  let b = fn() { a }
  let a = fn() { b }
}
fn boxmake() {
  let box = []
  box.push(fn() { box })
}
make()
boxmake()
println("ok")
