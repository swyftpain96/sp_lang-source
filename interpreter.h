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

class Environment {
public:
    Environment() : enclosing(nullptr) {}
    Environment(std::shared_ptr<Environment> enclosing) : enclosing(enclosing) {}
    Environment(size_t size, std::shared_ptr<Environment> enclosing)
        : values(size), enclosing(enclosing) {}

    void define(const std::string& name, Value value) {
        namedValues[name] = std::move(value);
    }

    void assign(const std::string& name, Value val) {
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
        Environment* env = this;
        for (int i = 0; i < depth; ++i) env = env->enclosing.get();
        if (index >= (int)env->values.size()) {
            static Value undefined;
            return undefined;
        }
        return env->values[index];
    }

    void assignAt(int depth, int index, Value val) {
        Environment* env = this;
        for (int i = 0; i < depth; ++i) env = env->enclosing.get();
        env->values[index] = std::move(val);
    }

    void defineAt(int index, Value val) {
        if (index >= (int)values.size()) {
            values.resize(index + 1);
        }
        values[index] = std::move(val);
    }

    bool has(const std::string& name) {
        if (namedValues.find(name) != namedValues.end()) return true;
        if (enclosing) return enclosing->has(name);
        return false;
    }

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
    void interpret(const std::vector<Statement>& statements);

    Value evaluate(std::shared_ptr<Expression> expr);
    void execute(const Statement& stmt);
    std::string* makeString(const std::string& s);
    int64_t* makeBigInt(int64_t v);
    DateData* makeDate(double ts);
    MapData* makeMap();

    std::shared_ptr<Environment> environment;
    std::unordered_map<std::string, Value> currentExports;
    std::unordered_map<std::string, void*> nativeModules;
    std::vector<std::shared_ptr<std::string>> allStrings;
    std::vector<std::shared_ptr<std::vector<std::pair<std::string, Value>>>> allObjects;
    std::vector<std::shared_ptr<std::vector<Value>>> allArrays;
    std::vector<std::shared_ptr<int64_t>> allBigInts;
    std::vector<std::shared_ptr<DateData>> allDates;
    std::vector<std::shared_ptr<MapData>> allMaps;
    std::vector<std::shared_ptr<SpClass>> allClasses;
    std::vector<std::shared_ptr<ICallable>> allFunctions;
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