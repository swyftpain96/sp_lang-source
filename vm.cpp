#include "vm.h"
#include "interpreter.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <dlfcn.h>
#include <libgen.h>
#endif
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <functional>
#include <sys/stat.h>

namespace fs = std::filesystem;

VM::VM() {
    stack = std::make_unique<Value[]>(STACK_MAX);
    frames = std::make_unique<CallFrame[]>(FRAMES_MAX);
}

VM::~VM() {
    // Vectors of shared_ptr handle cleanup automatically.
}

void VM::runtimeError(const std::string& message, int line) {
    if (useColor) {
        std::cerr << Color::Red << Color::Bold << "[Runtime Error] " << Color::Reset << Color::Red << message << Color::Reset;
        if (line != -1) std::cerr << Color::Cyan << " (at line " << line << ")" << Color::Reset;
    } else {
        std::cerr << "[Runtime Error] " << message;
        if (line != -1) std::cerr << " (at line " << line << ")";
    }
    std::cerr << std::endl;
    throw std::runtime_error(message);
}

int VM::getLine(CallFrame* frame, uint8_t* ip) {
    size_t offset = ip - frame->function->chunk.code.data() - 1;
    int line = -1;
    for (const auto& li : frame->function->chunk.lineInfo) {
        if (li.offset > offset) break;
        line = li.line;
    }
    return line;
}

void VM::defineGlobal(const std::string& name, Value val) {
    auto it = globalIndices.find(name);
    if (it != globalIndices.end()) {
        globals[it->second] = val;
        return;
    }
    globalIndices[name] = (int)globals.size();
    globals.push_back(val);
}

static Value sp_get_property(Interpreter& interpreter, Value objVal, const std::string& property) {
    if (objVal.isArray()) {
        auto* arr = objVal.asArray();
        if (property == "length") return Value((double)arr->size());
        if (property == "push") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                for (const auto& arg : args) arr->push_back(arg);
                return Value((double)arr->size());
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "pop") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>&) {
                if (arr->empty()) return Value(Type::NULL_VAL);
                Value val = arr->back();
                arr->pop_back();
                return val;
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "shift") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>&) {
                if (arr->empty()) return Value(Type::NULL_VAL);
                Value val = (*arr)[0];
                arr->erase(arr->begin());
                return val;
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "unshift") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                arr->insert(arr->begin(), args.begin(), args.end());
                return Value((double)arr->size());
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "join") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                std::string sep = (args.empty() || !args[0].isString()) ? "," : *args[0].asString();
                std::string res = "";
                for (size_t i = 0; i < arr->size(); ++i) {
                    res += (*arr)[i].toPureString();
                    if (i < arr->size() - 1) res += sep;
                }
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "reverse") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr, objVal](Interpreter&, const std::vector<Value>&) {
                std::reverse(arr->begin(), arr->end());
                return objVal;
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "slice") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                size_t start = (args.size() > 0 && args[0].isNumber()) ? (size_t)args[0].asNumber() : 0;
                size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : arr->size();
                if (start > arr->size()) start = arr->size();
                if (end > arr->size()) end = arr->size();
                if (start > end) std::swap(start, end);
                auto* newArr = interp.makeArray();
                for (size_t i = start; i < end; ++i) newArr->push_back((*arr)[i]);
                return Value(newArr);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "contains" || property == "includes") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(false);
                for (const auto& item : *arr) {
                    if ((item == args[0]).asBoolean()) return Value(true);
                }
                return Value(false);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "indexOf") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(-1.0);
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (((*arr)[i] == args[0]).asBoolean()) return Value((double)i);
                }
                return Value(-1.0);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "forEach") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("forEach requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (size_t i = 0; i < arr->size(); ++i) {
                    interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                }
                return Value(Type::NULL_VAL);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "map") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("map requires a function callback.");
                ICallable* callback = args[0].asFunction();
                auto* newArr = interp.makeArray();
                for (size_t i = 0; i < arr->size(); ++i) {
                    newArr->push_back(interp.callHandler(callback, {(*arr)[i], Value((double)i)}));
                }
                return Value(newArr);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "filter") {
            auto nativeFunc = std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("filter requires a function callback.");
                ICallable* callback = args[0].asFunction();
                auto* newArr = interp.makeArray();
                for (size_t i = 0; i < arr->size(); ++i) {
                    Value res = interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                    bool isTrue = (res.isBoolean()) ? res.asBoolean() : (!res.isNil() && !res.isUndefined());
                    if (isTrue) newArr->push_back((*arr)[i]);
                }
                return Value(newArr);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
    }
    if (objVal.isDate()) {
        time_t ts = (time_t)(objVal.asDate()->timestamp / 1000.0);
        struct tm* timeinfo = localtime(&ts);
        if (property == "year") return Value((double)(timeinfo->tm_year + 1900));
        if (property == "month") return Value((double)(timeinfo->tm_mon + 1));
        if (property == "day") return Value((double)timeinfo->tm_mday);
        if (property == "hour") return Value((double)timeinfo->tm_hour);
        if (property == "minute") return Value((double)timeinfo->tm_min);
        if (property == "second") return Value((double)timeinfo->tm_sec);
    }
    if (objVal.isMap()) {
        auto* m = objVal.asMap();
        if (property == "size") return Value((double)m->map.size());
        if (property == "set") {
            auto nativeFunc = std::make_shared<NativeFunction>([m, objVal](Interpreter&, const std::vector<Value>& args) {
                if (args.size() < 2) throw std::runtime_error("Map.set requires 2 arguments.");
                m->map[args[0]] = args[1];
                return objVal;
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "get") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) throw std::runtime_error("Map.get requires 1 argument.");
                auto it = m->map.find(args[0]);
                if (it == m->map.end()) return Value(Type::NULL_VAL);
                return it->second;
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "has") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) throw std::runtime_error("Map.has requires 1 argument.");
                return Value(m->map.find(args[0]) != m->map.end());
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "delete") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) throw std::runtime_error("Map.delete requires 1 argument.");
                return Value(m->map.erase(args[0]) > 0);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "clear") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>&) {
                m->map.clear();
                return Value(Type::UNDEFINED);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "keys") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>&) {
                auto* arr = interp.makeArray();
                for (auto const& [key, val] : m->map) arr->push_back(key);
                return Value(arr);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "values") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>&) {
                auto* arr = interp.makeArray();
                for (auto const& [key, val] : m->map) arr->push_back(val);
                return Value(arr);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "forEach") {
            auto nativeFunc = std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("Map.forEach requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (auto const& [key, val] : m->map) interp.callHandler(callback, {key, val});
                return Value(Type::UNDEFINED);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
    }
    if (objVal.isString()) {
        auto* s = objVal.asString();
        if (property == "length" || property == "size") return Value((double)s->length());
        if (property == "trim") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                res.erase(0, res.find_first_not_of(" \t\n\r\f\v"));
                size_t last = res.find_last_not_of(" \t\n\r\f\v");
                if (last != std::string::npos) res.erase(last + 1);
                else if (!res.empty() && isspace(res[0])) res.clear();
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "toLowerCase") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                for (auto& c : res) c = std::tolower(c);
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "toUpperCase") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                for (auto& c : res) c = std::toupper(c);
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "contains" || property == "includes") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                return Value(s->find(*args[0].asString()) != std::string::npos);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "startsWith") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                const std::string& sub = *args[0].asString();
                return Value(s->compare(0, sub.length(), sub) == 0);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "endsWith") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                const std::string& sub = *args[0].asString();
                if (sub.length() > s->length()) return Value(false);
                return Value(s->compare(s->length() - sub.length(), sub.length(), sub) == 0);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "indexOf") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(-1.0);
                auto pos = s->find(*args[0].asString());
                return Value(pos == std::string::npos ? -1.0 : (double)pos);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "split") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                std::string sep = (args.size() > 0 && args[0].isString()) ? *args[0].asString() : "";
                auto* arr = interp.makeArray();
                if (sep.empty()) {
                    for (char c : *s) arr->push_back(Value(interp.makeString(std::string(1, c))));
                } else {
                    size_t last = 0, next = 0;
                    while ((next = s->find(sep, last)) != std::string::npos) {
                        arr->push_back(Value(interp.makeString(s->substr(last, next - last))));
                        last = next + sep.length();
                    }
                    arr->push_back(Value(interp.makeString(s->substr(last))));
                }
                return Value(arr);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "replace") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.size() < 2 || !args[0].isString() || !args[1].isString()) return Value(interp.makeString(*s));
                std::string res = *s;
                const std::string& from = *args[0].asString();
                const std::string& to = *args[1].asString();
                size_t start_pos = res.find(from);
                if (start_pos != std::string::npos) res.replace(start_pos, from.length(), to);
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "substring") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                size_t start = (size_t)args[0].asNumber();
                size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : s->length();
                if (start > s->length()) start = s->length();
                if (end > s->length()) end = s->length();
                if (start > end) std::swap(start, end);
                return Value(interp.makeString(s->substr(start, end - start)));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
    }
    if (objVal.isNumber()) {
        double n = objVal.asNumber();
        if (property == "toFixed") {
            auto nativeFunc = std::make_shared<NativeFunction>([n](Interpreter& interp, const std::vector<Value>& args) {
                int digits = (args.size() > 0 && args[0].isNumber()) ? (int)args[0].asNumber() : 0;
                std::stringstream ss; ss << std::fixed << std::setprecision(digits) << n;
                return Value(interp.makeString(ss.str()));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "toString") {
            auto nativeFunc = std::make_shared<NativeFunction>([objVal](Interpreter& interp, const std::vector<Value>&) {
                return Value(interp.makeString(objVal.toPureString()));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
    }
    if (objVal.isObject() && !objVal.isInstance()) {
        auto* obj = objVal.asObject();
        if (property == "keys") {
            auto nativeFunc = std::make_shared<NativeFunction>([obj](Interpreter& interp, const std::vector<Value>&) {
                auto* res = interp.makeArray();
                for (const auto& pair : *obj) res->push_back(Value(interp.makeString(pair.first)));
                return Value(res);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "values") {
            auto nativeFunc = std::make_shared<NativeFunction>([obj](Interpreter& interp, const std::vector<Value>&) {
                auto* res = interp.makeArray();
                for (const auto& pair : *obj) res->push_back(pair.second);
                return Value(res);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "has") {
            auto nativeFunc = std::make_shared<NativeFunction>([obj](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                const std::string& key = *args[0].asString();
                for (const auto& pair : *obj) if (pair.first == key) return Value(true);
                return Value(false);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
    }
    if (objVal.isInstance()) {
        auto* instance = objVal.asInstance();
        for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
            if (instance->klass->properties[i].name == property) return instance->fields[i];
        }
        auto it = instance->klass->methods.find(property);
        if (it != instance->klass->methods.end()) {
            Value methodVal = it->second;
            if (methodVal.isFunction()) {
                auto bm = std::make_shared<BoundMethod>(methodVal.asFunction(), objVal);
                Value::registerFunction(bm);
                return Value(bm.get());
            }
            return methodVal;
        }
    }
    if (objVal.isObject()) {
        for (const auto& pair : *objVal.asObject()) {
            if (pair.first == property) return pair.second;
        }
    }
    return Value(Type::UNDEFINED);
}

void VM::run(VMFunction* mainFunction, Interpreter& interpreter) {
    Value::useColor = useColor;
    CallFrame* framesPtr = frames.get();
    Value* stackPtr = stack.get();

    frame = framesPtr;
    CallFrame* framesEnd = framesPtr + FRAMES_MAX;
    
    frame->function = mainFunction;
    frame->ip = mainFunction->chunk.code.data();
    frame->stackBase = 0;
    
    sp = stackPtr;
    fp = sp;
    globalsPtr = globals.data();
    
    interpreter.callHandler = [this, &interpreter](ICallable* c, const std::vector<Value>& args) {
        return this->call(interpreter, c, args);
    };
    
    for (int i = 0; i < mainFunction->localCount; ++i) *sp++ = Value();

    uint8_t* ip = frame->ip;
    Value* constantsPtr = frame->function->chunk.constants.data();

#define DISPATCH() goto next_instruction

    while (true) {
    next_instruction:
        uint8_t opcode = *ip;
        ip++;
        switch (static_cast<OpCode>(opcode)) {

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() (constantsPtr[READ_BYTE()])

#define BINARY_OP(op) \
    do { \
        Value b = *(--sp); \
        Value a = *(--sp); \
        if (a.isNumber() && b.isNumber()) { \
            *sp++ = Value(a.asNumber() op b.asNumber()); \
        } else if (a.isBigInt() && b.isBigInt()) { \
            auto bi = std::make_shared<int64_t>(*a.asBigInt() op *b.asBigInt()); \
            Value::registerBigInt(bi); \
            *sp++ = Value(bi.get()); \
        } else { \
            runtimeError("Operands must be numbers or BigInts.", getLine(frame, ip)); \
            return; \
        } \
    } while (0)

#if 0 && defined(__GNUC__)
        op_constant:
#else
        case OpCode::CONSTANT:
#endif
            *sp++ = READ_CONSTANT();
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_null:
#else
        case OpCode::NULL_VAL:
#endif
            *sp++ = Value(Type::NULL_VAL);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_true:
#else
        case OpCode::TRUE_VAL:
#endif
            *sp++ = Value(true);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_false:
#else
        case OpCode::FALSE_VAL:
#endif
            *sp++ = Value(false);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_pop:
#else
        case OpCode::POP:
#endif
            --sp;
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_get_local:
#else
        case OpCode::GET_LOCAL:
#endif
            *sp++ = fp[READ_BYTE()];
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_set_local:
#else
        case OpCode::SET_LOCAL:
#endif
            fp[READ_BYTE()] = sp[-1];
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_get_global:
#else
        case OpCode::GET_GLOBAL:
#endif
            {
                uint8_t constIdx = READ_BYTE();
                Value nameVal = constantsPtr[constIdx];
                if (!nameVal.isString()) {
                    runtimeError("Global name must be a string.", getLine(frame, ip));
                    return;
                }
                std::string& name = *nameVal.asString();
                
                auto it = globalIndices.find(name);
                if (it == globalIndices.end()) {
                    runtimeError("Undefined global: " + name, getLine(frame, ip));
                    return;
                }
                
                int globalIdx = it->second;
                if (globalIdx <= 255) {
                    *(ip - 2) = static_cast<uint8_t>(OpCode::GET_GLOBAL_FAST);
                    *(ip - 1) = static_cast<uint8_t>(globalIdx);
                }
                *sp++ = globals[globalIdx];
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_get_global_fast:
#else
        case OpCode::GET_GLOBAL_FAST:
#endif
            *sp++ = globals[READ_BYTE()];
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_define_global:
#else
        case OpCode::DEFINE_GLOBAL:
#endif
            {
                uint8_t constIdx = READ_BYTE();
                bool isMutable = READ_BYTE();
                Value nameVal = constantsPtr[constIdx];
                if (!nameVal.isString()) throw std::runtime_error("Global name must be a string.");
                std::string& name = *nameVal.asString();
                
                Value val = *(--sp);
                if (globalIndices.find(name) == globalIndices.end()) {
                    globalIndices[name] = globals.size();
                    globalMutability[name] = isMutable;
                    globals.push_back(val);
                    globalsPtr = globals.data(); 
                } else {
                    globals[globalIndices[name]] = val;
                    globalsPtr = globals.data();
                }
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_set_global:
#else
        case OpCode::SET_GLOBAL:
#endif
            {
                uint8_t constIdx = READ_BYTE();
                Value nameVal = constantsPtr[constIdx];
                std::string& name = *nameVal.asString();
                
                auto it = globalIndices.find(name);
                if (it == globalIndices.end()) throw std::runtime_error("Undefined global: " + name);
                
                int globalIdx = it->second;
                if (globalIdx <= 255) {
                    *(ip - 2) = static_cast<uint8_t>(OpCode::SET_GLOBAL_FAST);
                    *(ip - 1) = static_cast<uint8_t>(globalIdx);
                }
                globals[globalIdx] = sp[-1];
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_set_global_fast:
#else
        case OpCode::SET_GLOBAL_FAST:
#endif
            globals[READ_BYTE()] = sp[-1];
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_get_property:
#else
        case OpCode::GET_PROPERTY:
#endif
            {
                Value objVal = *(--sp);
                Value nameVal = constantsPtr[READ_BYTE()];
                const std::string& property = *nameVal.asString();
                Value res = sp_get_property(interpreter, objVal, property);
                if (res.isUndefined()) {
                    runtimeError("Property '" + property + "' not found", getLine(frame, ip));
                    return;
                }
                *sp++ = res;
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_set_property:
#else
        case OpCode::SET_PROPERTY:
#endif
            {
                Value val = *(--sp);
                Value objVal = *(--sp);
                Value nameVal = constantsPtr[READ_BYTE()];
                
                if (objVal.isInstance()) {
                    const std::string& property = *nameVal.asString();
                    auto* instance = objVal.asInstance();
                    for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
                        if (instance->klass->properties[i].name == property) {
                            if (instance->klass->properties[i].isReadonly) {
                                // Read-only property assignment handled by runtime error below
                                return;
                            }
                            instance->fields[i] = val;
                            *sp++ = objVal;
                            std::fflush(stdout);
                            DISPATCH();
                        }
                    }
                    runtimeError("Property '" + property + "' not found or not assignable in class '" + instance->klass->name + "'", getLine(frame, ip));
                    return;
                }

                if (!objVal.isObject()) {
                    runtimeError("Cannot set property on non-object", getLine(frame, ip));
                    return;
                }
                
                auto* obj = objVal.asObject();
                bool found = false;
                for (auto& pair : *obj) {
                    if (pair.first == *nameVal.asString()) {
                        pair.second = val;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    obj->emplace_back(*nameVal.asString(), val);
                }
                *sp++ = objVal; // Push the object back so successive sets chain cleanly
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_make_object:
#else
        case OpCode::MAKE_OBJECT:
#endif
            {
                int count = READ_BYTE();
                auto obj = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                Value::registerObject(obj);
                obj->reserve(count);
                for (int i = count - 1; i >= 0; i--) {
                    Value val = *(--sp);
                    Value nameVal = *(--sp);
                    obj->push_back({*nameVal.asString(), val});
                }
                *sp++ = Value(obj.get());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_get_element:
#else
        case OpCode::GET_ELEMENT:
#endif
            {
                Value indexVal = *(--sp);
                Value arrVal = *(--sp);
                
                if (!arrVal.isArray()) {
                    runtimeError("Index access on non-array", getLine(frame, ip));
                    return;
                }
                if (!indexVal.isNumber()) {
                    runtimeError("Array index must be a number", getLine(frame, ip));
                    return;
                }
                
                double d = indexVal.asNumber();
                if (d < 0 || std::floor(d) != d) {
                    runtimeError("Invalid array index", getLine(frame, ip));
                    return;
                }
                
                size_t idx = static_cast<size_t>(d);
                const auto& arr = *arrVal.asArray();
                
                if (idx >= arr.size()) {
                    runtimeError("Array index out of bounds", getLine(frame, ip));
                    return;
                }
                
                *sp++ = arr[idx];
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_make_array:
#else
        case OpCode::MAKE_ARRAY:
#endif
            {
                int count = READ_BYTE();
                auto arr = std::make_shared<std::vector<Value>>();
                Value::registerArray(arr);
                arr->resize(count);
                for (int i = count - 1; i >= 0; i--) {
                    (*arr)[i] = *(--sp);
                }
                *sp++ = Value(arr.get());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_add:
#else
        case OpCode::ADD:
#endif
            {
                Value b = *(--sp);
                Value a = *(--sp);
                if (a.isString() || b.isString()) {
                    auto s = std::make_shared<std::string>(a.toPureString() + b.toPureString());
                    Value::registerString(s);
                    *sp++ = Value(s.get());
                } else if (a.isBigInt() && b.isBigInt()) {
                    auto bi = std::make_shared<int64_t>(*a.asBigInt() + *b.asBigInt());
                    Value::registerBigInt(bi);
                    *sp++ = Value(bi.get());
                } else if (a.isNumber() && b.isNumber()) {
                    *sp++ = Value(a.asNumber() + b.asNumber());
                } else {
                    runtimeError("Operands must be numbers, BigInts, or strings for '+'.", getLine(frame, ip));
                    return;
                }
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_subtract:
#else
        case OpCode::SUBTRACT:
#endif
            BINARY_OP(-);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_multiply:
#else
        case OpCode::MULTIPLY:
#endif
            BINARY_OP(*);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_divide:
#else
        case OpCode::DIVIDE:
#endif
            {
                if (!sp[-1].isNumber() || !sp[-2].isNumber()) {
                    runtimeError("Operands must be numbers.", getLine(frame, ip));
                    return;
                }
                double b = sp[-1].asNumber();
                double a = sp[-2].asNumber();
                if (b == 0) {
                    runtimeError("Division by zero.", getLine(frame, ip));
                    return;
                }
                sp[-2] = Value(a / b);
                sp--;
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_equal:
#else
        case OpCode::EQUAL:
#endif
            {
                Value b = *(--sp);
                Value a = *(--sp);
                bool eq = (a.bits == b.bits);
                if (!eq) {
                    if (a.isNumber() && b.isNumber()) {
                        eq = (a.asNumber() == b.asNumber());
                    } else if (a.isString() && b.isString()) {
                        eq = (*a.asString() == *b.asString());
                    } else if (a.isBigInt() && b.isBigInt()) {
                        eq = (*a.asBigInt() == *b.asBigInt());
                    }
                }
                *sp++ = Value(eq);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_less:
#else
        case OpCode::LESS:
#endif
            BINARY_OP(<);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_less_equal:
#else
        case OpCode::LESS_EQUAL:
#endif
            BINARY_OP(<=);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_greater:
#else
        case OpCode::GREATER:
#endif
            BINARY_OP(>);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_greater_equal:
#else
        case OpCode::GREATER_EQUAL:
#endif
            BINARY_OP(>=);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_not:
#else
        case OpCode::NOT:
#endif
            {
                Value cond = sp[-1];
                bool isTrue;
                if (__builtin_expect((cond.bits & (VALUE_QNAN | (31ULL << 47))) == (VALUE_QNAN | TAG_BOOLEAN), 1)) {
                    isTrue = cond.bits & 1;
                } else {
                    isTrue = !(cond.isNil() || cond.isUndefined());
                }
                sp[-1] = Value(!isTrue);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_jump:
#else
        case OpCode::JUMP:
#endif
            {
                uint16_t offset = READ_SHORT();
                ip += offset;
            }
            DISPATCH();

        case OpCode::FS_CREATE:
            {
                Value contentVal = *(--sp);
                Value pathVal = *(--sp);
                if (!pathVal.isString() || !contentVal.isString()) {
                    runtimeError("fs.create requires string path and content.", getLine(frame, ip));
                    return;
                }
                std::string path = *pathVal.asString();
                std::ifstream check(path);
                if (check.good()) {
                    auto res = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                    Value::registerObject(res);
                    auto errStr = std::make_shared<std::string>("File already exists");
                    Value::registerString(errStr);
                    res->push_back({"error", Value(errStr.get())});
                    *sp++ = Value(res.get());
                } else {
                    std::ofstream out(path);
                    out << *contentVal.asString();
                    out.close();
                    *sp++ = Value(Type::NULL_VAL);
                }
            }
            DISPATCH();

        case OpCode::FS_OVERWRITE:
            {
                Value optionsVal = *(--sp);
                Value contentVal = *(--sp);
                Value pathVal = *(--sp);
                if (!pathVal.isString() || !contentVal.isString()) {
                    runtimeError("fs.overwrite requires string path and content.", getLine(frame, ip));
                    return;
                }
                std::string path = *pathVal.asString();
                std::string content = *contentVal.asString();

                if (optionsVal.isObject()) {
                    auto* obj = optionsVal.asObject();
                    bool handled = false;
                    for (auto& pair : *obj) {
                        if (pair.first == "line" && pair.second.isNumber()) {
                            int lineNum = (int)pair.second.asNumber();
                            if (lineNum > 0) {
                                std::vector<std::string> lines;
                                std::string line;
                                std::ifstream in(path);
                                while (std::getline(in, line)) lines.push_back(line);
                                in.close();

                                if (lineNum <= (int)lines.size()) {
                                    lines[lineNum - 1] = content;
                                } else {
                                    while ((int)lines.size() < lineNum - 1) lines.push_back("");
                                    lines.push_back(content);
                                }

                                std::ofstream out(path, std::ios::trunc);
                                for (size_t i = 0; i < lines.size(); ++i) {
                                    out << lines[i] << "\n";
                                }
                                out.close();
                                *sp++ = Value(Type::NULL_VAL);
                                handled = true;
                                break;
                            }
                        }
                    }
                    if (handled) {
                        DISPATCH();
                    }
                }

                std::ofstream out(path, std::ios::trunc);
                out << content;
                out.close();
                *sp++ = Value(Type::NULL_VAL);
            }
            DISPATCH();

        case OpCode::FS_APPEND:
            {
                Value optionsVal = *(--sp);
                Value contentVal = *(--sp);
                Value pathVal = *(--sp);
                if (!pathVal.isString() || !contentVal.isString()) {
                    runtimeError("fs.append requires string path and content.", getLine(frame, ip));
                    return;
                }
                std::string path = *pathVal.asString();
                std::string content = *contentVal.asString();

                if (optionsVal.isObject()) {
                    auto* obj = optionsVal.asObject();
                    bool handled = false;
                    for (auto& pair : *obj) {
                        if (pair.first == "line" && pair.second.isNumber()) {
                            int lineNum = (int)pair.second.asNumber();
                            if (lineNum > 0) {
                                std::vector<std::string> lines;
                                std::string line;
                                std::ifstream in(path);
                                while (std::getline(in, line)) lines.push_back(line);
                                in.close();

                                if (lineNum <= (int)lines.size()) {
                                    lines.insert(lines.begin() + lineNum - 1, content);
                                } else {
                                    while ((int)lines.size() < lineNum - 1) lines.push_back("");
                                    lines.push_back(content);
                                }

                                std::ofstream out(path, std::ios::trunc);
                                for (size_t i = 0; i < lines.size(); ++i) {
                                    out << lines[i] << "\n";
                                }
                                out.close();
                                *sp++ = Value(Type::NULL_VAL);
                                handled = true;
                                break;
                            }
                        }
                    }
                    if (handled) {
                        DISPATCH();
                    }
                }

                std::ofstream out(path, std::ios::app);
                out << content;
                out.close();
                *sp++ = Value(Type::NULL_VAL);
            }
            DISPATCH();

        case OpCode::FS_DELETE:
            {
                Value pathVal = *(--sp);
                if (!pathVal.isString()) {
                    runtimeError("fs.delete requires string path.", getLine(frame, ip));
                    return;
                }
                std::remove(pathVal.asString()->c_str());
                *sp++ = Value(Type::NULL_VAL);
            }
            DISPATCH();

        case OpCode::FS_READ:
            {
                Value pathVal = *(--sp);
                if (!pathVal.isString()) {
                    runtimeError("fs.read requires string path.", getLine(frame, ip));
                    return;
                }
                std::ifstream in(*pathVal.asString());
                if (!in.is_open()) {
                    *sp++ = Value(Type::NULL_VAL);
                } else {
                    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    auto s = std::make_shared<std::string>(content);
                    Value::registerString(s);
                    *sp++ = Value(s.get());
                }
            }
            DISPATCH();

        case OpCode::TRIM:
            {
                Value arg = *(--sp);
                if (!arg.isString()) {
                    runtimeError("string.trim requires a string argument.", getLine(frame, ip));
                    return;
                }
                std::string s = *arg.asString();
                size_t first = s.find_first_not_of(" \t\n\r");
                if (first == std::string::npos) {
                    auto res = std::make_shared<std::string>("");
                    Value::registerString(res);
                    *sp++ = Value(res.get());
                } else {
                    size_t last = s.find_last_not_of(" \t\n\r");
                    auto res = std::make_shared<std::string>(s.substr(first, (last - first + 1)));
                    Value::registerString(res);
                    *sp++ = Value(res.get());
                }
            }
            DISPATCH();

        case OpCode::FS_INFO:
            {
                Value pathVal = *(--sp);
                if (!pathVal.isString()) {
                    runtimeError("fs.info requires string path.", getLine(frame, ip));
                    return;
                }
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

                auto info = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                Value::registerObject(info);
                
                auto makeStrValue = [&](const std::string& s) {
                    auto str = std::make_shared<std::string>(s);
                    Value::registerString(str);
                    return Value(str.get());
                };

                info->push_back({"path", makeStrValue(absPathStr)});
                info->push_back({"dirname", makeStrValue(dirStr)});
                info->push_back({"name", makeStrValue(nameStr)});
                info->push_back({"ext", makeStrValue(extStr)});
                info->push_back({"size", Value(size)});
                info->push_back({"length", Value(size)});
                info->push_back({"exists", Value(exists)});

                if (exists) {
                    auto mDate = std::make_shared<DateData>((double)st.st_mtime * 1000.0);
                    Value::registerDate(mDate);
                    info->push_back({"modifiedAt", Value(mDate.get())});
                    
                    auto cDate = std::make_shared<DateData>((double)st.st_ctime * 1000.0);
                    Value::registerDate(cDate);
                    info->push_back({"createdAt", Value(cDate.get())});
                } else {
                    info->push_back({"modifiedAt", Value(Type::NULL_VAL)});
                    info->push_back({"createdAt", Value(Type::NULL_VAL)});
                }
                
                *sp++ = Value(info.get());
            }
            DISPATCH();

        case OpCode::STRING_SIZE:
            {
                Value arg = *(--sp);
                if (!arg.isString()) {
                    runtimeError("string.size/length requires a string argument.", getLine(frame, ip));
                    return;
                }
                *sp++ = Value((double)arg.asString()->size());
            }
            DISPATCH();

        case OpCode::FS_READ_JSON:
            {
                Value pathVal = *(--sp);
                if (!pathVal.isString()) {
                    runtimeError("fs.readJson requires string path.", getLine(frame, ip));
                    return;
                }
                std::ifstream in(*pathVal.asString());
                if (!in.is_open()) {
                    *sp++ = Value(Type::NULL_VAL);
                } else {
                    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    size_t pos = 0;
                    
                    auto skipWS = [](const std::string& j, size_t& p) {
                        while (p < j.size() && isspace(j[p])) p++;
                    };
                    
                    std::function<Value(const std::string&, size_t&)> parseValue;
                    parseValue = [&](const std::string& j, size_t& p) -> Value {
                        skipWS(j, p);
                        if (p >= j.size()) throw std::runtime_error("Unexpected end of JSON");
                        char c = j[p];
                        if (c == '"') {
                            p++;
                            std::string s = "";
                            while (p < j.size() && j[p] != '"') {
                                if (j[p] == '\\') {
                                    p++;
                                    if (p < j.size()) {
                                        if (j[p] == 'n') s += '\n';
                                        else if (j[p] == 'r') s += '\r';
                                        else if (j[p] == 't') s += '\t';
                                        else s += j[p];
                                    }
                                } else s += j[p];
                                p++;
                            }
                            if (p < j.size()) p++;
                            auto str = std::make_shared<std::string>(s);
                            Value::registerString(str);
                            return Value(str.get());
                        } else if (c == '{') {
                            p++;
                            auto obj = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                            Value::registerObject(obj);
                            skipWS(j, p);
                            if (p < j.size() && j[p] != '}') {
                                while (true) {
                                    skipWS(j, p);
                                    if (j[p] != '"') throw std::runtime_error("Expected \" for JSON key");
                                    p++;
                                    std::string key = "";
                                    while (p < j.size() && j[p] != '"') key += j[p++];
                                    if (p < j.size()) p++;
                                    skipWS(j, p);
                                    if (p >= j.size() || j[p] != ':') throw std::runtime_error("Expected : in JSON object");
                                    p++;
                                    obj->push_back({key, parseValue(j, p)});
                                    skipWS(j, p);
                                    if (p < j.size() && j[p] == ',') { p++; continue; }
                                    break;
                                }
                            }
                            if (p < j.size()) p++;
                            return Value(obj.get());
                        } else if (c == '[') {
                            p++;
                            auto arr = std::make_shared<std::vector<Value>>();
                            Value::registerArray(arr);
                            skipWS(j, p);
                            if (p < j.size() && j[p] != ']') {
                                while (true) {
                                    arr->push_back(parseValue(j, p));
                                    skipWS(j, p);
                                    if (p < j.size() && j[p] == ',') { p++; continue; }
                                    break;
                                }
                            }
                            if (p < j.size()) p++;
                            return Value(arr.get());
                        } else if (isdigit(c) || c == '-') {
                            size_t start = p;
                            if (c == '-') p++;
                            while (p < j.size() && (isdigit(j[p]) || j[p] == '.')) p++;
                            return Value(std::stod(j.substr(start, p - start)));
                        } else if (j.compare(p, 4, "true") == 0) { p += 4; return Value(true); }
                        else if (j.compare(p, 5, "false") == 0) { p += 5; return Value(false); }
                        else if (j.compare(p, 4, "null") == 0) { p += 4; return Value(Type::NULL_VAL); }
                        throw std::runtime_error("Invalid JSON character");
                    };

                    try {
                        *sp++ = parseValue(content, pos);
                    } catch (const std::exception& e) {
                        runtimeError(std::string("JSON Parse Error: ") + e.what(), getLine(frame, ip));
                        return;
                    }
                }
            }
            DISPATCH();

        case OpCode::FS_WRITE_JSON:
            {
                Value val = *(--sp);
                Value pathVal = *(--sp);
                if (!pathVal.isString()) {
                    runtimeError("fs.writeJson requires string path.", getLine(frame, ip));
                    return;
                }
                std::string path = *pathVal.asString();

                std::function<std::string(const Value&, int)> stringify;
                stringify = [&](const Value& v, int indent) -> std::string {
                    if (v.isNumber()) {
                        std::string s = std::to_string(v.asNumber());
                        if (s.find('.') != std::string::npos) {
                            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
                            if (s.back() == '.') s.pop_back();
                        }
                        return s;
                    }
                    if (v.isString()) {
                        std::string s = *v.asString();
                        std::string res = "\"";
                        for (char ch : s) {
                            if (ch == '"') res += "\\\"";
                            else if (ch == '\\') res += "\\\\";
                            else if (ch == '\n') res += "\\n";
                            else if (ch == '\r') res += "\\r";
                            else if (ch == '\t') res += "\\t";
                            else res += ch;
                        }
                        res += "\"";
                        return res;
                    }
                    if (v.isBoolean()) return v.asBoolean() ? "true" : "false";
                    if (v.isNil()) return "null";
                    if (v.isObject()) {
                        auto* obj = v.asObject();
                        std::string res = "{\n";
                        std::string outerPad(indent, ' ');
                        std::string innerPad(indent + 2, ' ');
                        for (size_t i = 0; i < obj->size(); ++i) {
                            res += innerPad + "\"" + (*obj)[i].first + "\": " + stringify((*obj)[i].second, indent + 2);
                            if (i < obj->size() - 1) res += ",";
                            res += "\n";
                        }
                        res += outerPad + "}";
                        return res;
                    }
                    if (v.isArray()) {
                        auto* arr = v.asArray();
                        std::string res = "[\n";
                        std::string outerPad(indent, ' ');
                        std::string innerPad(indent + 2, ' ');
                        for (size_t i = 0; i < arr->size(); ++i) {
                            res += innerPad + stringify((*arr)[i], indent + 2);
                            if (i < arr->size() - 1) res += ",";
                            res += "\n";
                        }
                        res += outerPad + "]";
                        return res;
                    }
                    return "null";
                };

                std::ofstream out(path);
                out << stringify(val, 0);
                out.close();
                *sp++ = Value(Type::NULL_VAL);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_jump_if_false:
#else
        case OpCode::JUMP_IF_FALSE:
#endif
            {
                uint16_t offset = READ_SHORT();
                Value cond = sp[-1];
                sp--;
                if (__builtin_expect((cond.bits & (VALUE_QNAN | (31ULL << 47))) == (VALUE_QNAN | TAG_BOOLEAN), 1)) {
                    if (!(cond.bits & 1)) ip += offset;
                } else {
                    if (cond.isNil() || cond.isUndefined()) ip += offset;
                }
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_loop:
#else
        case OpCode::LOOP:
#endif
            {
                uint16_t offset = READ_SHORT();
                ip -= offset;
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_for_iter:
#else
        case OpCode::FOR_ITER:
#endif
            {
                uint16_t offset = READ_SHORT();
                Value& indexVal = sp[-1];
                Value& arrayVal = sp[-2];
                if (!arrayVal.isArray()) {
                    runtimeError("For loop collection must be an array", getLine(frame, ip));
                    return;
                }
                auto* array = arrayVal.asArray();
                int index = (int)indexVal.asNumber();
                if (index >= (int)array->size()) {
                    ip += offset;
                } else {
                    indexVal = Value((double)(index + 1));
                    *sp++ = (*array)[index];
                }
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_call:
#else
        case OpCode::CALL:
#endif
            {
                int argCount = READ_BYTE();
            re_dispatch_call:
                Value callee = sp[-(argCount + 1)];
                if (!callee.isFunction()) {
                    runtimeError("Can only call functions.", getLine(frame, ip));
                    return;
                }
                
                ICallable* func = callee.asFunction();
                if (func->isBoundMethod()) {
                    BoundMethod* bm = static_cast<BoundMethod*>(func);
                    // Prepend 'this' to arguments on stack
                    for (int i = 0; i < argCount; i++) {
                        sp[-i] = sp[-(i + 1)];
                    }
                    sp++;
                    argCount++;
                    sp[-argCount] = bm->instance;
                    callee = bm->method->isVMFunction() ? Value(static_cast<VMFunction*>(bm->method)) : Value(bm->method, false);
                    sp[-(argCount + 1)] = callee;
                    goto re_dispatch_call;
                }
                
                if (callee.isVMFunction()) {
                    VMFunction* vmFunc = reinterpret_cast<VMFunction*>(callee.asFunction());
                    
                    if (vmFunc->hasRest) {
                        int regularParams = vmFunc->arity - 1;
                        auto* restArr = new std::vector<Value>();
                        int restCount = argCount - regularParams;
                        if (restCount > 0) {
                            for (int i = 0; i < restCount; i++) {
                                restArr->push_back(sp[-(restCount - i)]);
                            }
                            sp -= restCount;
                        } else if (restCount < 0) {
                            for (int i=0; i < -restCount; i++) *sp++ = Value(Type::UNDEFINED);
                        }
                        *sp++ = Value(restArr);
                        argCount = vmFunc->arity;
                    }
                    
                    if (*ip == static_cast<uint8_t>(OpCode::RETURN)) {
                        int base = frame->stackBase;
                        for (int i = 0; i < argCount; i++) {
                            stackPtr[base + i] = sp[-argCount + i];
                        }
                        sp = stackPtr + base + argCount;
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        ip = frame->ip;
                        constantsPtr = vmFunc->chunk.constants.data();
                        for (int i = argCount; i < vmFunc->localCount; i++) *sp++ = Value();
                    } else {
                        frame->ip = ip;
                        frame++;
                        if (__builtin_expect(frame >= framesEnd, 0)) {
                            runtimeError("Stack overflow (CallFrame).", getLine(frame, ip));
                            return;
                        }
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        frame->stackBase = (int)(sp - stackPtr) - argCount;
                        ip = frame->ip;
                        fp = sp - argCount;
                        constantsPtr = vmFunc->chunk.constants.data();
                        for (int i = argCount; i < vmFunc->localCount; i++) *sp++ = Value();
                    }
                } else {
                    ICallable* native = callee.asFunction();
                    std::vector<Value> args;
                    for (int i = 0; i < argCount; ++i) args.push_back(*(--sp));
                    std::reverse(args.begin(), args.end());
                    --sp; // callee
                    Value result = native->call(interpreter, args);
                    *sp++ = result;
                }
            }
            DISPATCH();
            
#if 0 && defined(__GNUC__)
        op_rest_object:
#else
        case OpCode::REST_OBJECT:
#endif
            {
                int excludeCount = READ_BYTE();
                std::unordered_set<std::string> excluded;
                for (int i = 0; i < excludeCount; i++) {
                    excluded.insert(*constantsPtr[READ_BYTE()].asString());
                }
                Value objVal = *(--sp);
                if (!objVal.isObject()) {
                    runtimeError("Rest destructuring expects object", getLine(frame, ip));
                    return;
                }
                auto* obj = objVal.asObject();
                auto newObj = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                Value::registerObject(newObj);
                for (const auto& pair : *obj) {
                    if (excluded.find(pair.first) == excluded.end()) {
                        newObj->push_back(pair);
                    }
                }
                *sp++ = Value(newObj.get());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_warn:
#else
        case OpCode::WARN:
#endif
            {
                uint8_t argCount = READ_BYTE();
                Value last = (argCount > 0) ? sp[-1] : Value();
                if (showWarnings) {
                    if (useColor) std::cout << Color::Yellow << "[Warning] " << Color::Reset;
                    else std::cout << "[Warning] ";
                    
                    for (int i = 0; i < argCount; ++i) {
                        std::cout << sp[-argCount + i].toString();
                        if (i < argCount - 1) std::cout << " ";
                    }

                    int line = getLine(frame, ip);
                    if (line != -1) {
                        if (useColor) std::cout << Color::Cyan << " (line " << line << ")" << Color::Reset;
                        else std::cout << " (line " << line << ")";
                    }
                    std::cout << std::endl;
                }
                sp -= argCount;
                *sp++ = last;
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_import_native:
#else
        case OpCode::IMPORT_NATIVE:
#endif
            {
                uint8_t modIdx = *ip++;
                uint8_t symIdx = *ip++;
                std::string* modName = constantsPtr[modIdx].asString();
                std::string* symName = constantsPtr[symIdx].asString();

                void* handle = nullptr;
                auto it = interpreter.nativeModules.find(*modName);
                if (it != interpreter.nativeModules.end()) {
                    handle = it->second;
                } else {
                    std::string libPath = "./lib" + *modName + ".so";
                    handle = dlopen(libPath.c_str(), RTLD_NOW);
                    if (!handle) {
                        std::string modPath = "./modules/lib" + *modName + ".so";
                        handle = dlopen(modPath.c_str(), RTLD_NOW);
                    }
                    if (!handle) {
                        handle = dlopen((*modName).c_str(), RTLD_NOW);
                    }
                    if (!handle) {
                        runtimeError("Could not load native module: " + *modName + " (" + dlerror() + ")", getLine(frame, ip));
                        return;
                    }
                    interpreter.nativeModules[*modName] = handle;
                }

                void* sym = dlsym(handle, symName->c_str());
                if (!sym) {
                    runtimeError("Could not find symbol '" + *symName + "' in module '" + *modName + "'", getLine(frame, ip));
                    return;
                }

                auto funcPtr = reinterpret_cast<uint64_t(*)(Interpreter&, const std::vector<Value>&)>(sym);
                NativeFunction::NativeFunc nativeFunc = [funcPtr](Interpreter& interp, const std::vector<Value>& args) {
                    return Value(funcPtr(interp, args));
                };
                auto nativeFuncPtr = std::make_shared<NativeFunction>(nativeFunc);
                Value::registerFunction(nativeFuncPtr);
                *sp++ = Value(nativeFuncPtr.get(), true);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_spread_args:
#else
        case OpCode::SPREAD_ARGS:
#endif
            {
                Value argsArray = *(--sp);
                if (!argsArray.isArray()) {
                    runtimeError("Spread arguments must be an array", getLine(frame, ip));
                    return;
                }
                auto* arr = argsArray.asArray();
                int argCount = arr->size();
                for (const auto& val : *arr) {
                    *sp++ = val;
                }
                
                Value callee = sp[-(argCount + 1)];
                if (!callee.isFunction()) {
                    runtimeError("Can only call functions.", getLine(frame, ip));
                    return;
                }
                
                if (callee.isVMFunction()) {
                    VMFunction* vmFunc = reinterpret_cast<VMFunction*>(callee.asFunction());
                    
                    if (vmFunc->hasRest) {
                        int regularParams = vmFunc->arity - 1;
                        auto* restArr = new std::vector<Value>();
                        int restCount = argCount - regularParams;
                        if (restCount > 0) {
                            for (int i = 0; i < restCount; i++) {
                                restArr->push_back(sp[-(restCount - i)]);
                            }
                            sp -= restCount;
                        } else if (restCount < 0) {
                            for (int i=0; i < -restCount; i++) *sp++ = Value(Type::UNDEFINED);
                        }
                        *sp++ = Value(restArr);
                        argCount = vmFunc->arity;
                    }
                    
                    if (*ip == static_cast<uint8_t>(OpCode::RETURN)) {
                        int base = frame->stackBase;
                        for (int i = 0; i < argCount; i++) {
                            stackPtr[base + i] = sp[-argCount + i];
                        }
                        sp = stackPtr + base + argCount;
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        ip = frame->ip;
                        constantsPtr = vmFunc->chunk.constants.data();
                        for (int i = argCount; i < vmFunc->localCount; i++) *sp++ = Value();
                    } else {
                        frame->ip = ip;
                        frame++;
                        if (__builtin_expect(frame >= framesEnd, 0)) throw std::runtime_error("Stack overflow (CallFrame).");
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        frame->stackBase = (int)(sp - stackPtr) - argCount;
                        ip = frame->ip;
                        fp = sp - argCount;
                        constantsPtr = vmFunc->chunk.constants.data();
                        for (int i = argCount; i < vmFunc->localCount; i++) *sp++ = Value();
                    }
                } else {
                    ICallable* native = callee.asFunction();
                    std::vector<Value> args;
                    for (int i = 0; i < argCount; ++i) args.push_back(*(--sp));
                    std::reverse(args.begin(), args.end());
                    --sp; // callee
                    Value result = native->call(interpreter, args);
                    *sp++ = result;
                }
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_append_array:
#else
        case OpCode::APPEND_ARRAY:
#endif
            {
                Value b = *(--sp);
                Value a = *(sp - 1);
                if (!a.isArray() || !b.isArray()) throw std::runtime_error("Spread expects arrays");
                auto* arrA = a.asArray();
                auto* arrB = b.asArray();
                arrA->insert(arrA->end(), arrB->begin(), arrB->end());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_append_element:
#else
        case OpCode::APPEND_ELEMENT:
#endif
            {
                Value b = *(--sp);
                Value a = *(sp - 1);
                a.asArray()->push_back(b);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_merge_object:
#else
        case OpCode::MERGE_OBJECT:
#endif
            {
                Value b = *(--sp);
                Value a = *(sp - 1);
                if (!a.isObject() || !b.isObject()) throw std::runtime_error("Spread expects objects");
                auto* objA = a.asObject();
                auto* objB = b.asObject();
                objA->insert(objA->end(), objB->begin(), objB->end());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_merge_property:
#else
        case OpCode::MERGE_PROPERTY:
#endif
            {
                Value val = *(--sp);
                Value key = *(--sp);
                Value a = *(sp - 1);
                if (!a.isObject()) throw std::runtime_error("Merge property expects object");
                auto* objA = a.asObject();
                objA->emplace_back(*key.asString(), val);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_slice_array:
#else
        case OpCode::SLICE_ARRAY:
#endif
            {
                Value indexVal = *(--sp);
                Value arrVal = *(--sp);
                if (!arrVal.isArray()) throw std::runtime_error("Rest destructuring expects array");
                int start = (int)indexVal.asNumber();
                auto* arr = arrVal.asArray();
                auto* newArr = new std::vector<Value>();
                if (start < (int)arr->size()) {
                    newArr->insert(newArr->end(), arr->begin() + start, arr->end());
                }
                *sp++ = Value(newArr);
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_return:
#else
        case OpCode::RETURN:
#endif
            {
                Value result = *(--sp);
                int base = frame->stackBase;
                
                if (__builtin_expect(frame == framesPtr, 0)) return;
                frame--;
                
                ip = frame->ip;
                fp = stackPtr + frame->stackBase;
                constantsPtr = frame->function->chunk.constants.data();
                sp = stackPtr + base - 1;
                *sp++ = result;
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_print:
#else
        case OpCode::PRINT:
#endif
            {
                int argCount = READ_BYTE();
                for (int i = 0; i < argCount; i++) {
                    Value val = sp[-(argCount - i)];
                    std::cout << val.toString() << (i == argCount - 1 ? "" : " ");
                }
                std::cout << std::endl;
                sp -= argCount;
                *sp++ = Value();
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_dup:
#else
        case OpCode::DUP:
#endif
            {
                Value top = sp[-1];
                *sp++ = top;
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_console_args:
#else
        case OpCode::CONSOLE_ARGS:
#endif
            {
                auto arr = std::make_shared<std::vector<Value>>();
                Value::registerArray(arr);
                for (const auto& arg : cliArgs) {
                    auto s = std::make_shared<std::string>(arg);
                    Value::registerString(s);
                    arr->push_back(Value(s.get()));
                }
                *sp++ = Value(arr.get());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_console_read:
#else
        case OpCode::CONSOLE_READ:
#endif
            {
                std::string input;
                if (!std::getline(std::cin, input)) {
                    *sp++ = Value(Type::UNDEFINED);
                } else {
                    auto s = std::make_shared<std::string>(input);
                    Value::registerString(s);
                    *sp++ = Value(s.get());
                }
            }
            DISPATCH();
#if 0 && defined(__GNUC__)
        op_class:
#else
        case OpCode::CLASS:
#endif
            {
                Value nameVal = constantsPtr[READ_BYTE()];
                bool isAbstract = READ_BYTE() != 0;
                int propCount = READ_BYTE();
                int methodCount = READ_BYTE();
                
                auto klass = std::make_shared<SpClass>(*nameVal.asString(), isAbstract);
                Value::registerClass(klass);
                
                // Methods (Name, Func)
                for (int i = 0; i < methodCount; i++) {
                    Value methodFunc = *(--sp);
                    Value methodName = *(--sp);
                    klass->methods[*methodName.asString()] = methodFunc;
                }
                
                // Properties (Name, Meta, Init)
                klass->properties.resize(propCount);
                for (int i = propCount - 1; i >= 0; i--) {
                    Value init = *(--sp);
                    Value metaVal = *(--sp);
                    double meta = metaVal.asNumber();
                    Value nameValProp = *(--sp);
                    
                    klass->properties[i].name = *nameValProp.asString();
                    klass->properties[i].isReadonly = ((int)meta & 1) != 0;
                    klass->properties[i].isPrivate = ((int)meta & 2) != 0;
                    klass->properties[i].initializer_value = init;
                }
                *sp++ = Value(klass.get());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_process_run:
#else
        case OpCode::PROCESS_RUN:
#endif
            {
                int argCount = READ_BYTE();
                Value argsVal = argCount > 0 ? sp[-1] : Value();
                if (argCount > 0) sp--;
                Value cmdVal = sp[-1];
                sp--;

                if (!cmdVal.isString()) {
                    runtimeError("process.run requires a string command.", getLine(frame, ip));
                    return;
                }

                std::string command = *cmdVal.asString();
                if (argsVal.isArray()) {
                    auto array = argsVal.asArray();
                    for (const auto& arg : *array) {
                        command += " " + arg.toPureString();
                    }
                }

                std::string output;
                char buffer[128];
                FILE* pipe = popen((command + " 2>&1").c_str(), "r");
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

                auto res = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                Value::registerObject(res);
                auto outStr = std::make_shared<std::string>(output);
                Value::registerString(outStr);
                res->push_back({"output", Value(outStr.get())});
                res->push_back({"status", Value((double)status)});
                res->push_back({"failed", Value(status != 0)});
                *sp++ = Value(res.get());
            }
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_process_spawn:
#else
        case OpCode::PROCESS_SPAWN:
#endif
            {
                int argCount = READ_BYTE();
                Value argsVal = argCount > 0 ? sp[-1] : Value();
                if (argCount > 0) sp--;
                Value cmdVal = sp[-1];
                sp--;

                if (!cmdVal.isString()) {
                    runtimeError("process.spawn requires a string command.", getLine(frame, ip));
                    return;
                }

                std::string command = *cmdVal.asString();
                if (argsVal.isArray()) {
                    auto array = argsVal.asArray();
                    for (const auto& arg : *array) {
                        command += " " + arg.toPureString();
                    }
                }
                
                system((command + " &").c_str());
                
                auto res = std::make_shared<std::vector<std::pair<std::string, Value>>>();
                Value::registerObject(res);
                res->push_back({"spawned", Value(true)});
                *sp++ = Value(res.get());
            }
            DISPATCH();
        } // switch
    } // while
} // VM::run

Value VM::call(Interpreter& interpreter, ICallable* callable, const std::vector<Value>& args) {
    if (!callable) return Value(Type::UNDEFINED);
    
    if (callable->isNative()) {
        return callable->call(interpreter, args);
    }
    
    if (!callable->isVMFunction()) {
        // Fallback to AST interpretation if it's a SpFunction
        return callable->call(interpreter, args);
    }

    VMFunction* func = static_cast<VMFunction*>(callable);
    
    // Set up new frame
    CallFrame* newFrame = frame + 1;
    if (newFrame >= frames.get() + FRAMES_MAX) {
        throw std::runtime_error("Stack overflow");
    }
    
    newFrame->function = func;
    newFrame->ip = func->chunk.code.data();
    newFrame->stackBase = (int)(sp - stack.get());
    
    // Save current frame state to restore later
    CallFrame* oldFrame = frame;
    Value* oldSp = sp;
    Value* oldFp = fp;
    
    // Arguments and locals
    for (int i = 0; i < (int)args.size() && i < func->arity; ++i) *sp++ = args[i];
    for (int i = (int)args.size(); i < func->localCount; ++i) *sp++ = Value();
    
    frame = newFrame;
    fp = stack.get() + frame->stackBase;
    
    // Mini instruction loop for the call
    uint8_t* ip = frame->ip;
    Value* constantsPtr = frame->function->chunk.constants.data();

    // Note: This is a simplified loop that recursively calls run() but with a limited scope.
    // To properly support all features, we'd need to refactor run() to be re-entrant.
    while (true) {
        uint8_t opcode = *ip++;
        switch (static_cast<OpCode>(opcode)) {
            case OpCode::CONSTANT: *sp++ = constantsPtr[*ip++]; break;
            case OpCode::NULL_VAL: *sp++ = Value(Type::NULL_VAL); break;
            case OpCode::TRUE_VAL: *sp++ = Value(true); break;
            case OpCode::FALSE_VAL: *sp++ = Value(false); break;
            case OpCode::GET_LOCAL: *sp++ = fp[*ip++]; break;
            case OpCode::SET_LOCAL: fp[*ip++] = *(sp - 1); break;
            case OpCode::GET_GLOBAL: {
                std::string name = frame->function->chunk.constants[*ip++].asString()->c_str();
                *sp++ = interpreter.environment->get(name);
                break;
            }
            case OpCode::POP: --sp; break;
            case OpCode::ADD: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() + b.asNumber()); break; }
            case OpCode::SUBTRACT: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() - b.asNumber()); break; }
            case OpCode::CALL: {
                int argCount = *ip++;
                Value funcVal = *(sp - argCount - 1);
                std::vector<Value> callArgs;
                for (int i = 0; i < argCount; ++i) callArgs.push_back(*(sp - argCount + i));
                sp -= argCount + 1;
                *sp++ = call(interpreter, funcVal.asFunction(), callArgs);
                break;
            }
            case OpCode::PRINT: {
                int argCount = *ip++;
                for (int i = 0; i < argCount; i++) {
                    Value val = sp[-(argCount - i)];
                    std::cout << val.toString() << (i == argCount - 1 ? "" : " ");
                }
                std::cout << std::endl;
                sp -= argCount;
                *sp++ = Value();
                break;
            }
            case OpCode::WARN: {
                int argCount = *ip++;
                for (int i = 0; i < argCount; i++) {
                    Value val = sp[-(argCount - i)];
                    std::cerr << val.toString() << (i == argCount - 1 ? "" : " ");
                }
                std::cerr << std::endl;
                sp -= argCount;
                *sp++ = Value();
                break;
            }
            case OpCode::MULTIPLY: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() * b.asNumber()); break; }
            case OpCode::DIVIDE: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() / b.asNumber()); break; }
            case OpCode::EQUAL: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.bits == b.bits || (a.isNumber() && b.isNumber() && a.asNumber() == b.asNumber())); break; }
            case OpCode::GREATER: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() > b.asNumber()); break; }
            case OpCode::LESS: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() < b.asNumber()); break; }
            case OpCode::GREATER_EQUAL: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() >= b.asNumber()); break; }
            case OpCode::LESS_EQUAL: { Value b = *--sp; Value a = *--sp; *sp++ = Value(a.asNumber() <= b.asNumber()); break; }
            case OpCode::NOT: { Value a = *--sp; *sp++ = Value(!a.asBoolean()); break; }
            case OpCode::JUMP: { uint8_t offset = *ip++; ip += offset; break; }
            case OpCode::JUMP_IF_FALSE: { uint8_t offset = *ip++; if (!(*--sp).asBoolean()) ip += offset; break; }
            case OpCode::GET_PROPERTY: {
                std::string prop = frame->function->chunk.constants[*ip++].asString()->c_str();
                Value obj = *--sp;
                *sp++ = sp_get_property(interpreter, obj, prop);
                break;
            }
            case OpCode::GET_ELEMENT: {
                Value idx = *--sp;
                Value obj = *--sp;
                if (!obj.isArray() || !idx.isNumber()) throw std::runtime_error("GET_ELEMENT requires array and number index.");
                auto* arr = obj.asArray();
                int i = (int)idx.asNumber();
                if (i < 0 || i >= (int)arr->size()) *sp++ = Value(Type::UNDEFINED);
                else *sp++ = (*arr)[i];
                break;
            }
            case OpCode::RETURN: {
                Value res = (sp > fp) ? *(--sp) : Value(Type::UNDEFINED);
                frame = oldFrame;
                sp = oldSp;
                fp = oldFp;
                return res;
            }
            default:
                throw std::runtime_error("VM::call nested loop unhandled opcode: " + std::to_string(opcode));
        }
    }
}
