set result = process.run("echo", ["Hello from Child Process!"])
console.show("Output:", result.output)
console.show("Status:", result.status)
console.show("Failed:", result.failed)

set lsResult = process.run("ls", ["-l", "main.cpp"])
console.show("LS Output:", lsResult.output)

set failedResult = process.run("nonexistentcommand")
console.show("Failed Status:", failedResult.status)
console.show("Is Failed:", failedResult.failed)

process.spawn("touch", ["spawned_file.txt"])
console.show("Spawned process started (check for spawned_file.txt)")
