console.show("--- Pattern Matching ---")
set choice = 2
set result = match choice {
    1: "One",
    2: "Two",
    default: "Other"
}
console.show("Choice was: {result}")

console.show("--- Pipes ---")
define doubleIt = (x) => x * 2
define log = (x) => { console.show("Logged: {x}") }
10 |> doubleIt |> log

console.show("--- Pipeline Placeholders ---")
define greet = (greeting, name) => {
    console.show("{greeting} {name}")
}
"Alice" |> greet("Hello,", _)
"Bob" |> greet(_, "Smith")
