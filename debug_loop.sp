set choices = ["user_123", "admin_panel"]
for choice in choices {
    console.show("Before match:", choice)
    set result = match choice {
        regex("^user_\d+$"): "User ID detected",
        default: "Other"
    }
    console.show("After match:", result)
}
console.show("Done")
