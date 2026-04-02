define callWithTen = (fn) => fn(10)

set result = callWithTen((x) => {
    console.show("Received:", x)
    return x * 5
})

console.show("Result should be 50:", result)

if (result == 50) {
    console.show("Functional Callbacks Test: SUCCESS")
} else {
    console.warn("Functional Callbacks Test: FAILURE")
}
