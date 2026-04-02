#include "types.h"
#include "interpreter.h"
#include "ast.h"
#include "vm.h"
#include <sstream>
#include <iomanip>

bool Value::useColor = true;
Interpreter* Value::CurrentContext = nullptr;

void Value::registerString(std::shared_ptr<std::string> s) {
    if (!CurrentContext) return;
    CurrentContext->allStrings.push_back(s);
}

void Value::registerArray(std::shared_ptr<std::vector<Value>> a) {
    if (!CurrentContext) return;
    CurrentContext->allArrays.push_back(a);
    if (CurrentContext->vm) CurrentContext->vm->allArrays.push_back(a);
}

void Value::registerObject(std::shared_ptr<std::vector<std::pair<std::string, Value>>> o) {
    if (!CurrentContext) return;
    CurrentContext->allObjects.push_back(o);
    if (CurrentContext->vm) CurrentContext->vm->allObjects.push_back(o);
}

void Value::registerBigInt(std::shared_ptr<int64_t> b) {
    if (!CurrentContext) return;
    CurrentContext->allBigInts.push_back(b);
    if (CurrentContext->vm) CurrentContext->vm->allBigInts.push_back(b);
}

void Value::registerDate(std::shared_ptr<DateData> d) {
    if (!CurrentContext) return;
    CurrentContext->allDates.push_back(d);
    if (CurrentContext->vm) CurrentContext->vm->allDates.push_back(d);
}

void Value::registerMap(std::shared_ptr<MapData> m) {
    if (!CurrentContext) return;
    CurrentContext->allMaps.push_back(m);
    if (CurrentContext->vm) CurrentContext->vm->allMaps.push_back(m);
}

void Value::registerError(std::shared_ptr<ErrorData> e) {
    if (!CurrentContext) return;
    if (CurrentContext->vm) CurrentContext->vm->allErrors.push_back(e);
}

void Value::registerInstance(std::shared_ptr<SpInstance> i) {
    if (!CurrentContext) return;
    if (CurrentContext->vm) CurrentContext->vm->allInstances.push_back(i);
}

void Value::registerClass(std::shared_ptr<SpClass> c) {
    if (!CurrentContext) return;
    CurrentContext->allClasses.push_back(c);
}

void Value::registerFunction(std::shared_ptr<ICallable> f) {
    if (!CurrentContext) return;
    CurrentContext->allFunctions.push_back(f);
    if (CurrentContext->vm) CurrentContext->vm->allFunctions.push_back(f);
}

size_t ValueHash::operator()(const Value& v) const {
    if (v.isError()) return (size_t)v.bits;
    if (v.isNumber()) return std::hash<double>{}(v.asNumber());
    if (v.isString()) return std::hash<std::string>{}(*v.asString());
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
    if (isBigInt()) return prefix + std::to_string(*asBigInt()) + "n" + suffix;
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
    if (isBigInt()) return std::to_string(*asBigInt());
    if (isDate()) return std::to_string((long long)asDate()->timestamp);
    if (isMap()) return "[Map]";
    if (isError()) return "[Error: " + asError()->message + "]";
    return "unknown";
}

Value Value::operator+(const Value& other) const {
    if (isError()) return *this;
    if (other.isError()) return other;
    if (isString() || other.isString()) {
        auto s = std::make_shared<std::string>(toPureString() + other.toPureString());
        registerString(s);
        return Value(s.get());
    }
    if (isBigInt() && other.isBigInt()) {
        auto b = std::make_shared<int64_t>(*asBigInt() + *other.asBigInt());
        registerBigInt(b);
        return Value(b.get());
    }
    if (isBigInt() || other.isBigInt()) {
        throw std::runtime_error("Cannot mix BigInt and other types in '+'");
    }
    return Value(asNumber() + other.asNumber());
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
