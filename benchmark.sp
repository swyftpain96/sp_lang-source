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

set iterations = 100000

console.show("--- Fibonacci Throughput Benchmark ---")
console.show("Running fib(100) {iterations} times...")

set start = timeNano()

var i = 0
var checksum = 0

while i < iterations {
    checksum = checksum + fib(100)
    i = i + 1
}

set elapsed = timeNano() - start

set avg = elapsed / iterations

console.show("Total time: {elapsed} ns")
console.show("Average per call: {avg} ns")
console.show("Checksum: {checksum}")