// Test functional error handling
define divide(a, b) => {
    if (b == 0) { 
        return [null, "Division by zero"] 
    }
    return [a / b, null]
}

// Test new destructuring pattern [result, error] on any value
define testThrowable(shouldFail) => {
    if (shouldFail) {
        return "Failure message" 
    }
    return 42
}

console.show("--- Testing .error() pattern ---")

// Type mismatch (real error)
var x: number = "not a number"
x.error((e) => {
    console.show("Caught expected type error:", e)
})

// Chaining (success case)
var y: number = 100
var resY = y.error((e) => {
    console.show("This should NOT be printed:", e)
})
console.show("resY should be 100:", resY)

console.show("--- Testing [result, error] destructuring pattern ---")

// 1. Array-based destructuring (standard)
var [res1, err1] = divide(10, 2)
console.show("10 / 2 => res:", res1, "err:", err1)

var [res2, err2] = divide(10, 0)
console.show("10 / 0 => res:", res2, "err:", err2)

// 2. Value-based destructuring (Special Pattern)
// If we have a success value: [val, null]
var [val3, err3] = testThrowable(false)
console.show("Success: val:", val3, "err:", err3)

// If we have an ERROR value: [null, error]
// Currently, CHECK_TYPE is the only way to get a real ErrorData in the VM easily
var z: string = 123 // Fails, z becomes ErrorData
var [val4, err4] = z
console.show("Error destructuring: val:", val4, "err:", err4)

console.show("Error handling tests completed!")
