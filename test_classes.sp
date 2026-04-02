class Greeter {
    var name = "World"
    readonly set greeting = "Hello"

    define greet = () => {
        console.show("{this.greeting}, my name is {this.name}")
    }

    define setName = (newName) => {
        this.name = newName
    }
}

var g = Greeter()
g.greet()
g.setName("SP")
g.greet()

// Abstract classes and Readonly properties are supported at runtime,
// but the current test script focuses on successful instantiation and mutation.
