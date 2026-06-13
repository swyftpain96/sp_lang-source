// Test: Positive case for Array<number>
var validList: Array<number> = [1, 2, 3]
console.show("Valid list check passed")

// Test: Negative case for Array<number>
// This should produce a runtime type error during assignment
define testGenericFailure() => {
    console.show("Checking invalid assignment (should fail):")
    var invalidList: Array<number> = [1, "two", 3]
    return invalidList
}

set thisVar: string = 2

var [res, err] = testGenericFailure()
if (err) {
    console.show("Caught expected error: " + err.message)
} else {
    console.show("FAILED: Did not catch expected type mismatch error")
}

// Test: Error type
var e = Error("Custom error message", 42)
console.show("Error message: " + e.message)
console.show("Error line: " + e.line)

// Test: Autocomplete data check (via console.show)
// Ensuring methods exist by calling them
var s = "hello"
console.show("Padded string: " + s.padStart(10, "*"))
console.show("Repeated string: " + s.repeat(2))

var arr = [1, 2, 3]
console.show("First element find: " + arr.find((item) => item > 1))
