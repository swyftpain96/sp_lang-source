set argsList = console.args()
console.show("Args count:", argsList.length)
console.show("Arguments:", argsList)

console.show("Enter your name:")
set name = console.read()
console.show("Hello, {name}!")

console.warn("This is a CLI warning!")