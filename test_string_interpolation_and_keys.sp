set name = 'alice'
set label = 'v1'
set obj = {version: 1, name: name}

console.show('single quote: {name}')
console.show("method call: {name.toUpperCase()}")
console.show("method with single-quoted arg: {name.replace('a', 'A')}")
console.show("nested expression: {({inner: label}).inner.toUpperCase()}")
console.show("keys: {obj.keys().join(',')}")
