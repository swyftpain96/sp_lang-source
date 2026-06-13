# SP Language Guide

Welcome to the **SP Programming Language**! SP is a modern and expressive language designed for simplicity and performance. Whether you are a beginner or an experienced developer, this guide will help you get started.

---

## Table of Contents
1. [Getting Started](#getting-started)
2. [Variables & Mutability](#variables--mutability)
3. [Data Types](#data-types)
4. [Gradual Typing (Optional Types)](#gradual-typing-optional-types)
5. [Layouts & Structural Typing](#layouts--structural-typing)
6. [Control Flow](#control-flow)
7. [Functions & Closures](#functions--closures)
8. [Destructuring & Spread](#destructuring--spread)
9. [The Pipeline Operator (`|>`)](#the-pipeline-operator)
10. [Module System](#module-system)
11. [Type Assertions (`as`)](#type-assertions-as)
12. [Native Addon Types (`.spd`)](#native-addon-types-spd)
13. [Error Handling](#error-handling)
12. [Async Futures](#async-futures)
13. [Networking & Web](#networking--web)
14. [Standard Library](#standard-library)
15. [Object-Oriented Programming (Classes)](#object-oriented-programming)
16. [Regex & Pattern Matching](#regex--pattern-matching)
17. [SQLite Database](#sqlite-database)
18. [Async Storage](#async-storage)

---

## Getting Started

To run an SP script, use the `sp` executable:

```bash
./sp hello.sp
```

### Your First Script: Hello World
Create a file named `hello.sp` and add the following:

```sp
console.show("Hello, World!")
```

---

## Variables & Mutability

SP distinguishes between constants and mutable variables.

- **`set`**: Creates an **immutable** variable (a constant). Once set, it cannot be changed.
- **`var`**: Creates a **mutable** variable. Use this if you need to update the value later.

```sp
set name = "Alice"   // This cannot be changed
var age = 25         // This can be updated
age = 26             // Works!
```

---

## Data Types

SP supports several built-in types:

- **Numbers**: `10`, `3.14`
- **Strings**: `"Hello"`, `"Value is: {x}"` (Suppports interpolation using `{ }`)
- **Booleans**: `true`, `false`
- **BigInts**: `1000000000000000000n` (Suffix with `n`)
- **Arrays**: `[1, 2, 3]`
- **Objects**: `{name: "Bob", age: 30}`
- **Null & Undefined**: `null`, `undefined`

---

## Gradual Typing (Optional Types)

SP is a **gradually typed** language. This means you can choose to omit types for flexibility or add them for safety and clarity. Types are enforced at **runtime**.

### Type Annotations
You can add type annotations to variables using the `: type` syntax.

```sp
var count: number = 42
var name: string = "Alice"
var isActive: boolean = true
var items: array = [1, 2, 3]
```

### Function Types
Functions can also have types for their parameters and return values.

```sp
// Explicit parameter types and return type
define add = (a: number, b: number): number => a + b

// Grouped parameter types
define greet = (name, title: string): string => {
    return "Hello, {title} {name}"
}
```

### Union Types
Use the `|` operator to allow a variable to hold multiple types.

```sp
var id: string | number = 101
id = "UID-202" // Valid
```

### Generics
SP supports basic generics for built-in container types like `Array`.

```sp
var scores: Array<number> = [95, 80, 100]
var names: Array<string> = ["Alice", "Bob"]
```

---

## Layouts & Structural Typing

**Layouts** allow you to define the structure of objects. Unlike classes, they are purely structural and focus on the shape of the data.

### Defining a Layout
```sp
layout Point {
    x: number,
    y: number
}

layout User {
    id: number,
    name: string
}
```

### Intersection Types (`&`)
You can compose layouts using the intersection operator `&` to create new types that combine properties.

```sp
layout Named = { name: string }
layout Aged = { age: number }

layout Person = Named & Aged
// Person now requires both 'name' and 'age'
```

### Inline Structural Types
You don't always need to define a named layout. You can use structural literals directly in function signatures.

```sp
define move = (p: { x: number, y: number }, dx, dy) => {
    p.x = p.x + dx
    p.y = p.y + dy
}
```

---

## Type Methods

SP provides built-in methods for core data types to make manipulation easy and expressive.

### 🧵 String Methods
- `s.trim()`: Removes whitespace from both ends.
- `s.toLowerCase()` / `s.toUpperCase()`: Converts casing.
- `s.contains(sub)` / `s.includes(sub)`: Checks for a substring.
- `s.startsWith(sub)` / `s.endsWith(sub)`: Prefix/Suffix checks.
- `s.indexOf(sub)`: Returns the first index of `sub`, or `-1`.
- `s.split(sep)`: Splits the string into an array.
- `s.substring(start, end)`: Extracts a portion of the string.
- `s.match(regex)`: Returns an array of matches (and capture groups).
- `s.replace(regex|string, to)`: Replaces occurrences of a pattern. (See Regex section for `.global()`).
- `s.padStart(len, char)`: Pads the string at the start.
- `s.repeat(n)`: Repeats the string `n` times.

### 📦 Array Methods
- `arr.push(val)` / `arr.pop()`: Add/remove from end.
- `arr.unshift(val)` / `arr.shift()`: Add/remove from beginning.
- `arr.length`: Returns the number of elements.
- `arr.join(sep)`: Joins elements into a string.
- `arr.reverse()`: Reverses the array **in-place**.
- `arr.slice(start, end)`: Returns a shallow copy of a portion.
- `arr.contains(val)` / `arr.includes(val)`: Checks for value.
- `arr.indexOf(val)`: Returns index of value.
- `arr.find(callback)`: Returns the first element that satisfies the test.

#### ⚡ Functional Methods
- `arr.map(callback)`: Returns a new array with transformed items.
- `arr.filter(callback)`: Returns a new array with items that pass a test.
- `arr.forEach(callback)`: Executes a function for each element.

### 🗝️ Object Methods
- `obj.keys()`: Returns an array of property names.
- `obj.values()`: Returns an array of property values.
- `obj.has(key)`: Checks if a property exists.

### 🔢 Number Methods
- `n.toFixed(digits)`: Returns a string with fixed decimal points.
- `n.toString()`: Converts the number to a string.

#### 🔗 Method Chaining Example
You can chain methods together for concise and expressive code:
```sp
set result = "  Hello, world!  ".trim().toUpperCase().replace("WORLD", "SP")
console.show(result) // Outputs: HELLO, SP!
```

---

## Control Flow

### If / Else
`if` can be used as a statement or an expression.

```sp
set x = 10
if x > 5 {
    console.show("Big")
} else {
    console.show("Small")
}

// As an expression:
set status = if x > 5 { "Active" } else { "Inactive" }
```

### Match Expression (Pattern Matching)
A cleaner way to handle multiple conditions.

```sp
set choice = 2
set name = match choice {
    1: "One",
    2: "Two",
    default: "Unknown"
}
```

### Loops
- **While**: `while condition { ... }`
- **For-In**: `for item in collection { ... }`

```sp
var i = 0
while i < 5 {
    console.show(i)
    i = i + 1
}

for n in [10, 20, 30] {
    console.show("Number: {n}")
}
```

---

## Functions & Closures

### Defining Functions
Use the `define` keyword to create named functions.

```sp
define add = (a, b) => a + b

define greet = (name) => {
    console.show("Hello, {name}!")
}
```

### Closures
Functions can capture variables from their outer scope.

```sp
define makeAdder = (x) => {
    return (y) => x + y
}
set add5 = makeAdder(5)
console.show(add5(10)) // Outputs 15
```

### Implicit Returns
Functions in SP return the value of the last expression in their block if no `return` is used.
```sp
define multiply = (a, b) => a * b // Implicitly returns a * b
```

### Rest Parameters
Use the spread operator `...` to capture a variable number of arguments into an array.

```sp
define sumAll = (...numbers) => {
    var total = 0
    for n in numbers { total = total + n }
    return total
}
console.show(sumAll(1, 2, 3, 4)) // Outputs 10
```

---

## Destructuring & Spread

SP provides powerful syntax for unpacking values from arrays and objects.

### Array Destructuring
```sp
set arr = [1, 2, 3, 4]
set [first, second, ...rest] = arr

console.show(first)  // 1
console.show(second) // 2
console.show(rest)   // [3, 4]
```

### Object Destructuring
You can extract properties and even rename them.
```sp
set user = { name: "Alice", age: 25, role: "admin" }
set { name, age: years, ...others } = user

console.show(name)   // "Alice"
console.show(years)  // 25
console.show(others) // { role: "admin" }
```

### Nested Destructuring
```sp
set data = { 
    meta: { status: 200 }, 
    items: ["apple", "banana"] 
}
set { meta: { status }, items: [firstItem] } = data

console.show(status)    // 200
console.show(firstItem) // "apple"
```

### Spread Operators
Use `...` to expand arrays or objects into new ones.
```sp
set defaults = { theme: "dark", notify: true }
set settings = { ...defaults, notify: false, user: "Alice" }

set base = [2, 3]
set full = [1, ...base, 4]
```

### Call Spread
You can also use spread when calling functions.
```sp
define logPoints = (x, y, z) => console.show(x, y, z)
set pos = [10, 20, 30]
logPoints(...pos)
```

---

## The Pipeline Operator

The pipe operator `|>` allows you to chain function calls in a readable left-to-right fashion.

```sp
define double = (x) => x * 2
define log = (x) => console.show("Result: {x}")

// Without pipes: log(double(10))
// With pipes:
10 |> double |> log
```

### Placeholders (`_`)
If a function takes multiple arguments, use `_` to represent the piped value.

```sp
define greet = (greeting, name) => console.show("{greeting}, {name}!")

"Alice" |> greet("Hello", _) // Outputs: Hello, Alice!
```

---

## Module System

SP uses a simple and flexible module system to organize code.

### Importing Modules
Use the `use` keyword to bring in other files.

```sp
// Standard import (namespaced)
use math
console.show(math.add(5, 10))

// With alias
use math as m
console.show(m.add(2, 3))
```

### Named Imports
You can import specific members directly into the current scope.

```sp
use { add, pi } from math
console.show(add(1, 2))

// With aliases
use { add as plus, pi as PI } from math
```

### Exporting
To make functions or variables available to other files, use the `export` keyword.

```sp
// math.sp
export set pi = 3.14159
export define add = (a, b) => a + b
```

---

## Type Assertions (`as`)

You can cast values to specific types using the `as` operator. This is useful when working with `any` types or when you want to enforce a specific structure.

```sp
set userProfile = { name: "John", isAdmin: true } as UserProfile
```

You can also use dotted names for type assertions:

```sp
use db_addon
set user = get_user() as db_addon.UserProfile
```

## Runtime Type Checking (`typeof`)

The `typeof` operator allows you to check the type of a value at runtime. it returns a `string` representing the type.

```sp
set val = 42
set t = typeof val // "number"

if typeof val == "number" {
    console.show("Value is a number!")
}
```

Possible return values for `typeof`:
- `"number"` (includes BigInts)
- `"string"`
- `"boolean"`
- `"array"`
- `"function"` (includes Classes and Methods)
- `"null"`
- `"undefined"`
- `"error"`
- `"regex"`
- `"future"`
- `"map"`
- `"timer"`
- `"object"` (includes instances of classes)

---

## Native Addon Types (`.spd`)

Native addons (written in C++ as `.so` files) can provide type definitions to the SP ecosystem using `.spd` (**SP Definition**) files. These files are similar to TypeScript's `.d.ts` files.

An `.spd` file contains `layout` and `define` statements without full implementation bodies, serving as a contract for the native module.

### Example: `database.spd`
```sp
layout QueryResult {
    rows: Array<object>
    count: number
}

export define connect(path: string): { query: (sql: string) => QueryResult } => {
    // Body can be mock or empty; it's use for type discovery
}
```

### Automatic Exports
Unlike standard `.sp` files, all declarations (layouts, functions, etc.) within an `.spd` file are **automatically exported**. You do not need to use the `export` keyword inside an `.spd` file, although it is supported.

### Usage
When you `use database`, the SP environment will look for `database.spd` to infer types for the native module. Layouts defined in the `.spd` file become members of the module object:

```sp
use database
var data: database.QueryResult // Referencing the layout via the module name
```

---

## Error Handling

SP emphasizes functional error handling and provides built-in support for capturing failures.

### Functional Error Pattern
Many operations return a `[result, error]` pair. You can destructure this to handle errors gracefully.

```sp
define safeDivide = (a, b) => {
    if b == 0 { return [null, "Division by zero"] }
    return [a / b, null]
}

var [res, err] = safeDivide(10, 0)
if err {
    console.show("Error occurred: {err}")
} else {
    console.show("Result: {res}")
}
```

### The `Error` Object
Use the `Error` constructor to create structured error data.
```sp
var myErr = Error("Something went wrong", 500)
console.show(myErr.message) // "Something went wrong"
console.show(myErr.code)    // 500
```

### `.error()` Callback
Typed variables provide a `.error()` method to catch runtime type mismatches.

```sp
var x: number = "not a number"
x.error((e) => {
    console.show("Caught type error: {e.message}")
})
```

---

## Async Futures

SP uses **Futures** to handle values that will be available later. These are returned by `async` operations and network requests.

### Waiting for Results
Use the `.wait()` method to synchronously pause the current thread and get the value from a future.

```sp
set fut = async {
    process.sleep(1000)
    return "Done!"
}
set result = fut.wait()
console.show(result)
```

### Implicit Unwrapping
One of SP's most powerful features is **implicit unwrapping**. If you access a property on a future, it automatically waits for the result before proceeding.

```sp
set userFut = async { return { name: "Alice" } }
console.show(userFut.name) // Automatically waits and prints "Alice"
```

### Error Handling
Futures provide an `.error()` method to handle failures that occur during asynchronous execution.

```sp
set fut = async fs.read("missing.txt")
fut.error((e) => console.show("Async error: {e}"))
```

---

## Networking & Web

The `net` (also aliased as `http`) module provides tools for building web clients and servers.

### Making Requests
You can easily make HTTP GET and POST requests.

```sp
// GET Request
set res = net.get("https://api.example.com/data")
console.show("Status: {res.status}")

// POST Request with JSON
set newUser = { name: "Bob" }
set postRes = net.post("https://api.example.com/users", newUser)
```

### The Response Object
Network requests return a `Response` object with the following:
- `.status`: The HTTP status code (e.g., 200, 404).
- `.body`: The raw response body as a string.
- `.headers`: An object containing the response headers.
- `.json()`: A method that parses the body as JSON and returns an SP object.

### Building a Server
You can start an HTTP server with `net.serve`.

```sp
net.serve(8080, (req) => {
    console.show("Received {req.method} request for {req.path}")
    
    return {
        status: 200,
        body: { message: "Hello from SP!" },
        headers: { "X-Powered-By": "SP" }
    }
})
```

### The Request Object
The server handler receives a `Request` object:
- `.method`: The HTTP method (GET, POST, etc.).
- `.path`: The request path.
- `.headers`: Request headers.
- `.query`: URL query parameters.
- `.body`: The raw request body.

---

## Concurrency & Timing

SP provides modern, easy-to-use constructs for handling asynchronous tasks and time-based logic.

### ⚡ Async Tasks
Use `async` to run a block of code in the background without blocking the main thread.

```sp
async {
    console.show("Starting heavy task...")
    process.sleep(2000)
    console.show("Task complete!")
}
console.show("Main thread is still running!")
```

### ⏱️ Delayed Execution (`after`)
Execute a block of code once after a specified delay in milliseconds.

```sp
after 1000 {
    console.show("One second has passed!")
}
```

### 🔄 Repeating Timers (`every`)
Execute a block of code repeatedly at a specified interval. It returns a `Timer` object that can be stopped.

```sp
var count = 0
set timer = every 500 {
    count = count + 1
    console.show("Tick {count}")
    
    if count == 10 {
        timer.stop()
        console.show("Timer stopped.")
    }
}
```

### 😴 Synchronous Sleep
If you need to pause the current thread execution for a specific duration, use `process.sleep(ms)`.

```sp
console.show("Wait a moment...")
process.sleep(1500) // Sleep for 1.5 seconds
console.show("Continue!")
```

---

## Standard Library

### `console`
- `console.show(args...)`: Prints values to the screen.
- `console.read()`: Reads a line of text from the user.

### `fs` (File System)
- `fs.read(path)`: Returns file content as a string.
- `fs.create(path, content)`: Creates a new file with the specified content.
- `fs.overwrite(path, content, options?)`: Overwrites a file. You can specify a `{ line: number }` in options to only overwrite a specific line.
- `fs.append(path, content, options?)`: Appends content to a file. You can specify a `{ line: number }` in options to append after a specific line (moving rest of file down).
- `fs.delete(path)`: Deletes a file.
- `fs.info(path)`: Returns an object with file details (size, exists, etc.).
- `fs.readJson(path)`: Reads a JSON file and parses it into an SP object.
- `fs.writeJson(path, content)`: Converts an SP object to JSON and writes it to a file.

### `math`
- Use `use math` to access math functions like `math.add(a, b)`.
- Global functions: `floor(n)`, `time()`.

### 🌐 Global Functions
These functions are available globally in every script without needing an import.

#### `time()`
Returns the current unix timestamp in **milliseconds**. This is useful for benchmarking or calculating durations.
```sp
set start = time()
// ... do some work ...
set end = time()
console.show("Took: {end - start}ms")
```

#### `floor(n)`
Rounds a number **down** to the nearest integer.
```sp
console.show(floor(10.7)) // Outputs: 10
console.show(floor(5.2))  // Outputs: 5
```

### `Map`
SP provides a `Map` (also aliased as `HashMap`) for efficient key-value storage.

```sp
set myMap = Map()
myMap.set("key", "value")
console.show(myMap.get("key"))

// Advanced: size, delete, clear, forEach
myMap.size // 1
myMap.delete("key")
myMap.clear()
myMap.forEach((k, v) => console.show("{k}: {v}"))

// Note: HashMap is an alias for Map.
```

### `Date`
Use the `Date` object to work with time.

```sp
set today = Date.now()
console.show("Current timestamp: {today}")

// Properties: year, month, day, hour, minute, second
console.show(today.year)
```

### `process`
- `process.run(cmd, args)`: Executes a command and waits for it to finish.
- `process.spawn(cmd, args)`: Starts a background process.

---

## Object-Oriented Programming

SP supports classes with modern features like access modifiers.

```sp
class Person {
    readonly name           // Read-only property
    private secret          // Private property
    var age                 // Mutable property

    define init = (name, secret, age) => {
        this.name = name
        this.secret = secret
        this.age = age
    }

    define sayHello = () => {
        console.show("Hi, I'm {this.name}")
    }
}

set bob = Person("Bob", "TopSecret", 30)
bob.sayHello()
```

### Abstract Classes
Use `abstract class` for base classes that shouldn't be instantiated directly. Properties can also be marked as `readonly` or `private`.

---

## Regex & Pattern Matching

SP features a powerful, chainable **Regex Builder API** that makes complex patterns readable and easy to maintain.

### 🧩 Creating a Regex
You can create a regex using a literal string or the builder:

```sp
// Literal Regex
set r1 = regex("^\d{3}-\d{4}$")

// Builder Regex (Equivalent)
set r2 = regex.start().digit().repeat(3).text("-").digit().repeat(4).end()
```

### 🛠️ Builder Methods
Builder methods can be chained to construct patterns step-by-step:

- **Character Classes**: `.digit()`, `.nonDigit()`, `.word()`, `.letter()`, `.whitespace()`, `.any()`.
- **Anchors**: `.start()`, `.end()`, `.wordBoundary()`.
- **Literals**: `.text("string")`, `.range("a", "z")`.
- **Quantifiers**: 
    - `.maybe()` / `.optional()` (0 or 1)
    - `.oneOrMore()` (1+)
    - `.zeroOrMore()` (0+)
    - `.repeat(n)` / `.repeat(min, max)`
    - `.repeatAtLeast(min)`
- **Groups**: `.capture(inner)`, `.group(inner)`.
- **Logic**: `.or(other)`.

### 🌍 Global Flag
By default, regex replacement only affects the **first** occurrence. Use `.global()` to replace all matches:

```sp
set s = "123-456-789"
console.show(s.replace(regex("\d"), "X"))          // Outputs: X23-456-789
console.show(s.replace(regex("\d").global(), "X")) // Outputs: XXX-XXX-XXX
```

### 🔍 String Integration
- **`s.match(regex)`**: Returns an array of matches. If capturing groups are used, it returns an array containing the full match followed by the groups.
- **`s.replace(regex, to)`**: Replaces matches in the string. Honors the `.global()` flag.

### 🎯 Pattern Matching with Regex
The `match` expression natively supports regex objects as patterns:

```sp
set input = "user_123"
set type = match input {
    regex("^user_\d+$"): "User ID",
    regex("^admin_.*"):  "Admin Access",
    default:             "Guest"
}
console.show(type) // Outputs: User ID
```

---

## SQLite Database

The `sqlite` module provides full CRUD (Create, Read, Update, Delete) capabilities for local database management.

### Opening a Connection
Use `sqlite.open(path)` to connect to a database file. If the file doesn't exist, it will be created.

```sp
set db = sqlite.open("app.db")
```

### Executing Commands
Use `.execute(sql, ...params)` for statements that perform actions but don't return data (like `CREATE`, `INSERT`, `UPDATE`, or `DELETE`). It returns the number of rows affected.

```sp
// Create a table
db.execute("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT)")

// Insert data with parameter binding
set rowsAdded = db.execute("INSERT INTO users (name) VALUES (?)", "Alice")
console.show("Added {rowsAdded} user.")
```

### Querying Data
Use `.query(sql, ...params)` for `SELECT` statements. It returns an **array of objects**, where each object represents a row.

```sp
set users = db.query("SELECT * FROM users WHERE name = ?", "Alice")

for user in users {
    console.show("User: {user.name} (ID: {user.id})")
}
```

### Closing the Connection
It is good practice to close the connection when you are done, although SP will automatically cleanup handles when the database object is no longer in use.

```sp
db.close()
```

---

## Async Storage

The `storage` module provides a high-level, asynchronous key-value store. It is built on top of SQLite and automatically handles JSON serialization for complex objects and arrays.

### Basic Usage

```sp
use storage

// Storing data (returns a Future)
storage.setItem("user", { name: "Alice", age: 30 })

// Retrieving data (returns a Future, implicitly unwrapped)
set user = storage.getItem("user")
console.show("User name: {user.name}")

// Removing data
storage.removeItem("user")

// Clearing all storage
storage.clear()
```

### Explicit Waiting
While SP automatically unwraps futures, you can use `.wait()` if you need to ensure an operation (like `setItem`) is fully completed before proceeding to the next line of code.

```sp
storage.setItem("theme", "dark").wait()
console.show("Theme saved successfully.")
```

### Automatic JSON Serialization
Unlike SQLite which requires manual parameter binding and query writing, `storage` handles SP objects and arrays natively using `JSON`.

```sp
storage.setItem("settings", {
    theme: "dark",
    notifications: true,
    tags: ["work", "personal"]
})

set settings = storage.getItem("settings")
console.show(settings.tags[0]) // "work"
```

### JSON Global Object
The `JSON` object is also available for manual serialization.

```sp
set str = JSON.stringify({ a: 1 })
set obj = JSON.parse(str)
```

---

Enjoy coding in **SP**! If you find any bugs, deal with it (by reporting to SwyftPain)!
