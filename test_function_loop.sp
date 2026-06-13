define testLoop = () => {
    set choices = ["user_123", "admin_panel", "other_456"]
    for choice in choices {
        console.show("Before match:", choice)
        set res = match choice {
            regex("^user_\d+$"): "User ID detected",
            regex("^admin_.*"): "Admin detected",
            default: "Other"
        }
        console.show("After match:", res)
    }
}

testLoop()
console.show("Done")
