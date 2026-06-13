layout Point {
    x: number,
    y: number
}

layout Named {
    name: string
}

// Composition via &
layout NamedPoint = Point & Named

define greet(p: Point) => {
    console.show("Point x: {p.x} y: {p.y}")
}

define showNamed(n: Named) => {
    console.show("Name: {n.name}")
}

define checkBoth(np: NamedPoint) => {
    console.show("NamedPoint: {np.name} at {np.x} {np.y}")
}

console.show("--- Testing Point ---")
set p1 = { x: 10, y: 20 }
greet(p1)

console.show("--- Testing Named & Point ---")
set n1 = { name: "Origin", x: 0, y: 0 }
showNamed(n1)
checkBoth(n1)

console.show("--- Testing Structural Literals ---")
define checkRect(r: { width: number, height: number }) => {
    console.show("Rect {r.width} x {r.height}")
}
checkRect({ width: 100, height: 200 })

console.show("--- Testing Type Mismatch (Expect error) ---")
// In the current SP runtime, a type mismatch in a function call throws an error.
greet({ x: 10 }) 

console.show("Layout tests finished!")
