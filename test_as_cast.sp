use db_addon

console.show("--- Testing Type Assertions & SPD ---")

// Mock dynamic value (usually from storage or native addon)
set unknown_data = { id: 101, name: "Bob", email: "bob@example.com", is_admin: true }

// Using 'as' to cast to the layout defined in db_addon.spd
set user = unknown_data as UserProfile

console.show("User Name: ", user.name)
console.show("User ID: ", user.id)

if user.is_admin {
    console.show("User is an admin")
}

// Verify that it still works fine at runtime even if types don't strictly match (since it's a pass-through)
set x = "123" as number
console.show("X (cast string as number): ", x)

// Test module usage from .spd
set version = db_addon.get_version()
console.show("Addon Version: ", version)

console.show("--- Test Complete ---")
