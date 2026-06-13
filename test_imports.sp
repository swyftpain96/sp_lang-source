console.show("--- Plain use ---")
use test_math
console.show("2 + 3 = {test_math.add(2, 3)}")
console.show("Pi: {test_math.pi}")

console.show("--- use as alias ---")
use test_math as tm
console.show("10 + 5 = {tm.add(10, 5)}")
console.show("tm.Pi: {tm.pi}")

console.show("--- named imports ---")
use { add, pi } from test_math
console.show("7 + 8 = {add(7, 8)}")
console.show("pi: {pi}")

console.show("--- named imports with aliases ---")
use { mul as multiply, pi as PI } from test_math
console.show("4 * 6 = {multiply(4, 6)}")
console.show("PI: {PI}")
