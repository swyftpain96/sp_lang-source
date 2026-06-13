use storage

console.show("--- Testing Async Storage ---")

// 1. Set simple value
console.show("Setting 'theme' to 'dark'...")
storage.setItem("theme", "dark").wait() // Explicit wait

// 2. Set complex object
console.show("Setting 'user' object...")
storage.setItem("user", {
    name: "Alice",
    age: 30,
    tags: ["dev", "sp"]
}).wait() // Explicit wait

// 3. Get values
set theme = storage.getItem("theme")

set user = storage.getItem("user")

// 4. Verification
set isDark = (theme == "dark")
set isAlice = (user.name == "Alice")
if isDark && isAlice {
    console.show("\n✅ Basic storage tests passed!")
} else {
    console.show("\n❌ Basic storage tests failed! (Expected 'dark' and 'Alice')")
}

// 5. Deletion
console.show("\nRemoving 'theme'...")
storage.removeItem("theme").wait() // Explicit wait
set deletedTheme = storage.getItem("theme")
if deletedTheme == null {
    console.show("✅ Remove test passed!")
} else {
    console.show("❌ Remove test failed! (Value was {deletedTheme})")
}

// 6. Clear
console.show("\nClearing all storage...")
storage.clear().wait() // Explicit wait
set nullUser = storage.getItem("user")
if nullUser == null {
    console.show("✅ Clear test passed!")
} else {
    console.show("❌ Clear test failed!")
}

console.show("\n--- Async Storage Test Complete ---")
