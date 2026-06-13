set now = Date.now()
console.show("Current Date:", now)

set year = now.year
set month = now.month
set day = now.day
set hour = now.hour
set minute = now.minute
set second = now.second

console.show("Year:", year)
console.show("Month:", month)
console.show("Day:", day)
console.show("Time:", hour, ":", minute, ":", second)

console.show("Date object:", now)

if (year >= 2024) {
    console.show("Date.year seems correct!")
} else {
    console.show("Date.year seems wrong:", year)
}
