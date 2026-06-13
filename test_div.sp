define divide(a, b) => { 
    if (b == 0) {
        return [null, "err"]
    }
    else {
        return [a/b, null]
    }
}
console.show(divide(10, 2))
