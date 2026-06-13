set choices = ["user_123", "admin_panel"]
for choice in choices {
    set result = match choice {
        "user_123": "User ID detected",
        default: "Other"
    }
}
console.show("Done")
