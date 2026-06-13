#include "types.h"
#include "interpreter.h"
#include "ast.h"
#include "vm.h"
#include <sstream>
#include <iomanip>

bool Value::useColor = true;
Heap Value::GlobalHeap;
thread_local Interpreter* Value::CurrentContext = nullptr;
std::unordered_map<std::string, std::string> Value::Layouts;

void Value::registerString(std::shared_ptr<std::string> s) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allStrings.push_back(s);
}

void Value::registerArray(std::shared_ptr<std::vector<Value>> a) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allArrays.push_back(a);
}

void Value::registerObject(std::shared_ptr<std::vector<std::pair<std::string, Value>>> o) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allObjects.push_back(o);
}

void Value::registerBigInt(std::shared_ptr<BigInt> b) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allBigInts.push_back(b);
}

void Value::registerDate(std::shared_ptr<DateData> d) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allDates.push_back(d);
}

void Value::registerMap(std::shared_ptr<MapData> m) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allMaps.push_back(m);
}

void Value::registerError(std::shared_ptr<ErrorData> e) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allErrors.push_back(e);
}

void Value::registerRegex(std::shared_ptr<RegexData> r) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allRegexes.push_back(r);
}

void Value::registerFuture(std::shared_ptr<FutureData> f) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allFutures.push_back(f);
}

void Value::registerTimer(std::shared_ptr<TimerData> t) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allTimers.push_back(t);
}

void Value::registerInstance(std::shared_ptr<SpInstance> i) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allInstances.push_back(i);
}

void Value::joinAllFutures() {
    std::vector<std::shared_ptr<FutureData>> futures_copy;
    {
        std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
        futures_copy = GlobalHeap.allFutures;
    }
    for (auto& futData : futures_copy) {
        if (!futData->is_ready) {
            futData->get();
        }
    }
}

void Value::registerClass(std::shared_ptr<SpClass> c) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allClasses.push_back(c);
}

void Value::registerFunction(std::shared_ptr<ICallable> f) {
    std::lock_guard<std::recursive_mutex> lock(GlobalHeap.mutex);
    GlobalHeap.allFunctions.push_back(f);
}

size_t ValueHash::operator()(const Value& v) const {
    if (v.isError()) return (size_t)v.bits;
    if (v.isNumber()) return std::hash<double>{}(v.asNumber());
    if (v.isString()) return std::hash<std::string>{}(*v.asString());
    if (v.isBigInt()) return std::hash<std::string>{}(v.asBigInt()->str());
    return std::hash<uint64_t>{}(v.bits);
}

bool ValueEqual::operator()(const Value& a, const Value& b) const {
    if (a.isError() || b.isError()) return a.bits == b.bits;
    if (a.isNumber() && b.isNumber()) return a.asNumber() == b.asNumber();
    if (a.isString() && b.isString()) return *a.asString() == *b.asString();
    if (a.isBigInt() && b.isBigInt()) return *a.asBigInt() == *b.asBigInt();
    return a.bits == b.bits;
}

std::string Value::toString() const {
    std::string prefix = "";
    std::string suffix = "";
    if (useColor) {
        if (isNumber()) prefix = Color::Cyan;
        else if (isBoolean()) prefix = Color::Yellow;
        else if (isNil() || isUndefined()) prefix = Color::Red;
        else if (isString()) prefix = Color::Green;
        else if (isArray() || isObject()) prefix = Color::Blue;
        else if (isFunction()) prefix = Color::Magenta;
        else if (isBigInt()) prefix = Color::Cyan; // Use same as number for now
        else if (isDate()) prefix = Color::Green;  // Use same as string
        else if (isMap()) prefix = Color::Blue;    // Use same as object
        else if (isRegex()) prefix = Color::Red;   // Distinct color for regex
        else if (isFuture()) prefix = Color::Cyan; // Use cyan for future
        else if (isTimer()) prefix = Color::Cyan;  // Use cyan for timer
        suffix = Color::Reset;
    }

    if (isNumber()) {
        std::ostringstream oss;
        double num = asNumber();
        if (num == (long long)num) oss << (long long)num;
        else oss << num;
        return prefix + oss.str() + suffix;
    }
    if (isNil()) return prefix + "null" + suffix;
    if (isUndefined()) return prefix + "undefined" + suffix;
    if (isBoolean()) return prefix + (asBoolean() ? "true" : "false") + suffix;
    if (isString()) return prefix + *asString() + suffix;
    if (isArray()) {
        std::string res = prefix + "[" + suffix;
        auto& arr = *asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            res += arr[i].toString() + (i == arr.size() - 1 ? "" : ", ");
        }
        return res + prefix + "]" + suffix;
    }
    if (isObject()) {
        std::string res = prefix + "{" + suffix;
        auto& obj = *asObject();
        for (size_t i = 0; i < obj.size(); ++i) {
            res += obj[i].first + ": " + obj[i].second.toString() + (i == obj.size() - 1 ? "" : ", ");
        }
        return res + prefix + "}" + suffix;
    }
    if (isFunction()) return prefix + "[function]" + suffix;
    if (isBigInt()) return prefix + asBigInt()->str() + suffix;
    if (isDate()) {
        std::ostringstream oss;
        oss << "[Date " << (long long)asDate()->timestamp << "]";
        return prefix + oss.str() + suffix;
    }
    if (isMap()) {
        std::string res = prefix + "Map {" + suffix;
        auto& m = asMap()->map;
        size_t i = 0;
        for (auto const& [key, val] : m) {
            res += key.toString() + " => " + val.toString() + (i == m.size() - 1 ? "" : ", ");
            i++;
        }
        return res + prefix + "}" + suffix;
    }
    if (isRegex()) {
        return prefix + "/" + asRegex()->pattern + "/" + suffix;
    }
    if (isFuture()) return asFuture()->get().toString();
    if (isTimer()) return prefix + "[Timer]" + suffix;
    if (isError()) return prefix + "[Error: " + asError()->message + "]" + suffix;
    
    std::stringstream ss;
    ss << "unknown (bits: 0x" << std::hex << bits << ")";
    return ss.str();
}

std::string Value::toPureString() const {
    if (isNumber()) {
        std::ostringstream oss;
        double num = asNumber();
        if (num == (long long)num) oss << (long long)num;
        else oss << num;
        return oss.str();
    }
    if (isNil()) return "null";
    if (isUndefined()) return "undefined";
    if (isBoolean()) return asBoolean() ? "true" : "false";
    if (isString()) return *asString();
    if (isArray()) {
        std::string res = "[";
        auto& arr = *asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            res += arr[i].toPureString() + (i == arr.size() - 1 ? "" : ", ");
        }
        return res + "]";
    }
    if (isObject()) {
        std::string res = "{";
        auto& obj = *asObject();
        for (size_t i = 0; i < obj.size(); ++i) {
            res += obj[i].first + ": " + obj[i].second.toPureString() + (i == obj.size() - 1 ? "" : ", ");
        }
        return res + "}";
    }
    if (isFunction()) return "[function]";
    if (isBigInt()) return asBigInt()->str();
    if (isDate()) return std::to_string((long long)asDate()->timestamp);
    if (isMap()) return "[Map]";
    if (isError()) return "[Error: " + asError()->message + "]";
    if (isRegex()) return "/" + asRegex()->pattern + "/";
    if (isFuture()) return asFuture()->get().toPureString();
    if (isTimer()) return "[Timer]";
    return "unknown";
}

#include <algorithm>

Value Value::getBuiltinMethod(const std::string& property, Interpreter& interp) const {
    Value objVal = *this;
    
    if (objVal.isArray()) {
        auto* arr = objVal.asArray();
        if (property == "length") return Value((double)arr->size());
        if (property == "push") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                for (const auto& arg : args) arr->push_back(arg);
                return Value((double)arr->size());
            })));
        }
        if (property == "pop") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>&) {
                if (arr->empty()) return Value(Type::NULL_VAL);
                Value val = arr->back();
                arr->pop_back();
                return val;
            })));
        }
        if (property == "shift") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>&) {
                if (arr->empty()) return Value(Type::NULL_VAL);
                Value val = (*arr)[0];
                arr->erase(arr->begin());
                return val;
            })));
        }
        if (property == "unshift") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                arr->insert(arr->begin(), args.begin(), args.end());
                return Value((double)arr->size());
            })));
        }
        if (property == "join") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
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
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr, objVal](Interpreter&, const std::vector<Value>&) {
                std::reverse(arr->begin(), arr->end());
                return objVal;
            })));
        }
        if (property == "slice") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
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
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(false);
                for (const auto& item : *arr) {
                    if ((item == args[0]).asBoolean()) return Value(true);
                }
                return Value(false);
            })));
        }
        if (property == "indexOf") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(-1.0);
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (((*arr)[i] == args[0]).asBoolean()) return Value((double)i);
                }
                return Value(-1.0);
            })));
        }
        if (property == "forEach") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("forEach requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (size_t i = 0; i < arr->size(); ++i) {
                    interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                }
                return Value(Type::NULL_VAL);
            })));
        }
        if (property == "map") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
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
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
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
        if (property == "find") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("find requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (size_t i = 0; i < arr->size(); ++i) {
                    Value res = interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                    bool isTrue = (res.isBoolean()) ? res.asBoolean() : (!res.isNil() && !res.isUndefined());
                    if (isTrue) return (*arr)[i];
                }
                return Value(Type::NULL_VAL);
            })));
        }
        if (property == "findIndex") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([arr](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("findIndex requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (size_t i = 0; i < arr->size(); ++i) {
                    Value res = interp.callHandler(callback, {(*arr)[i], Value((double)i)});
                    bool isTrue = (res.isBoolean()) ? res.asBoolean() : (!res.isNil() && !res.isUndefined());
                    if (isTrue) return Value((double)i);
                }
                return Value(-1.0);
            })));
        }
    }
    
    if (objVal.isNumber()) {
        if (property == "toBigInt") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([objVal](Interpreter&, const std::vector<Value>&) {
                auto b = std::make_shared<BigInt>((int64_t)objVal.asNumber());
                Value::registerBigInt(b);
                return Value(b.get());
            })));
        }
    }
    
    if (objVal.isString()) {
        std::string* s = objVal.asString();
        if (property == "toBigInt") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                try {
                    auto b = std::make_shared<BigInt>(*s);
                    Value::registerBigInt(b);
                    return Value(b.get());
                } catch (...) {
                    throw std::runtime_error("Cannot convert string to BigInt");
                }
            })));
        }
        if (property == "length") return Value((double)s->size());
        if (property == "padStart") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                size_t targetLen = (size_t)args[0].asNumber();
                std::string padChar = (args.size() > 1 && args[1].isString()) ? *args[1].asString() : " ";
                if (s->size() >= targetLen) return Value(interp.makeString(*s));
                std::string res = "";
                size_t toPad = targetLen - s->size();
                while (res.size() < toPad) res += padChar;
                res = res.substr(0, toPad) + *s;
                return Value(interp.makeString(res));
            })));
        }
        if (property == "padEnd") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                size_t targetLen = (size_t)args[0].asNumber();
                std::string padChar = (args.size() > 1 && args[1].isString()) ? *args[1].asString() : " ";
                if (s->size() >= targetLen) return Value(interp.makeString(*s));
                std::string res = *s;
                while (res.size() < targetLen) res += padChar;
                res = res.substr(0, targetLen);
                return Value(interp.makeString(res));
            })));
        }
        if (property == "repeat") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                int count = (int)args[0].asNumber();
                std::string res = "";
                for (int i = 0; i < count; ++i) res += *s;
                return Value(interp.makeString(res));
            })));
        }
        if (property == "substring") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                size_t start = (size_t)args[0].asNumber();
                size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : s->length();
                if (start > s->length()) start = s->length();
                if (end > s->length()) end = s->length();
                if (start > end) std::swap(start, end);
                return Value(interp.makeString(s->substr(start, end - start)));
            })));
        }
        if (property == "contains" || property == "includes") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                return Value(s->find(*args[0].asString()) != std::string::npos);
            })));
        }
        if (property == "indexOf") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(-1.0);
                size_t pos = s->find(*args[0].asString());
                if (pos == std::string::npos) return Value(-1.0);
                return Value((double)pos);
            })));
        }
        if (property == "split") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                std::string sep = (args.size() > 0 && args[0].isString()) ? *args[0].asString() : "";
                auto* arr = interp.makeArray();
                if (sep.empty()) {
                    for (char c : *s) arr->push_back(Value(interp.makeString(std::string(1, c))));
                } else {
                    size_t start = 0, end;
                    while ((end = s->find(sep, start)) != std::string::npos) {
                        arr->push_back(Value(interp.makeString(s->substr(start, end - start))));
                        start = end + sep.length();
                    }
                    arr->push_back(Value(interp.makeString(s->substr(start))));
                }
                return Value(arr);
            })));
        }
        if (property == "trim") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                res.erase(0, res.find_first_not_of(" \t\n\r"));
                res.erase(res.find_last_not_of(" \t\n\r") + 1);
                return Value(interp.makeString(res));
            })));
        }
        if (property == "toLower" || property == "toLowerCase") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                std::transform(res.begin(), res.end(), res.begin(), ::tolower);
                return Value(interp.makeString(res));
            })));
        }
        if (property == "toUpper" || property == "toUpperCase") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>&) {
                std::string res = *s;
                std::transform(res.begin(), res.end(), res.begin(), ::toupper);
                return Value(interp.makeString(res));
            })));
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
        if (property == "getTime") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([objVal](Interpreter&, const std::vector<Value>&) {
                return Value(objVal.asDate()->timestamp);
            })));
        }
        if (property == "toString") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([objVal](Interpreter& interp, const std::vector<Value>&) {
                time_t ttt = (time_t)(objVal.asDate()->timestamp / 1000);
                struct tm* tm_inf = localtime(&ttt);
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_inf);
                return Value(interp.makeString(buf));
            })));
        }
    }

    if (objVal.isFuture()) {
        if (property == "wait") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([objVal](Interpreter&, const std::vector<Value>&) {
                return objVal.asFuture()->get();
            })));
        }
    }
    
    if (objVal.isMap()) {
        auto* m = objVal.asMap();
        if (property == "size") return Value((double)m->map.size());
        if (property == "set") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m, objVal](Interpreter&, const std::vector<Value>& args) {
                if (args.size() >= 2) m->map[args[0]] = args[1];
                return objVal;
            })));
        }
        if (property == "get") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(Type::UNDEFINED);
                auto it = m->map.find(args[0]);
                if (it != m->map.end()) return it->second;
                return Value(Type::UNDEFINED);
            })));
        }
        if (property == "has") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(false);
                return Value(m->map.find(args[0]) != m->map.end());
            })));
        }
        if (property == "delete") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>& args) {
                if (args.empty()) return Value(false);
                return Value(m->map.erase(args[0]) > 0);
            })));
        }
        if (property == "clear") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter&, const std::vector<Value>&) {
                m->map.clear();
                return Value();
            })));
        }
        if (property == "forEach") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isFunction()) throw std::runtime_error("Map.forEach requires a function callback.");
                ICallable* callback = args[0].asFunction();
                for (auto const& [key, val] : m->map) {
                    interp.callHandler(callback, {key, val});
                }
                return Value(Type::NULL_VAL);
            })));
        }
        if (property == "keys") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>&) {
                auto* arr = interp.makeArray();
                for (auto const& [key, val] : m->map) arr->push_back(key);
                return Value(arr);
            })));
        }
        if (property == "values") {
            return Value(interp.makeFunction(std::make_shared<NativeFunction>([m](Interpreter& interp, const std::vector<Value>&) {
                auto* arr = interp.makeArray();
                for (auto const& [key, val] : m->map) arr->push_back(val);
                return Value(arr);
            })));
        }
    }
    
    return Value(Type::UNDEFINED);
}

#include <cmath>

static inline BigInt getBigInt(const Value& v) {
    return v.isBigInt() ? *v.asBigInt() : BigInt((int64_t)v.asNumber());
}

static inline bool needsPromotion(double da, double db, double res) {
    if (std::trunc(da) == da && std::trunc(db) == db) {
        if (res > 9007199254740991.0 || res < -9007199254740991.0) return true;
    }
    return false;
}

Value Value::operator+(const Value& other) const {
    if (isError()) return *this;
    if (other.isError()) return other;
    if (isString() || other.isString()) {
        auto s = std::make_shared<std::string>(toPureString() + other.toPureString());
        registerString(s);
        return Value(s.get());
    }
    if (isBigInt() || other.isBigInt()) {
        auto b = std::make_shared<BigInt>(getBigInt(*this) + getBigInt(other));
        registerBigInt(b);
        return Value(b.get());
    }
    if (isNumber() && other.isNumber()) {
        double da = asNumber();
        double db = other.asNumber();
        double res = da + db;
        if (needsPromotion(da, db, res)) {
            auto b = std::make_shared<BigInt>(BigInt((int64_t)da) + BigInt((int64_t)db));
            registerBigInt(b);
            return Value(b.get());
        }
        return Value(res);
    }
    throw std::runtime_error("Invalid types for operation");
}

Value Value::operator-(const Value& other) const {
    if (isError()) return *this;
    if (other.isError()) return other;
    if (isBigInt() || other.isBigInt()) {
        auto b = std::make_shared<BigInt>(getBigInt(*this) - getBigInt(other));
        registerBigInt(b);
        return Value(b.get());
    }
    if (isNumber() && other.isNumber()) {
        double da = asNumber();
        double db = other.asNumber();
        double res = da - db;
        if (needsPromotion(da, db, res)) {
            auto b = std::make_shared<BigInt>(BigInt((int64_t)da) - BigInt((int64_t)db));
            registerBigInt(b);
            return Value(b.get());
        }
        return Value(res);
    }
    throw std::runtime_error("Invalid types for operation");
}

Value Value::operator*(const Value& other) const {
    if (isError()) return *this;
    if (other.isError()) return other;
    if (isBigInt() || other.isBigInt()) {
        auto b = std::make_shared<BigInt>(getBigInt(*this) * getBigInt(other));
        registerBigInt(b);
        return Value(b.get());
    }
    if (isNumber() && other.isNumber()) {
        double da = asNumber();
        double db = other.asNumber();
        double res = da * db;
        if (needsPromotion(da, db, res)) {
            auto b = std::make_shared<BigInt>(BigInt((int64_t)da) * BigInt((int64_t)db));
            registerBigInt(b);
            return Value(b.get());
        }
        return Value(res);
    }
    throw std::runtime_error("Invalid types for operation");
}

Value Value::operator/(const Value& other) const {
    if (isError()) return *this;
    if (other.isError()) return other;
    if (isBigInt() || other.isBigInt()) {
        BigInt db = getBigInt(other);
        if (db == 0) throw std::runtime_error("Division by zero");
        auto b = std::make_shared<BigInt>(getBigInt(*this) / db);
        registerBigInt(b);
        return Value(b.get());
    }
    if (isNumber() && other.isNumber()) {
        double db = other.asNumber();
        if (db == 0) throw std::runtime_error("Division by zero");
        double da = asNumber();
        double res = da / db;
        if (needsPromotion(da, db, res)) {
            auto b = std::make_shared<BigInt>(BigInt((int64_t)da) / BigInt((int64_t)db));
            registerBigInt(b);
            return Value(b.get());
        }
        return Value(res);
    }
    throw std::runtime_error("Invalid types for operation");
}

Value Value::operator%(const Value& other) const {
    if (isError()) return *this;
    if (other.isError()) return other;
    if (isBigInt() || other.isBigInt()) {
        BigInt db = getBigInt(other);
        if (db == 0) throw std::runtime_error("Modulo by zero");
        auto b = std::make_shared<BigInt>(getBigInt(*this) % db);
        registerBigInt(b);
        return Value(b.get());
    }
    if (isNumber() && other.isNumber()) {
        double db = other.asNumber();
        if (db == 0) throw std::runtime_error("Modulo by zero");
        double da = asNumber();
        double res = std::fmod(da, db);
        if (needsPromotion(da, db, res)) {
            auto b = std::make_shared<BigInt>(BigInt((int64_t)da) % BigInt((int64_t)db));
            registerBigInt(b);
            return Value(b.get());
        }
        return Value(res);
    }
    throw std::runtime_error("Invalid types for operation");
}

Value Value::operator-() const {
    if (isBigInt()) {
        auto b = std::make_shared<BigInt>(-*asBigInt());
        registerBigInt(b);
        return Value(b.get());
    }
    return Value(-asNumber());
}

Value Value::operator==(const Value& other) const {
    if (isFuture()) return asFuture()->get() == other;
    if (other.isFuture()) return *this == other.asFuture()->get();
    
    if (bits == other.bits) return Value(true);
    if (isNil() || isUndefined() || other.isNil() || other.isUndefined()) return Value(false);
    if (isNumber() && other.isNumber()) return Value(asNumber() == other.asNumber());
    if (isBigInt() && other.isBigInt()) return Value(*asBigInt() == *other.asBigInt());
    if ((isBigInt() && other.isNumber()) || (isNumber() && other.isBigInt())) {
        return Value(getBigInt(*this) == getBigInt(other));
    }
    if (isString() && other.isString()) return Value(*asString() == *other.asString());
    if (isRegex() && other.isString()) {
        auto* r = asRegex();
        if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(false); } }
        return Value(std::regex_search(*other.asString(), *r->re));
    }
    if (isString() && other.isRegex()) {
        auto* r = other.asRegex();
        if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(false); } }
        return Value(std::regex_search(*asString(), *r->re));
    }
    return Value(false);
}

Value Value::operator!=(const Value& other) const { return !(*this == other); }
Value Value::operator<(const Value& other) const {
    if ((isBigInt() || isNumber()) && (other.isBigInt() || other.isNumber())) {
        if (isBigInt() || other.isBigInt()) return Value(getBigInt(*this) < getBigInt(other));
        return Value(asNumber() < other.asNumber());
    }
    return Value(false);
}
Value Value::operator<=(const Value& other) const {
    if ((isBigInt() || isNumber()) && (other.isBigInt() || other.isNumber())) {
        if (isBigInt() || other.isBigInt()) return Value(getBigInt(*this) <= getBigInt(other));
        return Value(asNumber() <= other.asNumber());
    }
    return Value(false);
}
Value Value::operator>(const Value& other) const {
    if ((isBigInt() || isNumber()) && (other.isBigInt() || other.isNumber())) {
        if (isBigInt() || other.isBigInt()) return Value(getBigInt(*this) > getBigInt(other));
        return Value(asNumber() > other.asNumber());
    }
    return Value(false);
}
Value Value::operator>=(const Value& other) const {
    if ((isBigInt() || isNumber()) && (other.isBigInt() || other.isNumber())) {
        if (isBigInt() || other.isBigInt()) return Value(getBigInt(*this) >= getBigInt(other));
        return Value(asNumber() >= other.asNumber());
    }
    return Value(false);
}

Value Value::operator!() const {
    bool isTrue;
    if (isBool()) {
        isTrue = bits & 1;
    } else {
        isTrue = !(isNil() || isUndefined());
    }
    return Value(!isTrue);
}

Value SpClass::call(Interpreter& interpreter, const std::vector<Value>& args) {
    (void)args;
    if (isAbstract) throw std::runtime_error("Cannot instantiate abstract class '" + name + "'");
    auto instance = std::make_shared<SpInstance>(this);
    Value::registerInstance(instance);
    
    // Temporarily define 'this' in the environment so initializers (like lambdas) can capture it
    auto prevEnv = interpreter.environment;
    auto newEnv = std::make_shared<Environment>(0, prevEnv);
    newEnv->define("this", Value(instance.get()));
    interpreter.environment = newEnv;
    
    try {
        for (size_t i = 0; i < properties.size(); ++i) {
            if (properties[i].initializer_value.bits != Value().bits && !properties[i].initializer_value.isUndefined()) {
                 instance->fields[i] = properties[i].initializer_value;
            } else if (properties[i].initializer) {
                instance->fields[i] = properties[i].initializer->evaluate(interpreter);
            } else {
                instance->fields[i] = Value(Type::NULL_VAL);
            }
        }
    } catch (...) {
        interpreter.environment = prevEnv;
        throw;
    }
    
    interpreter.environment = prevEnv;
    return Value(instance.get());
}

Value BoundMethod::call(Interpreter& interpreter, const std::vector<Value>& args) {
    std::vector<Value> newArgs;
    newArgs.reserve(args.size() + 1);
    newArgs.push_back(instance);
    newArgs.insert(newArgs.end(), args.begin(), args.end());
    return method->call(interpreter, newArgs);
}
Value parseJSONValue(const std::string& json, size_t& pos, Interpreter& interpreter) {
    auto skipWS = [&]() {
        while (pos < json.size() && isspace(json[pos])) pos++;
    };
    skipWS();
    if (pos >= json.size()) throw std::runtime_error("Unexpected end of JSON");

    char c = json[pos];
    if (c == '"') {
        pos++;
        std::string s = "";
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                char esc = json[pos];
                if (esc == '"') s += '"';
                else if (esc == '\\') s += '\\';
                else if (esc == 'n') s += '\n';
                else if (esc == 'r') s += '\r';
                else if (esc == 't') s += '\t';
                else s += esc;
            } else {
                s += json[pos];
            }
            pos++;
        }
        if (pos < json.size()) pos++;
        return Value(interpreter.makeString(s));
    } else if (c == '{') {
        pos++;
        auto* obj = interpreter.makeObject();
        skipWS();
        if (pos < json.size() && json[pos] != '}') {
            while (true) {
                skipWS();
                if (json[pos] != '"') throw std::runtime_error("Expected \" for JSON key");
                pos++;
                std::string key = "";
                while (pos < json.size() && json[pos] != '"') {
                    key += json[pos++];
                }
                if (pos < json.size()) pos++;
                skipWS();
                if (pos >= json.size() || json[pos] != ':') throw std::runtime_error("Expected : in JSON object");
                pos++;
                obj->push_back({key, parseJSONValue(json, pos, interpreter)});
                skipWS();
                if (pos < json.size() && json[pos] == ',') {
                    pos++;
                    continue;
                }
                break;
            }
        }
        skipWS();
        if (pos >= json.size() || json[pos] != '}') throw std::runtime_error("Expected } in JSON object");
        pos++;
        return Value(obj);
    } else if (c == '[') {
        pos++;
        auto* arr = interpreter.makeArray();
        skipWS();
        if (pos < json.size() && json[pos] != ']') {
            while (true) {
                arr->push_back(parseJSONValue(json, pos, interpreter));
                skipWS();
                if (pos < json.size() && json[pos] == ',') {
                    pos++;
                    continue;
                }
                break;
            }
        }
        skipWS();
        if (pos >= json.size() || json[pos] != ']') throw std::runtime_error("Expected ] in JSON array");
        pos++;
        return Value(arr);
    } else if (isdigit(c) || c == '-') {
        size_t start = pos;
        if (c == '-') pos++;
        while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '.')) pos++;
        return Value(std::stod(json.substr(start, pos - start)));
    } else if (json.compare(pos, 4, "true") == 0) {
        pos += 4;
        return Value(true);
    } else if (json.compare(pos, 5, "false") == 0) {
        pos += 5;
        return Value(false);
    } else if (json.compare(pos, 4, "null") == 0) {
        pos += 4;
        return Value(Type::NULL_VAL);
    }
    throw std::runtime_error("Invalid JSON character: " + std::string(1, c));
}

std::string stringifyJSON(const Value& val, int indent) {
    if (val.isNumber()) {
        std::string s = std::to_string(val.asNumber());
        if (s.find('.') != std::string::npos) {
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (s.back() == '.') s.pop_back();
        }
        return s;
    }
    if (val.isString()) {
        std::string s = *val.asString();
        std::string res = "\"";
        for (char c : s) {
            if (c == '"') res += "\\\"";
            else if (c == '\\') res += "\\\\";
            else if (c == '\n') res += "\\n";
            else if (c == '\r') res += "\\r";
            else if (c == '\t') res += "\\t";
            else res += c;
        }
        res += "\"";
        return res;
    }
    if (val.isBoolean()) return val.asBoolean() ? "true" : "false";
    if (val.isNil()) return "null";
    if (val.isObject()) {
        auto* obj = val.asObject();
        std::string res = "{\n";
        std::string outerPad(indent, ' ');
        std::string innerPad(indent + 2, ' ');
        for (size_t i = 0; i < obj->size(); ++i) {
            res += innerPad + "\"" + (*obj)[i].first + "\": " + stringifyJSON((*obj)[i].second, indent + 2);
            if (i < obj->size() - 1) res += ",";
            res += "\n";
        }
        res += outerPad + "}";
        return res;
    }
    if (val.isArray()) {
        auto* arr = val.asArray();
        std::string res = "[\n";
        std::string outerPad(indent, ' ');
        std::string innerPad(indent + 2, ' ');
        for (size_t i = 0; i < arr->size(); ++i) {
            res += innerPad + stringifyJSON((*arr)[i], indent + 2);
            if (i < arr->size() - 1) res += ",";
            res += "\n";
        }
        res += outerPad + "]";
        return res;
    }
    return "null";
}

std::ostream& operator<<(std::ostream& os, const Value& val) {
    if (val.isNumber()) {
        double num = val.asNumber();
        if (num == (long long)num) os << (long long)num;
        else os << num;
    } else if (val.isString()) {
        os << *val.asString();
    } else if (val.isBool()) {
        os << (val.asBoolean() ? "true" : "false");
    } else if (val.isNil()) {
        os << "null";
    } else {
        os << val.toString();
    }
    return os;
}
