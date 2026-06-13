http.serve(8082, (req) => {
    console.show("Method: " + req.method)
    console.show("Path: " + req.path)
    console.show("Query: " + req.query)
    console.show("Headers Status: " + (req.headers != null))
    return "ok"
})
after 1000 {
    net.get("http://localhost:8082/test?a=1")
    exit(0)
}
