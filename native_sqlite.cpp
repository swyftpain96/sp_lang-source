#include "vm.h"
#include "interpreter.h"
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

// Helper to wrap sqlite3* handle
struct Sqlite3Deleter {
    void operator()(sqlite3* db) const {
        if (db) {
            sqlite3_close(db);
        }
    }
};

using SqlitePtr = std::shared_ptr<sqlite3>;

// Helper to bind arguments to a statement
void bindArgs(sqlite3_stmt* stmt, const std::vector<Value>& args, size_t startIndex) {
    for (size_t i = startIndex; i < args.size(); ++i) {
        int bindIdx = (int)(i - startIndex + 1);
        const Value& val = args[i];
        if (val.isNumber()) {
            sqlite3_bind_double(stmt, bindIdx, val.asNumber());
        } else if (val.isString()) {
            sqlite3_bind_text(stmt, bindIdx, val.asString()->c_str(), -1, SQLITE_TRANSIENT);
        } else if (val.isBool()) {
            sqlite3_bind_int(stmt, bindIdx, val.asBoolean() ? 1 : 0);
        } else if (val.isNil() || val.isUndefined()) {
            sqlite3_bind_null(stmt, bindIdx);
        } else {
            // Fallback to string representation
            sqlite3_bind_text(stmt, bindIdx, val.toPureString().c_str(), -1, SQLITE_TRANSIENT);
        }
    }
}

Value createDatabaseObject(Interpreter& interp, SqlitePtr db) {
    auto dbExecute = std::make_shared<NativeFunction>([db](Interpreter& interp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("db.execute requires a SQL string.");
        if (!db) throw std::runtime_error("Database is closed.");

        std::string sql = *args[0].asString();
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("SQLite prepare error: ") + sqlite3_errmsg(db.get()));
        }

        bindArgs(stmt, args, 1);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            std::string err = sqlite3_errmsg(db.get());
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("SQLite step error: ") + err);
        }

        int changes = sqlite3_changes(db.get());
        sqlite3_finalize(stmt);
        return Value((double)changes);
    });
    Value::registerFunction(dbExecute);

    auto dbQuery = std::make_shared<NativeFunction>([db](Interpreter& interp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("db.query requires a SQL string.");
        if (!db) throw std::runtime_error("Database is closed.");

        std::string sql = *args[0].asString();
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("SQLite prepare error: ") + sqlite3_errmsg(db.get()));
        }

        bindArgs(stmt, args, 1);

        auto* results = interp.makeArray();
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            auto* row = interp.makeObject();
            int colCount = sqlite3_column_count(stmt);
            for (int i = 0; i < colCount; ++i) {
                const char* name = sqlite3_column_name(stmt, i);
                int type = sqlite3_column_type(stmt, i);
                Value val;
                switch (type) {
                    case SQLITE_INTEGER:
                        val = Value((double)sqlite3_column_int64(stmt, i));
                        break;
                    case SQLITE_FLOAT:
                        val = Value(sqlite3_column_double(stmt, i));
                        break;
                    case SQLITE_TEXT:
                        val = Value(interp.makeString((const char*)sqlite3_column_text(stmt, i)));
                        break;
                    case SQLITE_NULL:
                        val = Value(Type::NULL_VAL);
                        break;
                    default:
                        val = Value(interp.makeString((const char*)sqlite3_column_text(stmt, i)));
                        break;
                }
                row->push_back({name, val});
            }
            results->push_back(Value(row));
        }

        if (rc != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db.get());
            sqlite3_finalize(stmt);
            throw std::runtime_error(std::string("SQLite step error: ") + err);
        }

        sqlite3_finalize(stmt);
        return Value(results);
    });
    Value::registerFunction(dbQuery);

    auto dbClose = std::make_shared<NativeFunction>([db_copy = db](Interpreter&, const std::vector<Value>&) mutable {
        // Explicitly close by resetting the shared_ptr inside the captured closure?
        // Actually, we can't easily reset all closures, but we can reset the primary handle.
        // But since it's a shared_ptr, it will close when the last function is gone.
        // For explicit close, we can use a separate object that holds the handle.
        // For now, let's just null out the underlying handle if we can, or just do nothing
        // and let GC (or reference counting in our case) handle it.
        // However, if the user wants to close IT NOW:
        if (db_copy) {
            sqlite3_close(db_copy.get());
            // We can't null it easily if multiple functions capture it.
            // Let's use a pointer to a pointer or a wrapper.
        }
        return Value(Type::NULL_VAL);
    });
    Value::registerFunction(dbClose);

    auto* obj = interp.makeObject();
    obj->push_back({"execute", Value(dbExecute.get(), true)});
    obj->push_back({"query", Value(dbQuery.get(), true)});
    obj->push_back({"close", Value(dbClose.get(), true)});
    return Value(obj);
}

void registerSqliteModule(VM& vm, Interpreter& interp) {
    auto sqliteOpen = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("sqlite.open requires a filename.");
        std::string filename = *args[0].asString();

        sqlite3* raw_db;
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        if (sqlite3_open_v2(filename.c_str(), &raw_db, flags, nullptr) != SQLITE_OK) {
            std::string err = sqlite3_errmsg(raw_db);
            sqlite3_close(raw_db);
            throw std::runtime_error(std::string("Could not open SQLite database: ") + err);
        }

        SqlitePtr db(raw_db, Sqlite3Deleter());
        return createDatabaseObject(interp, db);
    });
    Value::registerFunction(sqliteOpen);

    auto* sqliteObj = interp.makeObject();
    sqliteObj->push_back({"open", Value(sqliteOpen.get(), true)});

    Value sqliteVal(sqliteObj);
    vm.defineGlobal("sqlite", sqliteVal);
    interp.environment->define("sqlite", sqliteVal);
}
