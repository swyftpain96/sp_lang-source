// Test Map and HashMap support

set m = Map()
console.show("Initial size:", m.size)

m.set("a", 1)
m.set("b", 2)
m.set(3, "c")
m.set(true, "d")

console.show("Size after sets:", m.size)
console.show("get('a'):", m.get("a"))
console.show("get(3):", m.get(3))
console.show("get(true):", m.get(true))
console.show("has('b'):", m.has("b"))
console.show("has('x'):", m.has("x"))

m.delete("b")
console.show("Size after delete('b'):", m.size)
console.show("has('b') after delete:", m.has("b"))

console.show("Keys:", m.keys())
console.show("Values:", m.values())

m.clear()
console.show("Size after clear:", m.size)

// Test HashMap alias
set hm = HashMap()
hm.set("hello", "world")
console.show("HashMap get('hello'):", hm.get("hello"))

// Test chaining
m.set("x", 10).set("y", 20).set("z", 30)
console.show("Chaining size:", m.size)
console.show("Chaining get('y'):", m.get("y"))

define printKV = (k, v) => {
    console.show("forEach - Key:", k, "Val:", v)
}

m.forEach(printKV)

console.show("Map tests passed!")
