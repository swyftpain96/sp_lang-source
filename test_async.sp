console.show("Starting async test...")

define slow_add = (a, b) => {
    set task = async process.run("sleep", ["5"])
    task.wait()
    return a + b
}

// Test basic async and wait
set fut = async slow_add(10, 20)
console.show("Future created: " + fut)
set result = fut.wait()
console.show("Result: " + result)

// Test implicit unwrapping
set fut2 = async {
    set obj = { name: "Async Object", val: 42 }
    return obj
}
console.show("Result.name (implicit wait): " + fut2.name)

// Test error chaining
define throw_err = () => {
    return fs.read("non_existent_file_xyz.txt")
}

set error_fut = async throw_err()
error_fut.error((e) => {
    console.show("Caught expected error: " + e)
})

console.show("Async test finished.")
