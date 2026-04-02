define fib = (n) => {
    if n <= 1 {
        return n
    }
    
    var a = 0
    var b = 1
    var count = 2
    
    while count <= n {
        var next = a + b
        a = b
        b = next
        count = count + 1
    }
    
    return b
}

set n = 90
console.show("--- Iterative Fibonacci Benchmark ---")
console.show("Calculating fib({n})...")

set start = time()
set result = fib(n)
set end = time()

set diff = end - start
set seconds = floor(diff / 1000)
set ms = diff - (seconds * 1000)

if seconds > 0 {
    console.show("Duration: {seconds} seconds and {ms} milliseconds")
} else {
    console.show("Duration: {ms} milliseconds")
}

console.show("Result: {result}")