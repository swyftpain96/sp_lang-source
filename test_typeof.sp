console.show("--- Typeof Tests ---")

set n = 42
set s = "hello"
set b = true
set a = [1, 2, 3]
set f = (x) => x * x
set ni = null
set ud = undefined
set re = regex("abc")
set m = Map()
set o = { x: 1 }

console.show("number:", typeof n)
console.show("string:", typeof s)
console.show("boolean:", typeof b)
console.show("array:", typeof a)
console.show("function:", typeof f)
console.show("null:", typeof ni)
console.show("undefined:", typeof ud)
console.show("regex:", typeof re)
console.show("map:", typeof m)
console.show("object:", typeof o)

// BigInt
set bi = 100n
console.show("bigint:", typeof bi)

// Class
class Person {
  set name
}
set p = Person()
console.show("class:", typeof Person)
console.show("instance:", typeof p)

// Future/Timer
async { 1 }
set fut = async { 1 }
console.show("future:", typeof fut)

set t0 = after 100 { 1 }
console.show("timer:", typeof t0)

// Error
set err = Error("test")
console.show("error:", typeof err)

console.show("--- Done ---")
