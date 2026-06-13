set info = fs.info("test_date.sp")

if (info.exists) {
    console.show("File:", info.name)
    console.show("Modified At:", info.modifiedAt)
    console.show("Created At:", info.createdAt)
    
    set m = info.modifiedAt
    console.show("Modified Year:", m.year)
    console.show("Modified Month:", m.month)
    console.show("Modified Day:", m.day)
} else {
    console.show("File does not exist.")
}
