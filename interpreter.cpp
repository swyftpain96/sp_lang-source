#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "vm.h"
#include "builtin_modules.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <fstream>
#include <cmath>
#include <chrono>
#include <functional>
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <filesystem>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#include <libgen.h>
#endif
#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <ctime>
#include <unordered_set>

namespace fs = std::filesystem;

Value SpFunction::call(Interpreter& interpreter, const std::vector<Value>& args) {
    auto env = std::make_shared<Environment>(declaration.localCount, closure);
    if (!boundInstance.isUndefined()) {
        env->define("this", boundInstance);
    }
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
        if (i < args.size()) {
            env->defineAt(i, args[i]);
        } else {
            env->defineAt(i, Value(Type::UNDEFINED));
        }
    }
    
    auto previous = interpreter.environment;
    interpreter.environment = env;
    
    Value result;
    try {
        result = declaration.body->evaluate(interpreter);
    } catch (const ReturnException& e) {
        result = e.value;
    } catch (...) {
        interpreter.environment = previous;
        throw;
    }
    
    interpreter.environment = previous;
    return result;
}

Interpreter::Interpreter(VM* vm) : vm(vm), environment(std::make_shared<Environment>()) {
    Value::CurrentContext = this;
    callHandler = [this](ICallable* c, const std::vector<Value>& args) {
        return c->call(*this, args);
    };
    
    // Built-ins
    auto timeFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>&) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return Value(ms);
    });
    environment->define("time", Value(timeFunc.get()));

    auto floorFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isNumber()) return Value(Type::UNDEFINED);
        return Value(std::floor(args[0].asNumber()));
    });
    environment->define("floor", Value(floorFunc.get()));

    // Date built-in object
    auto* dObj = makeObject();
    dObj->push_back({"now", Value(makeFunction(std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return Value(interp.makeDate(ms));
    }))) });
    dObj->push_back({"parse", Value(makeFunction(std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("Date.parse requires a string.");
        // Simple mock parse for now, implementation later if needed
        return Value(interp.makeDate(0.0));
    }))) });
    environment->define("Date", Value(dObj));

    // Map built-in object
    environment->define("Map", Value(makeFunction(std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
        return Value(interp.makeMap());
    }))));
    environment->define("HashMap", environment->get("Map"));
}

std::string* Interpreter::makeString(const std::string& s) {
    auto str = std::make_shared<std::string>(s);
    Value::registerString(str);
    return str.get();
}

std::vector<std::pair<std::string, Value>>* Interpreter::makeObject() {
    auto o = std::make_shared<std::vector<std::pair<std::string, Value>>>();
    Value::registerObject(o);
    return o.get();
}

std::vector<Value>* Interpreter::makeArray() {
    auto a = std::make_shared<std::vector<Value>>();
    Value::registerArray(a);
    return a.get();
}
SpClass* Interpreter::makeClass(const std::string& name, bool isAbstract) {
    auto cl = std::make_shared<SpClass>(name, isAbstract);
    allClasses.push_back(cl);
    return cl.get();
}

int64_t* Interpreter::makeBigInt(int64_t v) {
    auto b = std::make_shared<int64_t>(v);
    Value::registerBigInt(b);
    return b.get();
}

DateData* Interpreter::makeDate(double ms) {
    auto d = std::make_shared<DateData>(ms);
    Value::registerDate(d);
    return d.get();
}

MapData* Interpreter::makeMap() {
    auto m = std::make_shared<MapData>();
    Value::registerMap(m);
    return m.get();
}

ICallable* Interpreter::makeFunction(std::shared_ptr<ICallable> f) {
    allFunctions.push_back(f);
    return f.get();
}


void Interpreter::interpret(const std::vector<Statement>& statements) {
    for (const auto& stmt : statements) {
        execute(stmt);
    }
}

void Interpreter::execute(const Statement& stmt) {
    auto* prev = Value::CurrentContext;
    Value::CurrentContext = this;
    try {
        if (std::holds_alternative<VariableDeclaration>(stmt)) {
        const auto& decl = std::get<VariableDeclaration>(stmt);
        auto value = evaluate(decl.value);
        if (decl.index != -1) {
            environment->defineAt(decl.index, std::move(value));
        } else {
            environment->define(decl.name, std::move(value));
        }
    } else if (std::holds_alternative<FunctionDeclaration>(stmt)) {
        const auto& decl = std::get<FunctionDeclaration>(stmt);
        Value funcVal(makeFunction(std::make_shared<SpFunction>(decl, environment)));
        if (decl.index != -1) {
            environment->defineAt(decl.index, std::move(funcVal));
        } else {
            environment->define(decl.name, std::move(funcVal));
        }
    } else if (std::holds_alternative<PrintStatement>(stmt)) {
        const auto& print = std::get<PrintStatement>(stmt);
        for (size_t i = 0; i < print.exprs.size(); ++i) {
            auto value = evaluate(print.exprs[i]);
            std::cout << value.toString();
            if (i < print.exprs.size() - 1) std::cout << " ";
        }
        std::cout << std::endl;
    } else if (std::holds_alternative<ReturnStatement>(stmt)) {
        const auto& returnStmt = std::get<ReturnStatement>(stmt);
        Value val;
        if (returnStmt.value) {
            val = evaluate(returnStmt.value);
        }
        throw ReturnException(std::move(val));
    } else if (std::holds_alternative<UseStatement>(stmt)) {
        const auto& useStmt = std::get<UseStatement>(stmt);
        if (useStmt.moduleName == "fs" || useStmt.moduleName == "console" || useStmt.moduleName == "process") {
            return;
        }
        std::string filename = useStmt.moduleName + ".sp";
        std::string source;
        bool found = false;

        if (builtinModules.find(useStmt.moduleName) != builtinModules.end()) {
            source = builtinModules[useStmt.moduleName];
            found = true;
        } else {
            std::ifstream file(filename);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                source = buffer.str();
                found = true;
            }
        }

        if (!found) {
            throw std::runtime_error("Could not find module: " + useStmt.moduleName);
        }
        
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto modStmts = parser.parse();
        
        auto prevEnv = environment;
        auto prevExports = currentExports;
        bool prevExtracting = isExtractingExports;
        
        environment = std::make_shared<Environment>();
        currentExports.clear();
        isExtractingExports = true;
        
        interpret(modStmts);
        
        std::unordered_map<std::string, Value> exports = currentExports;
        
        environment = prevEnv;
        currentExports = prevExports;
        isExtractingExports = prevExtracting;
        
        if (useStmt.isNamed) {
            // use { foo, bar as b } from module  — bind each export directly
            for (const auto& [exportName, localAlias] : useStmt.namedImports) {
                auto it = exports.find(exportName);
                if (it == exports.end()) {
                    throw std::runtime_error("Module '" + useStmt.moduleName
                        + "' does not export '" + exportName + "'");
                }
                environment->define(localAlias, it->second);
            }
        } else {
            // use module  /  use module as alias
            auto* exportedObjProps = makeObject();
            for (const auto& pair : exports) {
                exportedObjProps->emplace_back(pair.first, pair.second);
            }
            Value moduleObj(exportedObjProps);
            std::string bindName = useStmt.alias.empty() ? useStmt.moduleName : useStmt.alias;
            environment->define(bindName, std::move(moduleObj));
        }
    } else if (std::holds_alternative<ExportStatement>(stmt)) {
        const auto& expStmt = std::get<ExportStatement>(stmt);
        if (std::holds_alternative<VariableDeclaration>(expStmt.declaration)) {
            const auto& decl = std::get<VariableDeclaration>(expStmt.declaration);
            execute(decl);
            if (isExtractingExports) {
                currentExports[decl.name] = environment->get(decl.name);
            }
        } else if (std::holds_alternative<FunctionDeclaration>(expStmt.declaration)) {
            const auto& decl = std::get<FunctionDeclaration>(expStmt.declaration);
            execute(decl);
            if (isExtractingExports) {
                currentExports[decl.name] = environment->get(decl.name);
            }
        }
    } else if (std::holds_alternative<WhileStatement>(stmt)) {
        const auto& whileStmt = std::get<WhileStatement>(stmt);
        while (true) {
            auto condVal = evaluate(whileStmt.condition);
            bool isTrue = (condVal.isBoolean()) ? condVal.asBoolean() : (!condVal.isNil() && !condVal.isUndefined());
            if (!isTrue) break;
            
            evaluate(whileStmt.body);
        }
    } else if (std::holds_alternative<ForStatement>(stmt)) {
        const auto& forStmt = std::get<ForStatement>(stmt);
        auto colVal = evaluate(forStmt.collection);
        if (!colVal.isArray()) {
            throw std::runtime_error("For loop collection must be an array");
        }
        
        auto* array = colVal.asArray();
        for (const auto& item : *array) {
            auto prevEnv = environment;
            environment = std::make_shared<Environment>(forStmt.localCount, prevEnv);
            
            if (forStmt.index != -1) {
                environment->defineAt(forStmt.index, item);
            } else {
                environment->define(forStmt.variableName, item);
            }
            
            try {
                evaluate(forStmt.body);
            } catch (...) {
                environment = prevEnv;
                throw;
            }
            environment = prevEnv;
        }
    } else if (std::holds_alternative<ClassDeclaration>(stmt)) {
        const auto& decl = std::get<ClassDeclaration>(stmt);
        auto* klass = makeClass(decl.name, decl.isAbstract);
        klass->properties = decl.properties;
        for (const auto& method : decl.methods) {
            klass->methods[method.name] = Value(makeFunction(std::make_shared<SpFunction>(method, environment)));
        }
        Value classVal(klass);
        environment->define(decl.name, classVal);
    } else if (std::holds_alternative<DestructuringDeclaration>(stmt)) {
        const auto& decl = std::get<DestructuringDeclaration>(stmt);
        Value val = evaluate(decl.initializer);
        executeDestructuring(decl.bindings, val);
    } else if (std::holds_alternative<std::shared_ptr<Expression>>(stmt)) {
        evaluate(std::get<std::shared_ptr<Expression>>(stmt));
    }
    } catch (...) {
        Value::CurrentContext = prev;
        throw;
    }
    Value::CurrentContext = prev;
}

Value Interpreter::evaluate(std::shared_ptr<Expression> expr) {
    if (!expr) return Value();
    auto* prev = Value::CurrentContext;
    Value::CurrentContext = this;
    Value val;
    try {
        val = expr->evaluate(*this);
    } catch (...) {
        Value::CurrentContext = prev;
        throw;
    }
    Value::CurrentContext = prev;
    return val;
}

Value LiteralExpression::evaluate(Interpreter& interpreter) {
    (void)interpreter;
    return value;
}

Value BigIntLiteralExpression::evaluate(Interpreter& interpreter) {
    return Value(interpreter.makeBigInt(std::stoll(value)));
}

Value IdentifierExpression::evaluate(Interpreter& interpreter) {
    if (depth != -1) {
        return interpreter.environment->getAt(depth, index);
    }
    return interpreter.environment->get(name);
}

Value ThisExpression::evaluate(Interpreter& interpreter) {
    if (depth != -1) {
        return interpreter.environment->getAt(depth, index);
    }
    return interpreter.environment->get("this");
}

Value AssignmentExpression::evaluate(Interpreter& interpreter) {
    auto val = value->evaluate(interpreter);
    if (depth != -1) {
        interpreter.environment->assignAt(depth, index, val);
    } else {
        interpreter.environment->assign(name, val);
    }
    return val;
}

Value MemberAssignmentExpression::evaluate(Interpreter& interpreter) {
    fprintf(stderr, "[DEBUG] MemberAssignmentExpression::evaluate ENTERED\n");
    fflush(stderr);
    auto objVal = object->evaluate(interpreter);
    auto val = value->evaluate(interpreter);
    
    if (objVal.isInstance()) {
        auto* instance = objVal.asInstance();
        for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
            if (instance->klass->properties[i].name == property) {
                if (instance->klass->properties[i].isReadonly) {
                    throw std::runtime_error("Cannot assign to readonly property '" + property + "'");
                }
                instance->fields[i] = val;
                return val;
            }
        }
        throw std::runtime_error("Property '" + property + "' not found in class '" + instance->klass->name + "'");
    } else if (objVal.isObject()) {
        auto& obj = *objVal.asObject();
        fprintf(stdout, "[DEBUG] MemberAssignment object ptr=%p, prop=%s, newVal=%s\n", (void*)&obj, property.c_str(), val.toPureString().c_str());
        fflush(stdout);
        bool found = false;
        for (auto& pair : obj) {
            if (pair.first == property) {
                fprintf(stdout, "[DEBUG]   Found existing property %s, oldVal=%s\n", property.c_str(), pair.second.toPureString().c_str());
                pair.second = val;
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stdout, "[DEBUG]   Adding new property %s\n", property.c_str());
            obj.emplace_back(property, val);
        }
        fflush(stdout);
        return val;
    }
    throw std::runtime_error("Member assignment on non-object/instance");
}

Value MemberExpression::evaluate(Interpreter& interpreter) {
    // std::cerr << "Evaluating member access: " << property << std::endl;
    auto objVal = object->evaluate(interpreter);
    if (objVal.isArray()) {
        auto* arr = objVal.asArray();
        if (property == "length") return Value((double)arr->size());
        if (property == "push") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                for (const auto& arg : args) arr->push_back(arg);
                return Value((double)arr->size());
            })));
        }
        if (property == "pop") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>&) {
                if (arr->empty()) return Value(Type::NULL_VAL);
                Value val = arr->back();
                arr->pop_back();
                return val;
            })));
        }
        if (property == "shift") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>&) {
                if (arr->empty()) return Value(Type::NULL_VAL);
                Value val = (*arr)[0];
                arr->erase(arr->begin());
                return val;
            })));
        }
        if (property == "unshift") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                arr->insert(arr->begin(), args.begin(), args.end());
                return Value((double)arr->size());
            })));
        }
        if (property == "join") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                std::string sep = (args.empty() || !args[0].isString()) ? "," : *args[0].asString();
                std::string res = "";
                for (size_t i = 0; i < arr->size(); ++i) {
                    res += (*arr)[i].toPureString();
                    if (i < arr->size() - 1) res += sep;
                }
                return Value(interp.makeString(res));
            })));
        }
        if (property == "reverse") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr, objVal](Interpreter&, const std::vector<Value>&) {
                std::reverse(arr->begin(), arr->end());
                return objVal;
            })));
        }
        if (property == "slice") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr, &interpreter](Interpreter& interp, const std::vector<Value>& args) {
                size_t start = (args.size() > 0 && args[0].isNumber()) ? (size_t)args[0].asNumber() : 0;
                size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : arr->size();
                if (start > arr->size()) start = arr->size();
                if (end > arr->size()) end = arr->size();
                if (start > end) std::swap(start, end);
                auto* newArr = interp.makeArray();
                for (size_t i = start; i < end; ++i) newArr->push_back((*arr)[i]);
                return Value(newArr);
            })));
        }
        if (property == "contains" || property == "includes") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(false);
                for (const auto& item : *arr) {
                    if ((item == args[0]).asBoolean()) return Value(true);
                }
                return Value(false);
            })));
        }
        if (property == "indexOf") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(-1.0);
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (((*arr)[i] == args[0]).asBoolean()) return Value((double)i);
                }
                return Value(-1.0);
            })));
        }
        if (property == "forEach") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("forEach requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (size_t i = 0; i < arr->size(); ++i) {
                    interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                }
                return Value(Type::NULL_VAL);
            })));
        }
        if (property == "map") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("map requires a function callback.");
                ICallable* callback = args[0].asFunction();
                auto* newArr = interp.makeArray();
                for (size_t i = 0; i < arr->size(); ++i) {
                    newArr->push_back(interp.callHandler(callback, {(*arr)[i], Value((double)i)}));
                }
                return Value(newArr);
            })));
        }
        if (property == "filter") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("filter requires a function callback.");
                ICallable* callback = args[0].asFunction();
                auto* newArr = interp.makeArray();
                for (size_t i = 0; i < arr->size(); ++i) {
                    Value res = interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                    bool isTrue = (res.isBoolean()) ? res.asBoolean() : (!res.isNil() && !res.isUndefined());
                    if (isTrue) newArr->push_back((*arr)[i]);
                }
                return Value(newArr);
            })));
        }
    }
    if (objVal.isString()) {
        auto* s = objVal.asString();
        if (property == "length" || property == "size") return Value((double)s->length());
        if (property == "trim") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                res.erase(0, res.find_first_not_of(" \t\n\r\f\v"));
                size_t last = res.find_last_not_of(" \t\n\r\f\v");
                if (last != std::string::npos) res.erase(last + 1);
                else if (!res.empty() && isspace(res[0])) res.clear();
                return Value(interp.makeString(res));
            })));
        }
        if (property == "toLowerCase") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                for (auto& c : res) c = std::tolower(c);
                return Value(interp.makeString(res));
            })));
        }
        if (property == "toUpperCase") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                for (auto& c : res) c = std::toupper(c);
                return Value(interp.makeString(res));
            })));
        }
        if (property == "contains" || property == "includes") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                return Value(s->find(*args[0].asString()) != std::string::npos);
            })));
        }
        if (property == "startsWith") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                const std::string& sub = *args[0].asString();
                return Value(s->compare(0, sub.length(), sub) == 0);
            })));
        }
        if (property == "endsWith") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                const std::string& sub = *args[0].asString();
                if (sub.length() > s->length()) return Value(false);
                return Value(s->compare(s->length() - sub.length(), sub.length(), sub) == 0);
            })));
        }
        if (property == "indexOf") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(-1.0);
                auto pos = s->find(*args[0].asString());
                return Value(pos == std::string::npos ? -1.0 : (double)pos);
            })));
        }
        if (property == "split") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                std::string sep = (args.empty() || !args[0].isString()) ? "" : *args[0].asString();
                auto* arr = interp.makeArray();
                if (sep.empty()) {
                    for (char c : *s) arr->push_back(Value(interp.makeString(std::string(1, c))));
                } else {
                    size_t last = 0;
                    size_t next = 0;
                    while ((next = s->find(sep, last)) != std::string::npos) {
                        arr->push_back(Value(interp.makeString(s->substr(last, next - last))));
                        last = next + sep.length();
                    }
                    arr->push_back(Value(interp.makeString(s->substr(last))));
                }
                return Value(arr);
            })));
        }
        if (property == "replace") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.size() < 2 || !args[0].isString() || !args[1].isString()) return Value(interp.makeString(*s));
                std::string res = *s;
                const std::string& from = *args[0].asString();
                const std::string& to = *args[1].asString();
                size_t start_pos = res.find(from);
                if (start_pos != std::string::npos) {
                    res.replace(start_pos, from.length(), to);
                }
                return Value(interp.makeString(res));
            })));
        }
        if (property == "substring") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                size_t start = (size_t)args[0].asNumber();
                size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : s->length();
                if (start > s->length()) start = s->length();
                if (end > s->length()) end = s->length();
                if (start > end) std::swap(start, end);
                return Value(interp.makeString(s->substr(start, end - start)));
            })));
        }
    }
    if (objVal.isDate()) {
        auto* d = objVal.asDate();
        time_t tt = (time_t)(d->timestamp / 1000);
        struct tm* tm_info = localtime(&tt);
        if (property == "year") return Value((double)(tm_info->tm_year + 1900));
        if (property == "month") return Value((double)(tm_info->tm_mon + 1));
        if (property == "day") return Value((double)tm_info->tm_mday);
        if (property == "hour") return Value((double)tm_info->tm_hour);
        if (property == "minute") return Value((double)tm_info->tm_min);
        if (property == "second") return Value((double)tm_info->tm_sec);
        if (property == "toString") {
             return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([d](Interpreter& interp, const std::vector<Value>&) {
                 time_t ttt = (time_t)(d->timestamp / 1000);
                 struct tm* tm_inf = localtime(&ttt);
                 char buf[64];
                 strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_inf);
                 return Value(interp.makeString(buf));
             })));
        }
    }
    if (objVal.isMap()) {
        auto* m = objVal.asMap();
        if (property == "size") return Value((double)m->map.size());
        if (property == "set") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m, objVal](Interpreter&, const std::vector<Value>& args) {
                if (args.size() < 2) throw std::runtime_error("Map.set requires 2 arguments.");
                m->map[args[0]] = args[1];
                return objVal; // Chainable
            })));
        }
        if (property == "get") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) throw std::runtime_error("Map.get requires 1 argument.");
                auto it = m->map.find(args[0]);
                if (it == m->map.end()) return Value(Type::NULL_VAL);
                return it->second;
            })));
        }
        if (property == "has") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) throw std::runtime_error("Map.has requires 1 argument.");
                return Value(m->map.find(args[0]) != m->map.end());
            })));
        }
        if (property == "delete") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) throw std::runtime_error("Map.delete requires 1 argument.");
                return Value(m->map.erase(args[0]) > 0);
            })));
        }
        if (property == "clear") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                m->map.clear();
                return Value(Type::UNDEFINED);
            })));
        }
        if (property == "keys") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>&) {
                auto* arr = interp.makeArray();
                for (auto const& [key, val] : m->map) arr->push_back(key);
                return Value(arr);
            })));
        }
        if (property == "values") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>&) {
                auto* arr = interp.makeArray();
                for (auto const& [key, val] : m->map) arr->push_back(val);
                return Value(arr);
            })));
        }
        if (property == "forEach") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("Map.forEach requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (auto const& [key, val] : m->map) {
                    interp.callHandler(callback, {key, val});
                }
                return Value(Type::UNDEFINED);
            })));
        }
    }
    if (objVal.isNumber()) {
        double n = objVal.asNumber();
        if (property == "toFixed") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([n](Interpreter& interp, const std::vector<Value>& args) {
                int digits = (args.size() > 0 && args[0].isNumber()) ? (int)args[0].asNumber() : 0;
                std::stringstream ss;
                ss << std::fixed << std::setprecision(digits) << n;
                return Value(interp.makeString(ss.str()));
            })));
        }
        if (property == "toString") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([objVal](Interpreter& interp, const std::vector<Value>&) {
                return Value(interp.makeString(objVal.toPureString()));
            })));
        }
    }
    if (objVal.isObject() && !objVal.isInstance()) {
        auto* obj = objVal.asObject();
        if (property == "keys") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([obj](Interpreter& interp, const std::vector<Value>&) {
                auto* res = interp.makeArray();
                for (const auto& pair : *obj) res->push_back(Value(interp.makeString(pair.first)));
                return Value(res);
            })));
        }
        if (property == "values") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([obj](Interpreter& interp, const std::vector<Value>&) {
                auto* res = interp.makeArray();
                for (const auto& pair : *obj) res->push_back(pair.second);
                return Value(res);
            })));
        }
        if (property == "has") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([obj](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                const std::string& key = *args[0].asString();
                for (const auto& pair : *obj) if (pair.first == key) return Value(true);
                return Value(false);
            })));
        }
    }
    if (objVal.isInstance()) {
        auto* instance = objVal.asInstance();
        // Search in properties
        for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
            if (instance->klass->properties[i].name == property) {
                return instance->fields[i];
            }
        }
        // Search in methods
        auto it = instance->klass->methods.find(property);
        if (it != instance->klass->methods.end()) {
            Value methodVal = it->second;
            if (methodVal.isFunction()) {
                return Value(interpreter.makeFunction(std::make_shared<BoundMethod>(methodVal.asFunction(), objVal)));
            }
            return methodVal;
        }
        throw std::runtime_error("Member '" + property + "' not found in class '" + instance->klass->name + "'");
    }
    if (!objVal.isObject()) {
        throw std::runtime_error("Member access on non-object");
    }
    const auto& obj = *objVal.asObject();
    std::cout << "[DEBUG] MemberExpression obj=" << &obj << " prop=" << property << std::endl;
    for (const auto& pair : obj) {
        if (pair.first == property) {
            return pair.second;
        }
    }
    throw std::runtime_error("Property '" + property + "' not found");
}

Value IndexExpression::evaluate(Interpreter& interpreter) {
    auto objVal = object->evaluate(interpreter);
    if (!objVal.isArray()) {
        throw std::runtime_error("Index access on non-array");
    }
    auto indexVal = index->evaluate(interpreter);
    if (!indexVal.isNumber()) {
        throw std::runtime_error("Array index must be a number");
    }
    double d = indexVal.asNumber();
    if (d < 0 || std::floor(d) != d) {
        throw std::runtime_error("Invalid array index");
    }
    size_t idx = static_cast<size_t>(d);
    const auto& arr = *objVal.asArray();
    if (idx >= arr.size()) {
        throw std::runtime_error("Array index out of bounds");
    }
    return arr[idx];
}

Value MatchExpression::evaluate(Interpreter& interpreter) {
    auto matchVal = valueToMatch->evaluate(interpreter);
    for (const auto& matchCase : cases) {
        if (!matchCase.pattern) {
            return matchCase.body->evaluate(interpreter);
        }
        auto patternVal = matchCase.pattern->evaluate(interpreter);
        bool eq = (matchVal.bits == patternVal.bits);
        if (!eq && matchVal.isNumber() && patternVal.isNumber()) {
            eq = (matchVal.asNumber() == patternVal.asNumber());
        }
        if (eq) {
            return matchCase.body->evaluate(interpreter);
        }
    }
    return Value();
}

Value BinaryExpression::evaluate(Interpreter& interpreter) {
    auto leftVal = left->evaluate(interpreter);
    
    if (op == "&&") {
        if (!leftVal.isBoolean()) throw std::runtime_error("Logical AND requires boolean");
        if (!leftVal.asBoolean()) return Value(false);
        auto rightVal = right->evaluate(interpreter);
        if (!rightVal.isBoolean()) throw std::runtime_error("Logical AND requires boolean");
        return Value(rightVal.asBoolean());
    } else if (op == "||") {
        if (!leftVal.isBoolean()) throw std::runtime_error("Logical OR requires boolean");
        if (leftVal.asBoolean()) return Value(true);
        auto rightVal = right->evaluate(interpreter);
        if (!rightVal.isBoolean()) throw std::runtime_error("Logical OR requires boolean");
        return Value(rightVal.asBoolean());
    } else if (op == "|>") {
        Value leftVal = left->evaluate(interpreter);
        
        // Always provide _ in the environment for nested expressions
        auto previousEnv = interpreter.environment;
        interpreter.environment = std::make_shared<Environment>(previousEnv);
        interpreter.environment->define("_", leftVal);
        
        Value result;
        try {
            if (auto callExpr = dynamic_cast<CallExpression*>(right.get())) {
                auto calleeVal = callExpr->callee->evaluate(interpreter);
                if (!calleeVal.isFunction()) {
                    throw std::runtime_error("Right side of |> must be a function");
                }
                auto* callable = calleeVal.asFunction();
                
                std::vector<Value> args;
                for (const auto& argExpr : callExpr->arguments) {
                    if (auto idExpr = dynamic_cast<IdentifierExpression*>(argExpr.get())) {
                        if (idExpr->name == "_") {
                            args.push_back(leftVal);
                            continue;
                        }
                    }
                    args.push_back(argExpr->evaluate(interpreter));
                }
                result = callable->call(interpreter, args);
            } else {
                Value rightVal = right->evaluate(interpreter);
                if (rightVal.isFunction()) {
                    result = rightVal.asFunction()->call(interpreter, {leftVal});
                } else {
                    result = rightVal;
                }
            }
            interpreter.environment = previousEnv; // Restore
        } catch (...) {
            interpreter.environment = previousEnv; // Restore even on error
            throw;
        }
        return result;
    }

    auto rightVal = right->evaluate(interpreter);

    if (op == "==" || op == "!=") {
        bool eq = (leftVal.bits == rightVal.bits);
        if (!eq && leftVal.isNumber() && rightVal.isNumber()) {
            eq = (leftVal.asNumber() == rightVal.asNumber());
        }
        if (!eq && leftVal.isBigInt() && rightVal.isBigInt()) {
            eq = (*leftVal.asBigInt() == *rightVal.asBigInt());
        }
        if (!eq && leftVal.isString() && rightVal.isString()) {
            eq = (*leftVal.asString() == *rightVal.asString());
        }
        if (op == "!=") eq = !eq;
        return Value(eq);
    }

    if (op == "+" && (leftVal.isString() || rightVal.isString())) {
        return Value(interpreter.makeString(leftVal.toString() + rightVal.toString()));
    }

    if (op == "+" && (leftVal.isBigInt() || rightVal.isBigInt())) {
        return leftVal + rightVal;
    }

    if (!leftVal.isNumber() || !rightVal.isNumber()) {
        throw std::runtime_error("Binary arithmetic/comparison operations only supported on numbers");
    }
    double l = leftVal.asNumber();
    double r = rightVal.asNumber();
    
    if (op == "+") return Value(l + r);
    if (op == "-") return Value(l - r);
    if (op == "*") return Value(l * r);
    if (op == "/") {
        if (r == 0) throw std::runtime_error("Division by zero");
        return Value(l / r);
    }
    if (op == "<") {
        if (leftVal.isBigInt() && rightVal.isBigInt()) return Value(*leftVal.asBigInt() < *rightVal.asBigInt());
        return Value(l < r);
    }
    if (op == "<=") {
        if (leftVal.isBigInt() && rightVal.isBigInt()) return Value(*leftVal.asBigInt() <= *rightVal.asBigInt());
        return Value(l <= r);
    }
    if (op == ">") {
        if (leftVal.isBigInt() && rightVal.isBigInt()) return Value(*leftVal.asBigInt() > *rightVal.asBigInt());
        return Value(l > r);
    }
    if (op == ">=") {
        if (leftVal.isBigInt() && rightVal.isBigInt()) return Value(*leftVal.asBigInt() >= *rightVal.asBigInt());
        return Value(l >= r);
    }

    throw std::runtime_error("Unknown operator: " + op);
}

Value ArrayExpression::evaluate(Interpreter& interpreter) {
    auto* vals = interpreter.makeArray();
    for (const auto& e : elements) {
        vals->push_back(e->evaluate(interpreter));
    }
    return Value(vals);
}

Value ObjectExpression::evaluate(Interpreter& interpreter) {
    auto* props = interpreter.makeObject();
    for (const auto& p : properties) {
        props->emplace_back(p.first, p.second->evaluate(interpreter));
    }
    return Value(props);
}

Value UnaryExpression::evaluate(Interpreter& interpreter) {
    auto val = expr->evaluate(interpreter);
    if (op == "!") {
        bool isTrue = (val.isBoolean()) ? val.asBoolean() : (!val.isNil() && !val.isUndefined());
        return Value(!isTrue);
    }
    throw std::runtime_error("Unknown unary operator: " + op);
}

Value CallExpression::evaluate(Interpreter& interpreter) {
    auto calleeVal = callee->evaluate(interpreter);
    if (!calleeVal.isFunction()) throw std::runtime_error("Can only call functions");
    std::vector<Value> evaluatedArgs;
    for (const auto& arg : arguments) evaluatedArgs.push_back(arg->evaluate(interpreter));
    auto* func = calleeVal.asFunction();
    return func->call(interpreter, evaluatedArgs);
}

Value LambdaExpression::evaluate(Interpreter& interpreter) {
    FunctionDeclaration decl;
    decl.name = "lambda";
    decl.parameters = parameters;
    decl.body = body;
    decl.hasRest = hasRest;
    decl.localCount = localCount;
    decl.index = index;
    decl.line = line;
    return Value(interpreter.makeFunction(std::make_shared<SpFunction>(decl, interpreter.environment)));
}

Value IfExpression::evaluate(Interpreter& interpreter) {
    auto conditionVal = condition->evaluate(interpreter);
    bool isTrue = (conditionVal.isBoolean()) ? conditionVal.asBoolean() : (!conditionVal.isNil() && !conditionVal.isUndefined());
    if (isTrue) return thenBranch->evaluate(interpreter);
    else if (elseBranch) return elseBranch->evaluate(interpreter);
    return Value();
}

Value BlockExpression::evaluate(Interpreter& interpreter) {
    auto env = std::make_shared<Environment>(localCount, interpreter.environment);
    auto previous = interpreter.environment;
    interpreter.environment = env;
       Value lastVal;
    
    try {
        for (const auto& stmt : statements) {
            if (std::holds_alternative<VariableDeclaration>(stmt)) {
                const auto& decl = std::get<VariableDeclaration>(stmt);
                auto value = decl.value->evaluate(interpreter);
                if (decl.index != -1) {
                    interpreter.environment->defineAt(decl.index, std::move(value));
                } else {
                    interpreter.environment->define(decl.name, std::move(value));
                }
                lastVal = value;
            } else if (std::holds_alternative<FunctionDeclaration>(stmt)) {
                const auto& decl = std::get<FunctionDeclaration>(stmt);
                Value funcVal(interpreter.makeFunction(std::make_shared<SpFunction>(decl, interpreter.environment)));
                if (decl.index != -1) {
                    interpreter.environment->defineAt(decl.index, std::move(funcVal));
                } else {
                    interpreter.environment->define(decl.name, std::move(funcVal));
                }
                lastVal = funcVal;
            } else if (std::holds_alternative<PrintStatement>(stmt)) {
                const auto& print = std::get<PrintStatement>(stmt);
                Value lastPrintVal;
                for (size_t i = 0; i < print.exprs.size(); ++i) {
                    lastPrintVal = print.exprs[i]->evaluate(interpreter);
                    std::cout << lastPrintVal.toString();
                    if (i < print.exprs.size() - 1) std::cout << " ";
                }
                std::cout << std::endl;
                lastVal = lastPrintVal;
            } else if (std::holds_alternative<std::shared_ptr<Expression>>(stmt)) {
                lastVal = std::get<std::shared_ptr<Expression>>(stmt)->evaluate(interpreter);
            } else {
                interpreter.execute(stmt);
            }
        }
    } catch (...) {
        interpreter.environment = previous;
        throw;
    }
    
    interpreter.environment = previous;
    return lastVal;
}

Value ConsoleArgsExpression::evaluate(Interpreter& interpreter) {
    auto* arr = interpreter.makeArray();
    for (const auto& arg : interpreter.cliArgs) {
        arr->push_back(Value(interpreter.makeString(arg)));
    }
    return Value(arr);
}

Value ConsoleReadExpression::evaluate(Interpreter& interpreter) {
    std::string input;
    if (!std::getline(std::cin, input)) return Value();
    return Value(interpreter.makeString(input));
}

Value ConsoleWarnExpression::evaluate(Interpreter& interpreter) {
    for (size_t i = 0; i < arguments.size(); ++i) {
        auto value = arguments[i]->evaluate(interpreter);
        std::cerr << value.toString();
        if (i < arguments.size() - 1) std::cerr << " ";
    }
    std::cerr << std::endl;
    return Value(Type::UNDEFINED);
}

Value ProcessRunExpression::evaluate(Interpreter& interpreter) {
    Value cmdVal = command->evaluate(interpreter);
    if (!cmdVal.isString()) throw std::runtime_error("process.run requires a string command.");
    std::string cmd = *cmdVal.asString();
    if (!arguments.empty()) {
        Value argsVal = arguments[0]->evaluate(interpreter);
        if (argsVal.isArray()) {
            for (const auto& arg : *argsVal.asArray()) {
                cmd += " " + arg.toPureString();
            }
        }
    }

    std::string output;
    char buffer[128];
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    int status = -1;
    if (pipe) {
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            output += buffer;
        }
        status = pclose(pipe);
#ifdef WEXITSTATUS
        if (status != -1 && WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    }

    auto* obj = interpreter.makeObject();
    obj->push_back({"output", Value(interpreter.makeString(output))});
    obj->push_back({"status", Value((double)status)});
    obj->push_back({"failed", Value(status != 0)});
    return Value(obj);
}

Value ProcessSpawnExpression::evaluate(Interpreter& interpreter) {
    Value cmdVal = command->evaluate(interpreter);
    if (!cmdVal.isString()) throw std::runtime_error("process.spawn requires a string command.");
    std::string cmd = *cmdVal.asString();
    if (!arguments.empty()) {
        Value argsVal = arguments[0]->evaluate(interpreter);
        if (argsVal.isArray()) {
            for (const auto& arg : *argsVal.asArray()) {
                cmd += " " + arg.toPureString();
            }
        }
    }

    std::string commandWithArgs = cmd + " &";
    system(commandWithArgs.c_str());

    auto* obj = interpreter.makeObject();
    obj->push_back({"spawned", Value(true)});
    return Value(obj);
}

Value FSCreateExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    Value contentVal = content->evaluate(interpreter);
    if (!pathVal.isString() || !contentVal.isString()) throw std::runtime_error("fs.create requires string path and content.");
    std::string p = *pathVal.asString();
    std::ifstream check(p);
    if (check.good()) {
        auto* obj = interpreter.makeObject();
        obj->push_back({"error", Value(interpreter.makeString("File already exists"))});
        return Value(obj);
    }
    std::ofstream out(p);
    out << *contentVal.asString();
    out.close();
    return Value(Type::NULL_VAL);
}

Value FSReadExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    if (!pathVal.isString()) throw std::runtime_error("fs.read requires string path.");
    std::ifstream in(*pathVal.asString());
    if (!in.is_open()) return Value(Type::NULL_VAL);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Value(interpreter.makeString(content));
}

Value FSInfoExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    if (!pathVal.isString()) throw std::runtime_error("fs.info requires string path.");
    std::string pathStr = *pathVal.asString();
    
    std::string absPathStr;
    try {
        absPathStr = fs::absolute(pathStr).string();
    } catch (...) {
        absPathStr = pathStr;
    }

    fs::path p(absPathStr);
    std::string dirStr = p.parent_path().string();
    std::string nameStr = p.filename().string();

    std::string extStr = "";
    size_t dotPos = nameStr.find_last_of('.');
    if (dotPos != std::string::npos && dotPos != 0) {
        extStr = nameStr.substr(dotPos);
    }

    struct stat st;
    double size = 0;
    bool exists = (stat(absPathStr.c_str(), &st) == 0);
    if (exists) size = (double)st.st_size;

    auto* obj = interpreter.makeObject();
    obj->push_back({"path", Value(interpreter.makeString(absPathStr))});
    obj->push_back({"dirname", Value(interpreter.makeString(dirStr))});
    obj->push_back({"name", Value(interpreter.makeString(nameStr))});
    obj->push_back({"ext", Value(interpreter.makeString(extStr))});
    obj->push_back({"size", Value(size)});
    obj->push_back({"length", Value(size)});
    obj->push_back({"exists", Value(exists)});
    
    if (exists) {
        obj->push_back({"modifiedAt", Value(interpreter.makeDate((double)st.st_mtime * 1000.0))});
        obj->push_back({"createdAt", Value(interpreter.makeDate((double)st.st_ctime * 1000.0))});
    } else {
        obj->push_back({"modifiedAt", Value(Type::NULL_VAL)});
        obj->push_back({"createdAt", Value(Type::NULL_VAL)});
    }
    
    return Value(obj);
}

Value FSOverwriteExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    Value contentVal = content->evaluate(interpreter);
    if (!pathVal.isString() || !contentVal.isString()) throw std::runtime_error("fs.overwrite requires string path and content.");
    
    std::string p = *pathVal.asString();
    std::string c = *contentVal.asString();

    if (options) {
        Value optVal = options->evaluate(interpreter);
        if (optVal.isObject()) {
            auto* obj = optVal.asObject();
            for (auto& pair : *obj) {
                if (pair.first == "line" && pair.second.isNumber()) {
                    int lineNum = (int)pair.second.asNumber();
                    if (lineNum > 0) {
                        std::vector<std::string> lines;
                        std::string line;
                        std::ifstream in(p);
                        while (std::getline(in, line)) lines.push_back(line);
                        in.close();

                        if (lineNum <= (int)lines.size()) {
                            lines[lineNum - 1] = c;
                        } else {
                            while ((int)lines.size() < lineNum - 1) lines.push_back("");
                            lines.push_back(c);
                        }

                        std::ofstream out(p, std::ios::trunc);
                        for (size_t i = 0; i < lines.size(); ++i) {
                            out << lines[i] << "\n";
                        }
                        out.close();
                        return Value(Type::NULL_VAL);
                    }
                }
            }
        }
    }

    std::ofstream out(p, std::ios::trunc);
    out << c;
    out.close();
    return Value(Type::NULL_VAL);
}

Value FSAppendExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    Value contentVal = content->evaluate(interpreter);
    if (!pathVal.isString() || !contentVal.isString()) throw std::runtime_error("fs.append requires string path and content.");
    
    std::string p = *pathVal.asString();
    std::string c = *contentVal.asString();

    if (options) {
        Value optVal = options->evaluate(interpreter);
        if (optVal.isObject()) {
            auto* obj = optVal.asObject();
            for (auto& pair : *obj) {
                if (pair.first == "line" && pair.second.isNumber()) {
                    int lineNum = (int)pair.second.asNumber();
                    if (lineNum > 0) {
                        std::vector<std::string> lines;
                        std::string line;
                        std::ifstream in(p);
                        while (std::getline(in, line)) lines.push_back(line);
                        in.close();

                        if (lineNum <= (int)lines.size()) {
                            lines.insert(lines.begin() + lineNum - 1, c);
                        } else {
                            while ((int)lines.size() < lineNum - 1) lines.push_back("");
                            lines.push_back(c);
                        }

                        std::ofstream out(p, std::ios::trunc);
                        for (size_t i = 0; i < lines.size(); ++i) {
                            out << lines[i] << "\n";
                        }
                        out.close();
                        return Value(Type::NULL_VAL);
                    }
                }
            }
        }
    }

    std::ofstream out(p, std::ios::app);
    out << c;
    out.close();
    return Value(Type::NULL_VAL);
}


Value TrimExpression::evaluate(Interpreter& interpreter) {
    Value arg = argument->evaluate(interpreter);
    if (!arg.isString()) throw std::runtime_error("string.trim requires a string argument.");
    std::string s = *arg.asString();
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return Value(interpreter.makeString(""));
    size_t last = s.find_last_not_of(" \t\n\r");
    return Value(interpreter.makeString(s.substr(first, (last - first + 1))));
}

Value StringSizeExpression::evaluate(Interpreter& interpreter) {
    Value arg = argument->evaluate(interpreter);
    if (!arg.isString()) throw std::runtime_error("string.size/length requires a string argument.");
    return Value((double)arg.asString()->size());
}

Value FSDeleteExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    if (!pathVal.isString()) throw std::runtime_error("fs.delete requires string path.");
    std::remove(pathVal.asString()->c_str());
    return Value(Type::NULL_VAL);
}

Value FSReadJsonExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    if (!pathVal.isString()) throw std::runtime_error("fs.readJson requires string path.");
    std::ifstream in(*pathVal.asString());
    if (!in.is_open()) throw std::runtime_error("Could not open file for fs.readJson: " + *pathVal.asString());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    return parseJSONValue(content, pos, interpreter);
}

Value FSWriteJsonExpression::evaluate(Interpreter& interpreter) {
    Value pathVal = path->evaluate(interpreter);
    Value contentVal = value->evaluate(interpreter);
    if (!pathVal.isString()) throw std::runtime_error("fs.writeJson requires string path.");
    std::string p = *pathVal.asString();
    std::string json = stringifyJSON(contentVal);
    std::ofstream out(p, std::ios::trunc);
    out << json;
    out.close();
    return Value(Type::NULL_VAL);
}

Value ConsoleShowExpression::evaluate(Interpreter& interpreter) {
    Value last;
    for (size_t i = 0; i < arguments.size(); ++i) {
        last = arguments[i]->evaluate(interpreter);
        std::cout << last.toString();
        if (i < arguments.size() - 1) std::cout << " ";
    }
    std::cout << std::endl;
    return last;
}

void Interpreter::executeDestructuring(const std::vector<DestructuringBinding>& bindings, Value val) {
    for (const auto& binding : bindings) {
        Value element;
        if (binding.isRest) {
            if (binding.arrayIndex != -1) {
                if (!val.isArray()) throw std::runtime_error("Array rest destructuring expects array");
                auto* arr = val.asArray();
                auto* restArr = makeArray();
                if (binding.arrayIndex < (int)arr->size()) {
                    restArr->insert(restArr->end(), arr->begin() + binding.arrayIndex, arr->end());
                }
                element = Value(restArr);
            } else {
                if (!val.isObject()) throw std::runtime_error("Object rest destructuring expects object");
                auto* obj = val.asObject();
                auto* restObj = makeObject();
                std::unordered_set<std::string> excluded;
                for (const auto& b : bindings) if (!b.isRest && b.arrayIndex == -1) excluded.insert(b.propertyName);
                for (const auto& p : *obj) if (excluded.find(p.first) == excluded.end()) restObj->push_back(p);
                element = Value(restObj);
            }
        } else if (binding.arrayIndex != -1) {
            if (!val.isArray()) throw std::runtime_error("Array index access on non-array");
            auto* arr = val.asArray();
            if (binding.arrayIndex < (int)arr->size()) element = (*arr)[binding.arrayIndex];
            else element = Value();
        } else {
            if (!val.isObject()) throw std::runtime_error("Property access on non-object");
            auto* obj = val.asObject();
            bool found = false;
            for (const auto& p : *obj) if (p.first == binding.propertyName) { element = p.second; found = true; break; }
            if (!found) element = Value();
        }

        if (!binding.nested.empty()) {
            executeDestructuring(binding.nested, element);
        } else if (!binding.name.empty()) {
            if (binding.index != -1) environment->defineAt(binding.index, element);
            else environment->define(binding.name, element);
        }
    }
}
