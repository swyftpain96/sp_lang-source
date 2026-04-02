/**
 * Adds two numbers.
 * @param {number} a The first number
 * @param {number} b The second number
 * @return {number} The sum
 */
define add = (a, b) => {
    return a + b
}

/**
 * Greets a person.
 * @param {string} name The person's name
 * @return {string} The greeting
 */
define greet = (name) => {
    return "Hello, " + name
}

set x = add(1, 2)
set y = greet("World")

show_info(x)
show_info(y)

define show_info = (val) => {
    console.show(val)
}
