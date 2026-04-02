/**
 * test_manipulation.sp
 * Comprehensive test suite for basic type manipulation methods.
 */

// --- String Manipulation ---
set s = " Hello, World! "
console.show("Original: '" + s + "'")
console.show("Trimmed: '" + s.trim() + "'")
console.show("Lower: " + s.toLowerCase())
console.show("Upper: " + s.toUpperCase())
console.show("Contains 'World':", s.contains("World"))
console.show("Contains 'Goodbye':", s.contains("Goodbye"))
console.show("Starts with ' Hello':", s.startsWith(" Hello"))
console.show("Ends with '! ':", s.endsWith("! "))
console.show("Index of 'World':", s.indexOf("World"))
console.show("Split result:", s.split(", "))
console.show("Replace 'World' with 'SP':", s.replace("World", "SP"))
console.show("Substring(1, 6): '" + s.substring(1, 6) + "'")

// --- Array Manipulation ---
set arr = [1, 2, 3]
console.show("Original array:", arr)
arr.push(4)
console.show("After push(4):", arr)
set popped = arr.pop()
console.show("Popped:", popped, "Array:", arr)
arr.unshift(0)
console.show("After unshift(0):", arr)
set shifted = arr.shift()
console.show("Shifted:", shifted, "Array:", arr)
console.show("Joined with '-':", arr.join("-"))
console.show("Reversed:", arr.reverse())
console.show("Slice(1, 3):", arr.slice(1, 3))
console.show("Includes 2:", arr.includes(2))
console.show("Includes 10:", arr.includes(10))
console.show("Index of 2:", arr.indexOf(2))

// Functional methods on arrays
console.show("--- Functional Methods ---")
set mapped = arr.map((x) => x * 2)
console.show("Mapped (x * 2):", mapped)
set filtered = arr.filter((x) => x > 1)
console.show("Filtered (x > 1):", filtered)
console.show("ForEach items:")
arr.forEach((x) => console.show("  Element:", x))

// --- Object Manipulation ---
set obj = { name: "Swyftpain", version: 1.0 }
console.show("Object:", obj)
console.show("Keys:", obj.keys())
console.show("Values:", obj.values())
console.show("Has 'name':", obj.has("name"))
console.show("Has 'unknown':", obj.has("unknown"))

// --- Number Manipulation ---
set num = 123.456789
console.show("Number:", num)
console.show("toFixed(2):", num.toFixed(2))
console.show("toString():", num.toString())

console.show("All tests completed!")