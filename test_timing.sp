// Testing the new timing features with process.sleep()

console.show("Starting timing tests...")

var tickCount = 0
var repeating = every 200 {
    tickCount = tickCount + 1
    console.show("Tick " + tickCount)
}

after 1000 {
    console.show("One-second timer fired!")
}

// Wait for ~1.5 seconds using the new process.sleep()
process.sleep(1500)

repeating.stop()
console.show("Timer stopped at " + tickCount + " ticks.")

if (tickCount >= 5) {
    console.show("SUCCESS: Repeating timer worked correctly.")
} else {
    console.show("FAILURE: Repeating timer tick count too low: " + tickCount)
}

process.sleep(500)
console.show("Final count check: " + tickCount)
