console.show("--- While Loop ---")
var count = 0
while count < 3 {
    console.show("Count: ", count)
    count = count + 1
}

console.show("--- For-In Array ---")
set items = ["Apple", "Banana", "Cherry"]
for item in items {
    console.show("Item: ", item)
}

console.show("--- Range Function ---")
for i in range(5, 8) {
    console.show("Range: ", i)
}
