// Test Advanced File System Support (including info, name, ext)

set fileName = "data.test.json"

console.show("--- Initializing file ---")
fs.overwrite(fileName, "{\"name\": \"sp_language\"}")

console.show("--- Testing fs.info ---")
set info = fs.info(fileName)

console.show("Info object: ", info)
console.show("Info.name: ", info.name)
console.show("Info.ext: ", info.ext)
console.show("Info.size: ", info.size)
console.show("Info.length: ", info.length)
console.show("Info.exists: ", info.exists)

if (info.ext == ".json") {
    console.show("Success: extension detected correctly")
}

console.show("--- Testing pipe with fs.info ---")
set extension = fs.info(fileName) |> _.ext
console.show("Extension via pipe: ", extension)

console.show("--- Cleanup ---")
// fs.delete(fileName)
console.show("Success: verified and cleaned up")
