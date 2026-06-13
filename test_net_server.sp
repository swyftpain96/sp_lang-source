console.show("Starting Networking Server Test...")

define server_task = () => {
    console.show("Spawned background server task...")
    http.serve(8081, (req) => {
        console.show("Server received request: " + req.method + " " + req.path)
        if req.path == "/hello" {
            return "World from SP!"
        }
        if req.path == "/json" {
            return {
                status: 200,
                body: {
                    message: "Success",
                    timestamp: 123456
                }
            }
        }
        if req.path == "/stop" {
            exit(0)
        }
        return { status: 404, body: "Not Found" }
    })
}

// Start server in background
set server_fut = async server_task()
process.run("sleep", ["2"]) // Give server time to start

// Test Client fetching from local server
console.show("\nFetching from local server (GET /hello)...")
set res1 = net.get("http://localhost:8081/hello")
console.show("Local Status: " + res1.status)
console.show("Local Body: " + res1.body)

console.show("\nFetching from local server (GET /json)...")
set res2 = net.get("http://localhost:8081/json")
console.show("Local Status: " + res2.status)
set data2 = res2.json()
console.show("JSON Message: " + data2.message)

console.show("\nStopping background server...")
net.get("http://localhost:8081/stop")

console.show("\nNetworking Server Test Finished.")
