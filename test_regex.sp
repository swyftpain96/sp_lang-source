console.show("--- Classic Regex ---")
set r1 = regex("^\d{3}-\d{4}$")
console.show("Pattern r1:", r1)
console.show("Test 123-4567:", r1.test("123-4567"))
console.show("Test 123-456:", r1.test("123-456"))

console.show("--- Builder Regex ---")
set r2 = regex.start().digit().repeat(3).text("-").digit().repeat(4).end()
console.show("Pattern r2:", r2)
console.show("Test 123-4567:", r2.test("123-4567"))
console.show("Test abc-1234:", r2.test("abc-1234"))

console.show("--- New Builder Methods ---")
set r3_new = regex.word().oneOrMore().whitespace().word().oneOrMore()
console.show("Pattern r3_new:", r3_new)
console.show("Test 'hello world':", r3_new.test("hello world"))
console.show("Test '123_456 abc':", r3_new.test("123_456 abc"))

set r4_new = regex.start().nonDigit().oneOrMore().end()
console.show("Pattern r4_new:", r4_new)
console.show("Test 'abc':", r4_new.test("abc"))
console.show("Test '123':", r4_new.test("123"))

console.show("--- String Integration: match ---")
set s = "The price is $19.99 and $5.00"
set r3 = regex.text("$").digit().oneOrMore().text(".").digit().repeat(2)
console.show("Matches:", s.match(r3))

console.show("--- String Integration: replace (All) ---")
set s2 = "I have 12 apples and 45 oranges"
console.show("Replaced all digits:", s2.replace(regex("\d+"), "MANY"))

console.show("--- Pattern Matching ---")
set choices = ["user_123", "admin_panel", "guest", "123-4567"]
for choice in choices {
    set result = match choice {
        regex("^user_\d+$"): "User ID detected",
        regex("^admin_.*"): "Admin detected",
        regex("^\d{3}-\d{4}$"): "Phone number detected",
        default: "Unknown"
    }
    console.show("Choice:", choice, "=> Result:", result)
}

console.show("--- Complex Builder with Capture ---")
// Matching a date like 2023-04-03
set r_date = regex.start().capture(regex.digit().repeat(4)).text("-").capture(regex.digit().repeat(2)).text("-").capture(regex.digit().repeat(2)).end()
console.show("Date Pattern:", r_date)
set my_date = "2023-04-03"
console.show("Test 2023-04-03:", r_date.test(my_date))
console.show("Match groups:", my_date.match(r_date))
