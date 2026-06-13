set x = 1
if (true) {
    var x = 2
    x = 3
    console.show("Local x:", x)
}
console.show("Global x:", x)

if (x == 1) {
    console.show("Shadowing Test: SUCCESS")
} else {
    console.warn("Shadowing Test: FAILURE")
}
