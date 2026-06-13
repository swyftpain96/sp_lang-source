#ifndef BUILTIN_MODULES_H
#define BUILTIN_MODULES_H

#include <string>
#include <unordered_map>

inline std::unordered_map<std::string, std::string> builtinModules = {
    {"basic", R"module(console.show("--- Basic Types ---")
set name = "Alice"
set age = 30
console.show("Age / 2 = {age / 2}")
console.show("Hello, {name}")
set arr = [1, 2, 3]
console.show("Array: {arr}")

console.show("--- Object Ordering ---")
set obj = {age: 30, name: name}
console.show("Object: {obj}")
set reversedObj = {name: name, age: 30}
console.show("Reversed Object: {reversedObj}")

console.show("--- Access ---")
console.show("Name: {obj.name}")
console.show("Array[1]: {arr[1]}")
console.show(undefined)

console.show("--- Access ---")
console.show(obj.name)
console.show(arr[1])
)module"},
    {"math", R"module(export define add = (a, b) => a + b
)module"},
    {"logic", R"module(set age = 30
set isAdult = age >= 18
console.show("Is adult: {isAdult}")
set t = true
set f = false
console.show("True and False: {t && f}")
console.show("True or False: {t || f}")
console.show("Not True: {!t}")

console.show("1 == 1: {1 == 1}")
console.show("1 != 2: {1 != 2}")
console.show("Age < 40: {age < 40}")
console.show("Alice == Alice: {true}")
console.show("Bob != Alice: {true}")
)module"},
    {"functions", R"module(define greet = (n) => {
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
)module"},
    {"advanced", R"module(console.show("--- Pattern Matching ---")
set choice = 2
set result = match choice {
    1: "One",
    2: "Two",
    default: "Other"
}
console.show("Choice was: {result}")

console.show("--- Pipes ---")
define doubleIt = (x) => x * 2
define log = (x) => { console.show("Logged: {x}") }
10 |> doubleIt |> log

console.show("--- Pipeline Placeholders ---")
define greet = (greeting, name) => {
    console.show("{greeting} {name}")
}
"Alice" |> greet("Hello,", _)
"Bob" |> greet(_, "Smith")
)module"},
    {"storage", R"module(
export set setItem = __native_storage__.setItem
export set getItem = __native_storage__.getItem
export set removeItem = __native_storage__.removeItem
export set clear = __native_storage__.clear
)module"}
};

#endif
