console.show("--- SQLite CRUD Test ---")

set dbPath = "test_crud.db"
set db = sqlite.open(dbPath)

console.show("1. Creating table...")
db.execute("DROP TABLE IF EXISTS employees")
db.execute("CREATE TABLE employees (id INTEGER PRIMARY KEY, name TEXT, salary REAL, department TEXT)")

console.show("2. Inserting data...")
db.execute("INSERT INTO employees (name, salary, department) VALUES (?, ?, ?)", "Alice", 75000, "Engineering")
db.execute("INSERT INTO employees (name, salary, department) VALUES (?, ?, ?)", "Bob", 60000, "Marketing")
db.execute("INSERT INTO employees (name, salary, department) VALUES (?, ?, ?)", "Charlie", 85000, "Engineering")

console.show("3. Querying data...")
set allEmployees = db.query("SELECT * FROM employees ORDER BY salary DESC")
console.show("Total employees:", allEmployees.length)

for emp in allEmployees {
    console.show("Employee: {emp.name}, Salary: {emp.salary}, Dept: {emp.department}")
}

console.show("4. Parameter binding query...")
set engineering = db.query("SELECT name FROM employees WHERE department = ?", "Engineering")
console.show("Engineering employees:", engineering.length)

console.show("5. Updating data...")
set updatedRows = db.execute("UPDATE employees SET salary = salary * 1.1 WHERE name = ?", "Bob")
console.show("Rows updated:", updatedRows)

set bobAfter = db.query("SELECT salary FROM employees WHERE name = ?", "Bob")
console.show("Bob's new salary:", bobAfter[0].salary)

console.show("6. Deleting data...")
set deletedRows = db.execute("DELETE FROM employees WHERE name = ?", "Charlie")
console.show("Rows deleted:", deletedRows)

set finalCount = db.query("SELECT count(*) as count FROM employees")
console.show("Final count:", finalCount[0].count)

console.show("7. Closing database...")
db.close()

console.show("--- Test Complete ---")
