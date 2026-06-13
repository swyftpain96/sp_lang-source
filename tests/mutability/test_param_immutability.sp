define foo = (x) => {
    x = 10 // Should fail as parameters are currently immutable by default
}
foo(20)
