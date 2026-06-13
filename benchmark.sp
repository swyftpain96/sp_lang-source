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

set n = 100
console.show("--- Iterative Fibonacci Benchmark ---")
console.show("Calculating fib({n})...")

set start = timeNano()
set result = fib(n)
set elapsed = timeNano() - start

if elapsed < 1000 {
    console.show("Duration: {elapsed} ns")
} else {
    if elapsed < 1000000 {
        set us = elapsed / 1000
        console.show("Duration: {us} μs ({elapsed} ns)")
    } else {
        if elapsed < 1000000000 {
            set ms = elapsed / 1000000
            console.show("Duration: {ms} ms ({elapsed} ns)")
        } else {
            set s = elapsed / 1000000000
            console.show("Duration: {s} s ({elapsed} ns)")
        }
    }
}

console.show("Result: {result}")