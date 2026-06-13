// Test basic types
var a: number = 10
var b: string = "hello"

console.show("a:", a)
console.show("b:", b)

// Test grouped parameter types
define add(x, y: number): number => {
    return x + y
}

console.show("add(5, 10):", add(5, 10))

// Test union types
define printVal(val: string | number) => {
    console.show("Value is:", val)
}

printVal("test")
printVal(123)

// Test return type
define getGreeting(name: string): string => {
    return "Hello " + name
}

console.show(getGreeting("World"))

// Optional Typing: Untyped variables and functions still work fine
var untypedVar = "I have no type!"
set untypedConst = 42

define untypedFunc = (x) => {
    console.show("Untyped param:", x)
    return x * 2
}

console.show(untypedVar)
console.show(untypedFunc(10))

// This should fail because it's typed
var typedVar: number = "not a number"
typedVar.error((e) => {
    console.show("Caught expected type error:", e)
})

// This should pass because it's untyped (it's 'any')
var x = "string"
x = 123
console.show("Untyped transition worked:", x)

// Test generic syntax (basic check, base type enforcement)
var list: array = [1, 2, 3]
console.show("List:", list)

define processList(l: array) => {
    console.show("Processing list of size:", l.length)
}
processList(list)

console.show("Types test completed successfully!")
