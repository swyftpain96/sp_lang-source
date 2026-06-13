set obj = { name: "Alice", age: 30 }
set str = JSON.stringify(obj)
console.show("JSON:", str)

set parsed = JSON.parse(str)
console.show("Parsed Name:", parsed.name)
console.show("Parsed Age:", parsed.age)

if parsed.name == "Alice" && parsed.age == 30 {
    console.show("✅ JSON test passed!")
} else {
    console.show("❌ JSON test failed!")
}
