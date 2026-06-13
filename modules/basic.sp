console.show("--- Basic Types ---")
set name = "Alice"
set age = 30
console.show("Age / 2 = {age / 2}")
console.show("Hello, {name}")
set arr = [1, 2, 3]
console.show("Array: {arr}")

console.show("--- Object Ordering ---")
set obj = {age: 30, name: name}
console.show("Object: {obj}")
set reversedObj = {name: name, age: 30}
console.show("Reversed Object: {reversedObj}")

console.show("--- Access ---")
console.show("Name: {obj.name}")
console.show("Array[1]: {arr[1]}")
console.show(undefined)

console.show("--- Access ---")
console.show(obj.name)
console.show(arr[1])
