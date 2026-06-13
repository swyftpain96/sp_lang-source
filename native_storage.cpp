#include "vm.h"
#include "interpreter.h"
#include <sqlite3.h>
#include <future>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

// Helper to wrap sqlite3* handle
struct StorageSqliteDeleter {
    void operator()(sqlite3* db) const {
        if (db) {
            sqlite3_close(db);
        }
    }
};

using StoragePtr = std::shared_ptr<sqlite3>;

static StoragePtr globalStorageDb;
static std::recursive_mutex storageMutex;

// Internal helpers from types.h / types.cpp
extern std::string stringifyJSON(const Value& val, int indent);
extern Value parseJSONValue(const std::string& json, size_t& pos, Interpreter& interpreter);

static void ensureStorageInitialized() {
    std::lock_guard<std::recursive_mutex> lock(storageMutex);
    if (globalStorageDb) return;

    sqlite3* db;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(".storage.db", &db, flags, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Could not open .storage.db");
    }
    
    globalStorageDb = StoragePtr(db, StorageSqliteDeleter());
    
    char* err;
    if (sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS kv_store (key TEXT PRIMARY KEY, value TEXT)", nullptr, nullptr, &err) != SQLITE_OK) {
        std::string errMsg = err;
        sqlite3_free(err);
        throw std::runtime_error("Storage init error: " + errMsg);
    }
}

void registerStorageModule(VM& vm, Interpreter& interp) {
    auto setItem = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
        if (args.size() < 2 || !args[0].isString()) throw std::runtime_error("storage.setItem requires key and value.");
        ensureStorageInitialized();

        std::string key = *args[0].asString();
        std::string valStr = stringifyJSON(args[1]);

        auto fut = std::make_shared<std::future<Value>>(
            std::async(std::launch::async, [key, valStr]() -> Value {
                std::lock_guard<std::recursive_mutex> lock(storageMutex);
                sqlite3_stmt* stmt;
                const char* sql = "INSERT OR REPLACE INTO kv_store (key, value) VALUES (?, ?)";
                if (sqlite3_prepare_v2(globalStorageDb.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    return Value(Type::NULL_VAL); // Or return an Error
                }
                sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, valStr.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                return Value(Type::NULL_VAL);
            })
        );
        auto futData = std::make_shared<FutureData>(std::move(fut));
        Value::registerFuture(futData);
        return Value(futData.get());
    });
    Value::registerFunction(setItem);

    auto getItem = std::make_shared<NativeFunction>([](Interpreter& outerInterp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("storage.getItem requires a key.");
        ensureStorageInitialized();

        std::string key = *args[0].asString();
        auto sharedState = outerInterp.vm->sharedState;

        auto fut = std::make_shared<std::future<Value>>(
            std::async(std::launch::async, [key, sharedState]() -> Value {
                std::lock_guard<std::recursive_mutex> lock(storageMutex);
                sqlite3_stmt* stmt;
                const char* sql = "SELECT value FROM kv_store WHERE key = ?";
                if (sqlite3_prepare_v2(globalStorageDb.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    return Value(Type::NULL_VAL);
                }
                sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
                
                Value result(Type::NULL_VAL);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    std::string valStr = (const char*)sqlite3_column_text(stmt, 0);
                    
                    // We need an interpreter to parse JSON
                    VM threadVm(sharedState);
                    Interpreter threadInterp(&threadVm);
                    size_t pos = 0;
                    try {
                        result = parseJSONValue(valStr, pos, threadInterp);
                    } catch(...) {
                        result = Value(Type::NULL_VAL);
                    }
                }
                sqlite3_finalize(stmt);
                return result;
            })
        );
        auto futData = std::make_shared<FutureData>(std::move(fut));
        Value::registerFuture(futData);
        return Value(futData.get());
    });
    Value::registerFunction(getItem);

    auto removeItem = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("storage.removeItem requires a key.");
        ensureStorageInitialized();

        std::string key = *args[0].asString();
        auto fut = std::make_shared<std::future<Value>>(
            std::async(std::launch::async, [key]() -> Value {
                std::lock_guard<std::recursive_mutex> lock(storageMutex);
                sqlite3_stmt* stmt;
                const char* sql = "DELETE FROM kv_store WHERE key = ?";
                if (sqlite3_prepare_v2(globalStorageDb.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
                    return Value(Type::NULL_VAL);
                }
                sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                return Value(Type::NULL_VAL);
            })
        );
        auto futData = std::make_shared<FutureData>(std::move(fut));
        Value::registerFuture(futData);
        return Value(futData.get());
    });
    Value::registerFunction(removeItem);

    auto clear = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>&) {
        ensureStorageInitialized();
        auto fut = std::make_shared<std::future<Value>>(
            std::async(std::launch::async, []() -> Value {
                std::lock_guard<std::recursive_mutex> lock(storageMutex);
                sqlite3_exec(globalStorageDb.get(), "DELETE FROM kv_store", nullptr, nullptr, nullptr);
                return Value(Type::NULL_VAL);
            })
        );
        auto futData = std::make_shared<FutureData>(std::move(fut));
        Value::registerFuture(futData);
        return Value(futData.get());
    });
    Value::registerFunction(clear);

    auto* storageObj = interp.makeObject();
    storageObj->push_back({"setItem", Value(setItem.get(), true)});
    storageObj->push_back({"getItem", Value(getItem.get(), true)});
    storageObj->push_back({"removeItem", Value(removeItem.get(), true)});
    storageObj->push_back({"clear", Value(clear.get(), true)});

    Value storageVal(storageObj);
    vm.defineGlobal("__native_storage__", storageVal);
    interp.environment->define("__native_storage__", storageVal);
}
