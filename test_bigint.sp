set a = 1000000000000000000n
set b = 2000000000000000000n
set c = a + b
console.show("a:", a)
console.show("b:", b)
console.show("c (a + b):", c)

if (c == 3000000000000000000n) {
    console.show("BigInt addition works!")
} else {
    console.show("BigInt addition failed!")
}

set d = 10n + 20n
console.show("10n + 20n =", d)
if (d == 30n) {
    console.show("Small BigInt addition works!")
}
