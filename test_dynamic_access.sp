set a = "testone"

set me = {
    testone: "one",
    testtwo: "two"
}

console.show("Reading testone via me[a]:", me[a])
me["testtwo"] = "modified_two"
console.show("Reading modified testtwo:", me.testtwo)

set arr = [1, 2, 3]
arr[3] = 4
console.show("Reading arr:", arr)

set m = Map()
m.set("k", "v")
console.show("Map original:", m.get("k"))
m["k"] = "modified_v"
console.show("Map dynamic:", m["k"])
