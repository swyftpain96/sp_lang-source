console.show("--- Array Destructuring ---")
var arr = [1, 2, 3, 4]
var [first, second, ...rest] = arr
console.show("first: {first}")    // 1
console.show("second: {second}")  // 2
console.show("rest: {rest}")      // [3, 4]

console.show("--- Object Destructuring ---")
var obj = { x: 10, y: 20, z: 30 }
var { x, y: y_val, ...restObj } = obj
console.show("x: {x}")            // 10
console.show("y_val: {y_val}")    // 20
console.show("restObj: {restObj}")// { z: 30 }


console.show("--- Spread Arrays and Objects ---")
var mergedArr = [0, ...arr, 5]
console.show("mergedArr: {mergedArr}") // [0, 1, 2, 3, 4, 5]

var mergedObj = { start: 0, ...obj, end: 100 }
console.show("mergedObj: {mergedObj}")

console.show("--- Nested Destructuring ---")
var data = { user: { name: "Alice", age: 25 }, tags: ["vip", "early-adopter"] }
var { user: { name }, tags: [firstTag] } = data
console.show("name: {name}")         // Alice
console.show("firstTag: {firstTag}") // vip

console.show("--- Rest Params & Call Spread ---")
define myFun = (a, b, ...args) => {
    console.show("a: {a}")
    console.show("b: {b}")
    console.show("args: {args}")
}
myFun(1, 2, 3, 4, 5)

var callArgs = [3, 4, 5]
myFun(1, 2, ...callArgs)
