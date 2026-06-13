define greet = (n) => {
    console.show("Hello, {n}")
}
greet("World")

define add = (a, b) => a + b
console.show(add(5, 10))
console.show(add(20, 22))

console.show("--- Closures ---")
define makeAdder = (x) => {
    define adder = (y) => x + y
    adder
}

set add5 = makeAdder(5)
set add10 = makeAdder(10)

console.show(add5(2))
console.show(add10(2))
console.show(add5(3))

console.show("--- Conditionals ---")
if true {
    console.show("True branch executed")
} else {
    console.show("False branch executed (Bug!)")
}

if false {
    console.show("True branch executed (Bug!)")
} else {
    console.show("False branch executed")
}

set n = 10
if n > 5 {
    console.show("n is greater than 5")
}

console.show("--- Returns ---")
define checkReturn = (x) => {
    if x > 10 {
        return "Big"
    } else {
        return "Small"
    }
    console.show("This should not print")
    return "Error"
}

console.show(checkReturn(15))
console.show(checkReturn(5))

console.show("--- Implicit Returns ---")
define checkImplicit = (x) => {
    if x > 10 {
        "Huge"
    } else {
        "Tiny"
    }
}

console.show(checkImplicit(15))
console.show(checkImplicit(5))

console.show("--- Variadic ---")
console.show("Hello,", "world!", 1, 2, 3)
define missingArgs = (a, b) => { console.show(a, b) }
// missingArgs("Present")
