set fileName = "test_json_data.json"
set data = fs.readJson(fileName)
console.show("Read JSON from", fileName)

// Test nested access
console.show("User name:", data.user.name)
console.show("User age:", data.user.age)

// Test modification
data.user.name = "Alice Updated"
data.user.age = 26
console.show("Modified data name in memory:", data.user.name)

// Test write
set outFile = "data.output.json"
fs.writeJson(outFile, data)
console.show("Wrote JSON to", outFile)

// Test re-read
set result = fs.readJson(outFile)
console.show("Re-read data from", outFile)
console.show("New name:", result.user.name)

if (result.user.name == "Alice Updated") {
    console.show("JSON First-Class Support: SUCCESS")
} else {
    console.warn("JSON First-Class Support: FAILURE")
}
