// Test File System Support

set fileName = "data.txt"
set content = "Initial content"

console.show("--- Testing fs.create ---")
set res = fs.create(fileName, content)
if (res != null) {
    console.warn("Got error (expected if file exists):", res.error)
} else {
    console.show("Success: created", fileName)
}

console.show("--- Testing fs.append ---")
fs.append(fileName, "\nAppended line")
console.show("Success: appended to", fileName)

console.show("--- Reading file content (via cat) ---")
set readRes = process.run("cat", [fileName])
console.show("Content:\n", readRes.output)

console.show("--- Testing fs.overwrite ---")
fs.overwrite(fileName, "New overwritten content")
console.show("Success: overwrote", fileName)

set readRes2 = process.run("cat", [fileName])
console.show("New Content:\n", readRes2.output)

console.show("--- Testing fs.delete ---")
fs.delete(fileName)
console.show("Success: deleted", fileName)

set readRes3 = process.run("ls", [fileName])
if (readRes3.failed) {
    console.show("Confirmed: file no longer exists")
} else {
    console.warn("Error: file still exists!")
}

console.show("--- Testing fs.create (handle existing) ---")
fs.create("temp.txt", "temp")
set res2 = fs.create("temp.txt", "duplicate")
if (res2 != null) {
    console.show("Correctly caught existing file error:", res2.error)
}
fs.delete("temp.txt")
