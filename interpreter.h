#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "ast.h"
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>
#include <chrono>
#include <unordered_set>

class Interpreter;
class VM;

#include <mutex>

class Environment {
public:
    Environment() : enclosing(nullptr) {}
    Environment(std::shared_ptr<Environment> enclosing) : enclosing(enclosing) {}
    Environment(size_t size, std::shared_ptr<Environment> enclosing)
        : values(size), enclosing(enclosing) {}

    void define(const std::string& name, Value value) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        namedValues[name] = std::move(value);
    }

    void assign(const std::string& name, Value val) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        auto it = namedValues.find(name);
        if (it != namedValues.end()) {
            it->second = std::move(val);
            return;
        }
        if (enclosing) {
            enclosing->assign(name, std::move(val));
            return;
        }
        throw std::runtime_error("Undefined variable: " + name);
    }

    Value get(const std::string& name) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        auto it = namedValues.find(name);
        if (it != namedValues.end()) {
            return it->second;
        }
        if (enclosing) {
            return enclosing->get(name);
        }
        throw std::runtime_error("Undefined variable: " + name);
    }

    Value& getAt(int depth, int index) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        Environment* env = this;
        for (int i = 0; i < depth; ++i) {
            if (!env->enclosing) throw std::runtime_error("Environment depth exceeded");
            env = env->enclosing.get();
        }
        if (index >= (int)env->values.size()) {
            static Value undefined;
            return undefined;
        }
        return env->values[index];
    }

    void assignAt(int depth, int index, Value val) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        Environment* env = this;
        for (int i = 0; i < depth; ++i) {
            if (!env->enclosing) throw std::runtime_error("Environment depth exceeded");
            env = env->enclosing.get();
        }
        if (index >= (int)env->values.size()) env->values.resize(index + 1);
        env->values[index] = std::move(val);
    }

    void defineAt(int index, Value val) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        if (index >= (int)values.size()) {
            values.resize(index + 1);
        }
        values[index] = std::move(val);
    }

    bool has(const std::string& name) {
        std::lock_guard<std::recursive_mutex> lock(envMutex);
        if (namedValues.find(name) != namedValues.end()) return true;
        if (enclosing) return enclosing->has(name);
        return false;
    }

    static std::recursive_mutex envMutex;

private:
    std::vector<Value> values;
    std::unordered_map<std::string, Value> namedValues;
    std::shared_ptr<Environment> enclosing;
};

class ReturnException : public std::exception {
public:
    Value value;
    ReturnException(Value val) : value(std::move(val)) {}
};

class SpFunction : public ICallable {
public:
    FunctionDeclaration declaration;
    std::shared_ptr<Environment> closure;
    Value boundInstance;

    SpFunction(FunctionDeclaration decl, std::shared_ptr<Environment> closure, Value bound = Value())
        : declaration(std::move(decl)), closure(std::move(closure)), boundInstance(bound) {}
    Value call(Interpreter& interpreter, const std::vector<Value>& args) override;
};

class Interpreter {
public:
    class VM* vm = nullptr; // Forward declare VM
    Interpreter(class VM* vm = nullptr);
    Interpreter(std::shared_ptr<Environment> env, class VM* vm = nullptr);
    void interpret(const std::vector<Statement>& statements);

    Value evaluate(std::shared_ptr<Expression> expr);
    void execute(const Statement& stmt);
    std::string* makeString(const std::string& s);
    int64_t* makeBigInt(int64_t v);
    DateData* makeDate(double ts);
    MapData* makeMap();
    Value makeRegex(const std::string& pattern, const std::string& lastPart = "", bool isGlobal = false);

    std::shared_ptr<Environment> environment;
    std::unordered_map<std::string, Value> currentExports;
    std::unordered_map<std::string, void*> nativeModules;
    bool isExtractingExports = false;
    std::vector<std::string> cliArgs;
    std::function<Value(ICallable*, const std::vector<Value>&)> callHandler;

    std::vector<std::pair<std::string, Value>>* makeObject();
    std::vector<Value>* makeArray();
    SpClass* makeClass(const std::string& name, bool isAbstract);
    ICallable* makeFunction(std::shared_ptr<ICallable> f);
    void executeDestructuring(const std::vector<DestructuringBinding>& bindings, Value val);
};

#endif