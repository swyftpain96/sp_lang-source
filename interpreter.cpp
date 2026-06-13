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
#include <thread>
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
#include <regex>

namespace fs = std::filesystem;

static std::string escapeRegex(const std::string& s) {
    std::string res = "";
    for (char c : s) {
        if (strchr(".^$*+?()[]{}\\|", c)) res += "\\";
        res += c;
    }
    return res;
}

Value SpFunction::call(Interpreter& interpreter, const std::vector<Value>& args) {
    auto env = std::make_shared<Environment>(declaration.localCount, closure);
    if (!boundInstance.isUndefined()) {
        env->define("this", boundInstance);
    }
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
        Value val = (i < args.size()) ? args[i] : Value(Type::UNDEFINED);
        if (declaration.parameters[i].second.isPresent) {
            std::string typeStr = declaration.parameters[i].second.toString();
            if (!checkTypeInternal(val, typeStr)) {
                throw std::runtime_error("Type mismatch: expected " + typeStr + ", but got " + val.toString());
            }
        }
        env->defineAt(i, std::move(val));
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

    // Regex built-in object
    auto* rObj = makeObject();
    auto addRegexBuilder = [&](const std::string& name, std::function<std::string(const std::vector<Value>&)> patternGen) {
        rObj->push_back({name, Value(makeFunction(std::make_shared<NativeFunction>([this, patternGen](Interpreter&, const std::vector<Value>& args) {
            std::string p = patternGen(args);
            return makeRegex(p, p);
        }))) });
    };
    addRegexBuilder("digit", [](auto&){ return "\\d"; });
    addRegexBuilder("nonDigit", [](auto&){ return "\\D"; });
    addRegexBuilder("word", [](auto&){ return "\\w"; });
    addRegexBuilder("letter", [](auto&){ return "[a-zA-Z]"; });
    addRegexBuilder("whitespace", [](auto&){ return "\\s"; });
    addRegexBuilder("any", [](auto&){ return "."; });
    addRegexBuilder("start", [](auto&){ return "^"; });
    addRegexBuilder("end", [](auto&){ return "$"; });
    addRegexBuilder("wordBoundary", [](auto&){ return "\\b"; });
    addRegexBuilder("text", [](auto& args){ return args.empty() ? "" : escapeRegex(args[0].toPureString()); });
    addRegexBuilder("range", [](auto& args){ 
        if (args.size() < 2) return std::string("");
        return "[" + args[0].toPureString() + "-" + args[1].toPureString() + "]";
    });

    rObj->push_back({"group", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("(?:)", "(?:)");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex("(?:" + p + ")", "(?:" + p + ")");
    }))) });

    rObj->push_back({"capture", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("()", "()");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex("(" + p + ")", "(" + p + ")");
    }))) });

    rObj->push_back({"global", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("", "", true);
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex(p, p, true);
    }))) });

    rObj->push_back({"or", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.size() < 2) return args.empty() ? makeRegex("", "") : (args[0].isRegex() ? args[0] : makeRegex(args[0].toPureString(), args[0].toPureString()));
        std::string p1 = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        std::string p2 = args[1].isRegex() ? args[1].asRegex()->pattern : args[1].toPureString();
        return makeRegex(p1 + "|" + p2, p1 + "|" + p2);
    }))) });

    rObj->push_back({"optional", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("", "");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex("(?:" + p + ")?", "(" + p + ")?");
    }))) });

    rObj->push_back({"oneOrMore", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("", "");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex("(?:" + p + ")+", "(" + p + ")+");
    }))) });

    rObj->push_back({"zeroOrMore", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("", "");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex("(?:" + p + ")*", "(" + p + ")*");
    }))) });

    rObj->push_back({"repeatAtLeast", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.size() < 2) return makeRegex("", "");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        std::string n = args[1].toPureString();
        return makeRegex("(?:" + p + "){" + n + ",}", "(" + p + "){" + n + ",}");
    }))) });

    rObj->push_back({"repeatBetween", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.size() < 3) return makeRegex("", "");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        std::string min = args[1].toPureString();
        std::string max = args[2].toPureString();
        return makeRegex("(?:" + p + "){" + min + "," + max + "}", "(" + p + "){" + min + "," + max + "}");
    }))) });

    rObj->push_back({"capture", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("()", "()");
        std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
        return makeRegex("(" + p + ")", "(" + p + ")");
    }))) });

    // __call__ for classic regex: regex("...")
    rObj->push_back({"__call__", Value(makeFunction(std::make_shared<NativeFunction>([this](Interpreter&, const std::vector<Value>& args) {
        if (args.empty()) return makeRegex("", "");
        std::string p = args[0].toPureString();
        return makeRegex(p, p);
    }))) });
    environment->define("regex", Value(rObj));
}

Interpreter::Interpreter(std::shared_ptr<Environment> env, VM* vm) : vm(vm), environment(env) {
    Value::CurrentContext = this;
    callHandler = [this](ICallable* c, const std::vector<Value>& args) {
        return c->call(*this, args);
    };
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
    Value::registerClass(cl);
    return cl.get();
}

BigInt* Interpreter::makeBigInt(const BigInt& v) {
    auto b = std::make_shared<BigInt>(v);
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
    Value::registerFunction(f);
    return f.get();
}

Value Interpreter::makeRegex(const std::string& pattern, const std::string& lastPart, bool isGlobal) {
    auto r = std::make_shared<RegexData>(pattern, lastPart, isGlobal);
    Value::registerRegex(r);
    return Value(r.get());
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
        std::string spdFilename = useStmt.moduleName + ".spd";
        std::string modFilename = "modules/" + useStmt.moduleName + ".sp";
        std::string modSpdFilename = "modules/" + useStmt.moduleName + ".spd";
        
        std::string source;
        bool found = false;

        if (builtinModules.find(useStmt.moduleName) != builtinModules.end()) {
            source = builtinModules[useStmt.moduleName];
            found = true;
        } else {
            std::vector<std::string> paths = {filename, spdFilename, modFilename, modSpdFilename};
            for (const auto& p : paths) {
                std::ifstream file(p);
                if (file.is_open()) {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    source = buffer.str();
                    found = true;
                    break;
                }
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
        } else if (std::holds_alternative<LayoutDeclaration>(expStmt.declaration)) {
            const auto& decl = std::get<LayoutDeclaration>(expStmt.declaration);
            execute(decl);
            if (isExtractingExports) {
                // We should probably just store the layout in exports too
                // But currentExports stores Value. Layouts are in Value::Layouts.
                // We can export it as a string description of the layout.
                std::string desc = "{ ";
                for (size_t i = 0; i < decl.properties.size(); ++i) {
                    desc += decl.properties[i].name + ": " + decl.properties[i].type.toString();
                    if (i < decl.properties.size() - 1) desc += ", ";
                }
                desc += " }";
                currentExports[decl.name] = Value(makeString(desc));
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
    } else if (std::holds_alternative<LayoutDeclaration>(stmt)) {
        const auto& decl = std::get<LayoutDeclaration>(stmt);
        if (decl.aliasType.isPresent) {
            Value::Layouts[decl.name] = decl.aliasType.toString();
        } else {
            std::string typeStr = "{";
            for (size_t i = 0; i < decl.properties.size(); ++i) {
                typeStr += decl.properties[i].name + ":" + decl.properties[i].type.toString();
                if (i < decl.properties.size() - 1) typeStr += ",";
            }
            typeStr += "}";
            Value::Layouts[decl.name] = typeStr;
        }
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
    return Value(interpreter.makeBigInt(BigInt(value)));
}

Value AsExpression::evaluate(Interpreter& interpreter) {
    Value val = left->evaluate(interpreter);
    if (type.isPresent) {
        // We could add a runtime check here if we want strictness
        // For now, it serves as a hint/pass-through
    }
    return val;
}

Value TypeofExpression::evaluate(Interpreter& interpreter) {
    Value val = expr->evaluate(interpreter);
    if (val.isNumber()) return Value(interpreter.makeString("number"));
    if (val.isBigInt()) return Value(interpreter.makeString("number"));
    if (val.isString()) return Value(interpreter.makeString("string"));
    if (val.isBoolean()) return Value(interpreter.makeString("boolean"));
    if (val.isArray()) return Value(interpreter.makeString("array"));
    if (val.isFunction()) return Value(interpreter.makeString("function"));
    if (val.isNil()) return Value(interpreter.makeString("null"));
    if (val.isUndefined()) return Value(interpreter.makeString("undefined"));
    if (val.isError()) return Value(interpreter.makeString("error"));
    if (val.isRegex()) return Value(interpreter.makeString("regex"));
    if (val.isFuture()) return Value(interpreter.makeString("future"));
    if (val.isMap()) return Value(interpreter.makeString("map"));
    if (val.isTimer()) return Value(interpreter.makeString("timer"));
    if (val.isObject()) return Value(interpreter.makeString("object"));
    return Value(interpreter.makeString("unknown"));
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
        bool found = false;
        for (auto& pair : obj) {
            if (pair.first == property) {
                pair.second = val;
                found = true;
                break;
            }
        }
        if (!found) {
            obj.emplace_back(property, val);
        }
        fflush(stdout);
        return val;
    }
    throw std::runtime_error("Member assignment on non-object/instance");
}

Value MemberExpression::evaluate(Interpreter& interpreter) {
    auto objVal = object->evaluate(interpreter);
    if (objVal.isFuture()) objVal = objVal.asFuture()->get();

    if (objVal.isTimer()) {
        auto* timer = objVal.asTimer();
        if (property == "stop") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([timer](Interpreter&, const std::vector<Value>&) {
                timer->stop();
                return Value(Type::NULL_VAL);
            })));
        }
    }
    Value builtin = objVal.getBuiltinMethod(property, interpreter);
    if (!builtin.isUndefined()) return builtin;

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
                if (args.empty()) return Value(false);
                if (args[0].isRegex()) {
                    auto* r = args[0].asRegex();
                    if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(false); } }
                    return Value(std::regex_search(*s, *r->re));
                }
                if (!args[0].isString()) return Value(false);
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
                if (args.empty()) return Value(-1.0);
                if (args[0].isRegex()) {
                    auto* r = args[0].asRegex();
                    if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(-1.0); } }
                    std::smatch m;
                    if (std::regex_search(*s, m, *r->re)) return Value((double)m.position());
                    return Value(-1.0);
                }
                if (!args[0].isString()) return Value(-1.0);
                auto pos = s->find(*args[0].asString());
                return Value(pos == std::string::npos ? -1.0 : (double)pos);
            })));
        }
        if (property == "split") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                auto* arr = interp.makeArray();
                if (args.size() > 0 && args[0].isRegex()) {
                    auto* r = args[0].asRegex();
                    if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(arr); } }
                    std::sregex_token_iterator iter(s->begin(), s->end(), *r->re, -1);
                    std::sregex_token_iterator end;
                    for (; iter != end; ++iter) arr->push_back(Value(interp.makeString(*iter)));
                    return Value(arr);
                }
                std::string sep = (args.empty() || !args[0].isString()) ? "" : *args[0].asString();
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
                if (args.size() < 2) return Value(interp.makeString(*s));
                std::string to = args[1].toPureString();
                if (args[0].isRegex()) {
                    auto* r = args[0].asRegex();
                    if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(interp.makeString(*s)); } }
                    
                    if (r->isGlobal) {
                        return Value(interp.makeString(std::regex_replace(*s, *r->re, to)));
                    } else {
                        return Value(interp.makeString(std::regex_replace(*s, *r->re, to, std::regex_constants::format_first_only)));
                    }
                }
                if (!args[0].isString()) return Value(interp.makeString(*s));
                std::string res = *s;
                const std::string& from = *args[0].asString();
                
                // If global replace is desired for strings, we do it in a loop
                size_t start_pos = 0;
                while((start_pos = res.find(from, start_pos)) != std::string::npos) {
                    res.replace(start_pos, from.length(), to);
                    start_pos += to.length();
                }
                return Value(interp.makeString(res));
            })));
        }
        if (property == "match") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                auto* resArr = interp.makeArray();
                if (args.empty() || !args[0].isRegex()) return Value(resArr);
                auto* r = args[0].asRegex();
                if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(resArr); } }
                
                std::string target = *s;
                auto words_begin = std::sregex_iterator(target.begin(), target.end(), *r->re);
                auto words_end = std::sregex_iterator();
                for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
                    std::smatch match = *i;
                    if (match.size() > 1) {
                        auto* groups = interp.makeArray();
                        for (size_t g = 0; g < match.size(); ++g) {
                            groups->push_back(Value(interp.makeString(match[g].str())));
                        }
                        resArr->push_back(Value(groups));
                    } else {
                        resArr->push_back(Value(interp.makeString(match.str())));
                    }
                }
                return Value(resArr);
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
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>&) {
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
    if (objVal.isRegex()) {
        auto* r = objVal.asRegex();
        auto addChainer = [&](const std::string& name, std::function<std::pair<std::string, std::string>(const std::vector<Value>&)> gen) {
            if (property == name) {
                return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r, gen](Interpreter& interp, const std::vector<Value>& args) {
                    auto res = gen(args);
                    return interp.makeRegex(r->pattern + res.first, res.second, r->isGlobal);
                })));
            }
            return Value();
        };

        Value v;
        if (!(v = addChainer("digit", [](auto&){ return std::make_pair("\\d", "\\d"); })).isUndefined()) return v;
        if (!(v = addChainer("letter", [](auto&){ return std::make_pair("[a-zA-Z]", "[a-zA-Z]"); })).isUndefined()) return v;
        if (!(v = addChainer("whitespace", [](auto&){ return std::make_pair("\\s", "\\s"); })).isUndefined()) return v;
        if (!(v = addChainer("any", [](auto&){ return std::make_pair(".", "."); })).isUndefined()) return v;
        if (!(v = addChainer("start", [](auto&){ return std::make_pair("^", "^"); })).isUndefined()) return v;
        if (!(v = addChainer("end", [](auto&){ return std::make_pair("$", "$"); })).isUndefined()) return v;
        if (!(v = addChainer("text", [](auto& args){ std::string t = args.empty() ? "" : escapeRegex(args[0].toPureString()); return std::make_pair(t, t); })).isUndefined()) return v;
        if (!(v = addChainer("range", [](auto& args){ 
            if (args.size() < 2) return std::make_pair(std::string(""), std::string(""));
            std::string t = "[" + args[0].toPureString() + "-" + args[1].toPureString() + "]";
            return std::make_pair(t, t);
        })).isUndefined()) return v;

        // Modifiers apply to lastPart
        if (property == "repeat") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return Value(r);
                std::string rep;
                if (args.size() >= 2) rep = "{" + args[0].toPureString() + "," + args[1].toPureString() + "}";
                else rep = "{" + args[0].toPureString() + "}";
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")" + rep;
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")" + rep);
            })));
        }
        if (property == "maybe" || property == "optional") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")?";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")?");
            })));
        }
        if (property == "oneOrMore") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")+";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")+");
            })));
        }
        if (property == "zeroOrMore") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")*";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")*");
            })));
        }
        if (property == "repeatAtLeast") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return Value(r);
                std::string n = args[0].toPureString();
                std::string rep = "{" + n + ",}";
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")" + rep;
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")" + rep);
            })));
        }
        if (property == "repeatBetween") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.size() < 2) return Value(r);
                std::string min = args[0].toPureString();
                std::string max = args[1].toPureString();
                std::string rep = "{" + min + "," + max + "}";
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")" + rep;
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")" + rep);
            })));
        }
        if (property == "capture") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return interp.makeRegex(r->pattern + "()", "()");
                std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
                return interp.makeRegex(r->pattern + "(" + p + ")", "(" + p + ")");
            })));
        }
        if (property == "group") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return interp.makeRegex(r->pattern + "(?:)", "(?:)");
                std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
                return interp.makeRegex(r->pattern + "(?:" + p + ")", "(?:" + p + ")");
            })));
        }
        if (property == "or") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isRegex()) return Value(r);
                std::string res = r->pattern + "|" + args[0].asRegex()->pattern;
                return interp.makeRegex(res, res);
            })));
        }

        if (property == "test") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([r](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                if (!r->re) {
                    try {
                        r->re = std::make_shared<std::regex>(r->pattern);
                    } catch (...) { return Value(false); }
                }
                return Value(std::regex_search(*args[0].asString(), *r->re));
            })));
        }
        if (property == "global") {
            return Value(interpreter.makeFunction(std::make_shared<NativeFunction>([this, r](Interpreter& interp, const std::vector<Value>&) {
                return interp.makeRegex(r->pattern, r->lastPart, true);
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
    for (const auto& pair : obj) {
        if (pair.first == property) {
            return pair.second;
        }
    }
    throw std::runtime_error("Property '" + property + "' not found");
}

Value IndexExpression::evaluate(Interpreter& interpreter) {
    auto objVal = object->evaluate(interpreter);
    if (objVal.isFuture()) objVal = objVal.asFuture()->get();
    auto indexVal = index->evaluate(interpreter);

    if (objVal.isArray()) {
        if (!indexVal.isNumber()) {
            return Value(Type::UNDEFINED);
        }
        double d = indexVal.asNumber();
        if (d < 0 || std::floor(d) != d) {
            return Value(Type::UNDEFINED);
        }
        size_t idx = static_cast<size_t>(d);
        const auto& arr = *objVal.asArray();
        if (idx >= arr.size()) {
            return Value(Type::UNDEFINED);
        }
        return arr[idx];
    } else if (objVal.isMap()) {
        auto* m = objVal.asMap();
        auto it = m->map.find(indexVal);
        if (it == m->map.end()) return Value(Type::UNDEFINED);
        return it->second;
    } else if (objVal.isObject() && !objVal.isInstance()) {
        if (!indexVal.isString()) return Value(Type::UNDEFINED);
        std::string key = *indexVal.asString();
        const auto& obj = *objVal.asObject();
        for (const auto& pair : obj) {
            if (pair.first == key) return pair.second;
        }
        return Value(Type::UNDEFINED);
    } else if (objVal.isInstance()) {
        if (!indexVal.isString()) return Value(Type::UNDEFINED);
        std::string key = *indexVal.asString();
        auto* instance = objVal.asInstance();
        for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
            if (instance->klass->properties[i].name == key) return instance->fields[i];
        }
        auto it = instance->klass->methods.find(key);
        if (it != instance->klass->methods.end()) {
            Value methodVal = it->second;
            if (methodVal.isFunction()) {
                return Value(interpreter.makeFunction(std::make_shared<BoundMethod>(methodVal.asFunction(), objVal)));
            }
            return methodVal;
        }
        return Value(Type::UNDEFINED);
    }
    return Value(Type::UNDEFINED);
}

Value IndexAssignmentExpression::evaluate(Interpreter& interpreter) {
    auto objVal = object->evaluate(interpreter);
    if (objVal.isFuture()) objVal = objVal.asFuture()->get();
    auto indexVal = index->evaluate(interpreter);
    auto val = value->evaluate(interpreter);

    if (objVal.isArray()) {
        if (!indexVal.isNumber()) throw std::runtime_error("Array index must be a number");
        double d = indexVal.asNumber();
        if (d < 0 || std::floor(d) != d) throw std::runtime_error("Invalid array index");
        size_t idx = static_cast<size_t>(d);
        auto* arr = objVal.asArray();
        while (idx >= arr->size()) {
            arr->push_back(Value(Type::UNDEFINED));
        }
        (*arr)[idx] = val;
    } else if (objVal.isMap()) {
        auto* m = objVal.asMap();
        m->map[indexVal] = val;
    } else if (objVal.isObject() && !objVal.isInstance()) {
        if (!indexVal.isString()) throw std::runtime_error("Object properties must be indexed by string");
        std::string key = *indexVal.asString();
        auto* obj = objVal.asObject();
        bool found = false;
        for (auto& pair : *obj) {
            if (pair.first == key) {
                pair.second = val;
                found = true;
                break;
            }
        }
        if (!found) obj->push_back({key, val});
    } else if (objVal.isInstance()) {
        if (!indexVal.isString()) throw std::runtime_error("Instance properties must be indexed by string");
        std::string key = *indexVal.asString();
        auto* instance = objVal.asInstance();
        for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
            if (instance->klass->properties[i].name == key) {
                if (instance->klass->properties[i].isReadonly) {
                    throw std::runtime_error("Cannot assign to readonly property '" + key + "'");
                }
                instance->fields[i] = val;
                return val;
            }
        }
        throw std::runtime_error("Cannot add new properties to instance dynamically");
    } else {
        throw std::runtime_error("Invalid assignment target for index assignment");
    }
    return val;
}

Value MatchExpression::evaluate(Interpreter& interpreter) {
    auto matchVal = valueToMatch->evaluate(interpreter);
    for (const auto& matchCase : cases) {
        if (!matchCase.pattern) {
            return matchCase.body->evaluate(interpreter);
        }
        auto patternVal = matchCase.pattern->evaluate(interpreter);
        bool eq = false;
        if (patternVal.isRegex()) {
            auto* r = patternVal.asRegex();
            if (!r->re) {
                try {
                    r->re = std::make_shared<std::regex>(r->pattern);
                } catch (...) { eq = false; goto skip_regex; }
            }
            eq = std::regex_search(matchVal.toPureString(), *r->re);
        } else {
            eq = (matchVal.bits == patternVal.bits);
            if (!eq && matchVal.isNumber() && patternVal.isNumber()) {
                eq = (matchVal.asNumber() == patternVal.asNumber());
            }
            if (!eq && matchVal.isString() && patternVal.isString()) {
                eq = (*matchVal.asString() == *patternVal.asString());
            }
        }
        skip_regex:
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
            if (interpreter.callHandler) {
                result = interpreter.callHandler(callable, args);
            } else {
                result = callable->call(interpreter, args);
            }
            } else {
                Value rightVal = right->evaluate(interpreter);
                if (rightVal.isFunction()) {
                if (interpreter.callHandler) {
                    result = interpreter.callHandler(rightVal.asFunction(), {leftVal});
                } else {
                    result = rightVal.asFunction()->call(interpreter, {leftVal});
                }
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
        Value eq = (leftVal == rightVal);
        if (op == "!=") return !eq;
        return eq;
    }

    if (op == "+") return leftVal + rightVal;
    if (op == "-") return leftVal - rightVal;
    if (op == "*") return leftVal * rightVal;
    if (op == "/") return leftVal / rightVal;
    if (op == "%") return leftVal % rightVal;
    if (op == "<") return leftVal < rightVal;
    if (op == "<=") return leftVal <= rightVal;
    if (op == ">") return leftVal > rightVal;
    if (op == ">=") return leftVal >= rightVal;

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
    if (calleeVal.isFuture()) calleeVal = calleeVal.asFuture()->get();
    std::vector<Value> evaluatedArgs;
    for (const auto& arg : arguments) evaluatedArgs.push_back(arg->evaluate(interpreter));
    
    if (calleeVal.isFunction()) {
        auto* func = calleeVal.asFunction();
        if (interpreter.callHandler) return interpreter.callHandler(func, evaluatedArgs);
        return func->call(interpreter, evaluatedArgs);
    }
    
    if (calleeVal.isObject() && !calleeVal.isInstance()) {
        auto* obj = calleeVal.asObject();
        for (auto& pair : *obj) {
            if (pair.first == "__call__") {
                if (pair.second.isFunction()) {
                    if (interpreter.callHandler) return interpreter.callHandler(pair.second.asFunction(), evaluatedArgs);
                    return pair.second.asFunction()->call(interpreter, evaluatedArgs);
                }
            }
        }
    }
    
    throw std::runtime_error("Can only call functions");
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
    decl.returnType = returnType;
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

Value ProcessSleepExpression::evaluate(Interpreter& interpreter) {
    Value delayVal = delay->evaluate(interpreter);
    if (!delayVal.isNumber()) throw std::runtime_error("process.sleep requires a number delay in milliseconds.");
    double delayMs = delayVal.asNumber();
    if (delayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)delayMs));
    }
    return Value();
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

Value AsyncExpression::evaluate(Interpreter& interpreter) {
    // Capture the body expression and the current environment for the closure
    auto bodyExpr = body;
    auto capturedEnv = interpreter.environment;

    // Launch on a real OS thread via std::async
    auto fut = std::make_shared<std::future<Value>>(
        std::async(std::launch::async, [bodyExpr, capturedEnv]() -> Value {
            // Use the constructor that shares the environment
            Interpreter threadInterp(capturedEnv);
            Value::CurrentContext = &threadInterp;
            try {
                Value result = bodyExpr->evaluate(threadInterp);
                return result;
            } catch (const std::exception& e) {
                auto err = std::make_shared<ErrorData>(e.what(), -1);
                Value::registerError(err);
                return Value(err.get());
            }
        })
    );

    auto futData = std::make_shared<FutureData>(std::move(fut));
    Value::registerFuture(futData);
    return Value(futData.get());
}

Value AfterExpression::evaluate(Interpreter& interpreter) {
    auto delayVal = delay->evaluate(interpreter);
    double delayMs = delayVal.asNumber();
    auto bodyExpr = body;
    auto capturedEnv = interpreter.environment;

    auto timer = std::make_shared<TimerData>();
    timer->thread = std::thread([delayMs, bodyExpr, capturedEnv, timer]() {
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds((long long)delayMs));
        }
        if (timer->active) {
            Interpreter threadInterp(capturedEnv);
            Value::CurrentContext = &threadInterp;
            try {
                bodyExpr->evaluate(threadInterp);
            } catch (...) {}
        }
    });

    Value::registerTimer(timer);
    return Value(timer.get());
}

Value EveryExpression::evaluate(Interpreter& interpreter) {
    auto delayVal = delay->evaluate(interpreter);
    double delayMs = delayVal.asNumber();
    auto bodyExpr = body;
    auto capturedEnv = interpreter.environment;

    auto timer = std::make_shared<TimerData>();
    timer->thread = std::thread([delayMs, bodyExpr, capturedEnv, timer]() {
        while (timer->active) {
            if (delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds((long long)delayMs));
            }
            if (!timer->active) break;
            
            Interpreter threadInterp(capturedEnv);
            Value::CurrentContext = &threadInterp;
            try {
                bodyExpr->evaluate(threadInterp);
            } catch (...) {}
        }
    });

    Value::registerTimer(timer);
    return Value(timer.get());
}

Value LayoutExpression::evaluate(Interpreter& interpreter) {
    (void)interpreter;
    // Evaluation of a layout expression returns a string representation
    // suitable for checkTypeInternal's structural matching.
    std::string res = "{ ";
    for (size_t i = 0; i < properties.size(); ++i) {
        res += properties[i].name + ": " + properties[i].type.toString();
        if (i < properties.size() - 1) res += ", ";
    }
    res += " }";
    return Value(new std::string(res));
}


std::recursive_mutex Environment::envMutex;
