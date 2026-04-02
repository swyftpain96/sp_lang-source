// This is a single-line comment
set x = 10 // comment after code

/* 
   This is a 
   multi-line comment 
*/
set y = 20

/* single line block comment */
define test = () => {
    /* comment inside function */
    console.show("Counting to 5:")
    // range(1, 6) returns [1, 2, 3, 4, 5]
    for i in range(1, 6) {
        console.show(i)
    }
}

test()
