console.show("Server starting on 8081...")
http.serve(8081, (req) => {
    console.show("Request received: " + req.path)
    return "Hello from SP Server!"
})
