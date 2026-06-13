console.show("Starting Networking Client Test (JSONPlaceholder)...")

// Test HTTP GET
console.show("Fetching from jsonplaceholder.typicode.com (GET)...")
set res = net.get("https://jsonplaceholder.typicode.com/posts/1")
console.show("Status: " + res.status)
if (res.status == 200) {
    set data = res.json()
    console.show("Title: " + data.title)
}

// Test HTTP POST
console.show("\nCreating post (POST)...")
set newPost = {
    title: "foo",
    body: "bar",
    userId: 1
}
set postRes = net.post("https://jsonplaceholder.typicode.com/posts", newPost)
console.show("POST Status: " + postRes.status)
if (postRes.status == 201) {
    set postData = postRes.json()
    console.show("Created ID: " + postData.id)
}

console.show("\nNetworking Client Test Finished.")
