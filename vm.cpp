#include "vm.h"
#include "interpreter.h"
#include <regex>
#include <cstring>
#include <thread>
#include <chrono>

static std::string escapeRegex(const std::string& s) {
    std::string res = "";
    for (char c : s) {
        if (std::strchr(".^$*+?()[]{}\\\\|", c)) res += "\\";
        res += c;
    }
    return res;
}
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>

namespace fs = std::filesystem;

#ifdef _WIN32
#include <windows.h>
#define popen _popen
#define pclose _pclose
#define RTLD_NOW 0
static void* dlopen(const char* filename, int) {
    return (void*)LoadLibraryA(filename);
}
static void* dlsym(void* handle, const char* symbol) {
    return (void*)GetProcAddress((HMODULE)handle, symbol);
}
[[maybe_unused]] static int dlclose(void* handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}
[[maybe_unused]] static const char* dlerror() {
    static char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buf, (sizeof(buf) / sizeof(char)), NULL);
    return buf;
}
#else
#include <dlfcn.h>
#include <libgen.h>
#endif
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <functional>

VM::VM() : VM(std::make_shared<VMSharedState>()) {}

VM::VM(std::shared_ptr<VMSharedState> sharedState) : sharedState(sharedState) {
    stack = std::make_unique<Value[]>(STACK_MAX);
    frames = std::make_unique<CallFrame[]>(FRAMES_MAX);
    sp = stack.get();
    fp = sp;
    frame = frames.get() - 1;
}

VM::~VM() {
}

void VM::runtimeError(const std::string& message, int line) {
    std::cerr << Color::Red << "Runtime Error: " << message << Color::Reset;
    if (line >= 0) {
        std::cerr << " at line " << line;
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

Value VM::pushError(const std::string& message, int line) {
    auto e = std::make_shared<ErrorData>(message, line);
    Value::registerError(e);
    return Value(e.get());
}

void VM::defineGlobal(const std::string& name, Value val) {
    std::lock_guard<std::recursive_mutex> lock(sharedState->globalMutex);
    sharedState->globalIndices[name] = (int)sharedState->globals.size();
    sharedState->globalMutability[name] = true;
    sharedState->globals.push_back(val);
}

bool checkTypeInternal(const Value& val, const std::string& typeStr);


static Value sp_get_property(Interpreter& interpreter, Value objVal, const std::string& property) {
    if (property == "wait" && objVal.isFuture()) {
        auto nativeFunc = std::make_shared<NativeFunction>([objVal](Interpreter&, const std::vector<Value>&) {
             return objVal.asFuture()->get();
        });
        Value::registerFunction(nativeFunc);
        return Value(nativeFunc.get(), true);
    }

    if (property == "stop" && objVal.isTimer()) {
        auto timer = objVal.asTimer();
        auto nativeFunc = std::make_shared<NativeFunction>([timer](Interpreter&, const std::vector<Value>&) {
            timer->active = false;
            return Value();
        });
        Value::registerFunction(nativeFunc);
        return Value(nativeFunc.get(), true);
    }

    if (property == "error") {
        auto nativeFunc = std::make_shared<NativeFunction>([objVal](Interpreter& interp, const std::vector<Value>& args) {
            Value val = objVal;
            if (val.isFuture()) val = val.asFuture()->get();
            if (val.isError()) {
                if (!args.empty() && args[0].isFunction()) {
                    std::vector<Value> cbArgs = { Value(interp.makeString(val.asError()->message)) };
                    if (interp.callHandler) return interp.callHandler(args[0].asFunction(), cbArgs);
                    return args[0].asFunction()->call(interp, cbArgs);
                }
            }
            return val; 
        });
        Value::registerFunction(nativeFunc);
        return Value(nativeFunc.get(), true);
    }

    if (property == "message" && objVal.isError()) {
        return interpreter.makeString(objVal.asError()->message);
    }
    if (property == "line" && objVal.isError()) {
        return Value((double)objVal.asError()->line);
    }

    // Implicit future unwrapping: if objVal is a Future and we're accessing ANY property
    // other than .wait / .error, automatically resolve the future first
    if (objVal.isFuture() && property != "wait" && property != "error") {
        objVal = objVal.asFuture()->get();
    }

    Value builtin = objVal.getBuiltinMethod(property, interpreter);
    if (!builtin.isUndefined()) return builtin;
    if (objVal.isRegex()) {
        auto* r = objVal.asRegex();
        auto addChainer = [&](const std::string& name, std::function<std::pair<std::string, std::string>(const std::vector<Value>&)> gen) {
            if (property == name) {
                auto nativeFunc = std::make_shared<NativeFunction>([r, gen](Interpreter& interp, const std::vector<Value>& args) {
                    auto res = gen(args);
                    return interp.makeRegex(r->pattern + res.first, res.second, r->isGlobal);
                });
                Value::registerFunction(nativeFunc);
                return Value(nativeFunc.get(), true);
            }
            return Value();
        };

        Value v;
        if (!(v = addChainer("digit", [](const auto&){ return std::make_pair("\\d", "\\d"); })).isUndefined()) return v;
        if (!(v = addChainer("nonDigit", [](const auto&){ return std::make_pair("\\D", "\\D"); })).isUndefined()) return v;
        if (!(v = addChainer("word", [](const auto&){ return std::make_pair("\\w", "\\w"); })).isUndefined()) return v;
        if (!(v = addChainer("letter", [](const auto&){ return std::make_pair("[a-zA-Z]", "[a-zA-Z]"); })).isUndefined()) return v;
        if (!(v = addChainer("whitespace", [](const auto&){ return std::make_pair("\\s", "\\s"); })).isUndefined()) return v;
        if (!(v = addChainer("any", [](const auto&){ return std::make_pair(".", "."); })).isUndefined()) return v;
        if (!(v = addChainer("start", [](const auto&){ return std::make_pair("^", "^"); })).isUndefined()) return v;
        if (!(v = addChainer("end", [](const auto&){ return std::make_pair("$", "$"); })).isUndefined()) return v;
        if (!(v = addChainer("wordBoundary", [](const auto&){ return std::make_pair("\\b", "\\b"); })).isUndefined()) return v;
        if (!(v = addChainer("text", [](const auto& args){ std::string t = args.empty() ? "" : escapeRegex(args[0].toPureString()); return std::make_pair(t, t); })).isUndefined()) return v;
        if (!(v = addChainer("range", [](const auto& args){ 
            if (args.size() < 2) return std::make_pair(std::string(""), std::string(""));
            std::string t = "[" + args[0].toPureString() + "-" + args[1].toPureString() + "]";
            return std::make_pair(t, t);
        })).isUndefined()) return v;

        if (property == "repeat") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return Value(r);
                std::string rep;
                if (args.size() >= 2) rep = "{" + args[0].toPureString() + "," + args[1].toPureString() + "}";
                else rep = "{" + args[0].toPureString() + "}";
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")" + rep;
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")" + rep);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "maybe" || property == "optional") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")?";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")?");
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "oneOrMore") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")+";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")+", r->isGlobal);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "zeroOrMore") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")*";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")*", r->isGlobal);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "optional") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>&) {
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")?";
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")?");
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "repeatAtLeast") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return Value(r);
                std::string n = args[0].toPureString();
                std::string rep = "{" + n + ",}";
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")" + rep;
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")" + rep);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "repeatBetween") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.size() < 2) return Value(r);
                std::string min = args[0].toPureString();
                std::string max = args[1].toPureString();
                std::string rep = "{" + min + "," + max + "}";
                std::string newPattern = r->pattern.substr(0, r->pattern.length() - r->lastPart.length()) + "(?:" + r->lastPart + ")" + rep;
                return interp.makeRegex(newPattern, "(?:" + r->lastPart + ")" + rep);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "capture") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return interp.makeRegex(r->pattern + "()", "()");
                std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
                return interp.makeRegex(r->pattern + "(" + p + ")", "(" + p + ")", r->isGlobal);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "group") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty()) return interp.makeRegex(r->pattern + "(?:)", "(?:)");
                std::string p = args[0].isRegex() ? args[0].asRegex()->pattern : args[0].toPureString();
                return interp.makeRegex(r->pattern + "(?:" + p + ")", "(?:" + p + ")", r->isGlobal);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "global") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>&) {
                return interp.makeRegex(r->pattern, r->lastPart, true);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "or") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isRegex()) return Value(r);
                std::string res = r->pattern + "|" + args[0].asRegex()->pattern;
                return interp.makeRegex(res, res, r->isGlobal);
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "test") {
            auto nativeFunc = std::make_shared<NativeFunction>([r](Interpreter&, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isString()) return Value(false);
                if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(false); } }
                return Value(std::regex_search(*args[0].asString(), *r->re));
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
                if (args.empty()) return Value(false);
                if (args[0].isRegex()) {
                    auto* r = args[0].asRegex();
                    if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(false); } }
                    return Value(std::regex_search(*s, *r->re));
                }
                if (!args[0].isString()) return Value(false);
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
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "split") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                auto* arr = interp.makeArray();
                if (args.size() > 0 && args[0].isRegex()) {
                    auto* r = args[0].asRegex();
                    if (!r->re) { try { r->re = std::make_shared<std::regex>(r->pattern); } catch (...) { return Value(arr); } }
                    std::sregex_token_iterator iter(s->begin(), s->end(), *r->re, -1);
                    std::sregex_token_iterator end;
                    for (; iter != end; ++iter) arr->push_back(Value(interp.makeString(*iter)));
                    return Value(arr);
                }
                std::string sep = (args.size() > 0 && args[0].isString()) ? *args[0].asString() : "";
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
                
                size_t start_pos = 0;
                while((start_pos = res.find(from, start_pos)) != std::string::npos) {
                    res.replace(start_pos, from.length(), to);
                    start_pos += to.length();
                }
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "match") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
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
        if (property == "padStart") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                size_t targetLength = (size_t)args[0].asNumber();
                if (s->length() >= targetLength) return Value(interp.makeString(*s));
                std::string pad = (args.size() > 1 && args[1].isString()) ? *args[1].asString() : " ";
                std::string res = *s;
                while (res.length() < targetLength) {
                    if (res.length() + pad.length() <= targetLength) res = pad + res;
                    else res = pad.substr(0, targetLength - res.length()) + res;
                }
                return Value(interp.makeString(res));
            });
            Value::registerFunction(nativeFunc);
            return Value(nativeFunc.get(), true);
        }
        if (property == "repeat") {
            auto nativeFunc = std::make_shared<NativeFunction>([s](Interpreter& interp, const std::vector<Value>& args) {
                if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));
                int count = (int)args[0].asNumber();
                if (count <= 0) return Value(interp.makeString(""));
                std::string res = "";
                for (int i = 0; i < count; i++) res += *s;
                return Value(interp.makeString(res));
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
    
    frame = frames.get();
    frame->function = mainFunction;
    frame->ip = mainFunction->chunk.code.data();
    frame->stackBase = 0;
    
    sp = stack.get();
    fp = sp;
    
    interpreter.callHandler = [this, &interpreter](ICallable* c, const std::vector<Value>& args) {
        return this->call(interpreter, c, args);
    };
    
    for (int i = 0; i < mainFunction->localCount; ++i) *sp++ = Value();

    runLoop(0, interpreter);
}

void VM::runLoop(int stopFrameIndex, Interpreter& interpreter) {
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
        try { \
            *sp++ = (a op b); \
        } catch (const std::exception& e) { \
            runtimeError(e.what(), getLine(frame, ip)); \
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

        case OpCode::GET_GLOBAL:
            {
                Value nameVal = READ_CONSTANT();
                const std::string& name = *nameVal.asString();
                std::lock_guard<std::recursive_mutex> lock(sharedState->globalMutex);
                auto it = sharedState->globalIndices.find(name);
                if (it == sharedState->globalIndices.end()) {
                    runtimeError("Property '" + name + "' not found", getLine(frame, ip));
                    return;
                }
                *sp++ = sharedState->globals[it->second];
            }
            DISPATCH();

        case OpCode::DEFINE_GLOBAL:
            {
                uint8_t constIdx = READ_BYTE();
                bool isMutable = READ_BYTE() != 0;
                Value nameVal = constantsPtr[constIdx];
                if (!nameVal.isString()) throw std::runtime_error("Global name must be a string.");
                const std::string& name = *nameVal.asString();
                
                Value val = *(--sp);
                std::lock_guard<std::recursive_mutex> lock(sharedState->globalMutex);
                sharedState->globalIndices[name] = (int)sharedState->globals.size();
                sharedState->globalMutability[name] = isMutable;
                sharedState->globals.push_back(val);
            }
            DISPATCH();

        case OpCode::SET_GLOBAL:
            {
                Value nameVal = READ_CONSTANT();
                Value val = sp[-1];
                const std::string& name = *nameVal.asString();
                std::lock_guard<std::recursive_mutex> lock(sharedState->globalMutex);
                auto it = sharedState->globalIndices.find(name);
                if (it == sharedState->globalIndices.end()) {
                    runtimeError("Property '" + name + "' not found", getLine(frame, ip));
                    return;
                }
                if (!sharedState->globalMutability[name]) {
                    runtimeError("Cannot reassign immutable global '" + name + "'.", getLine(frame, ip));
                    return;
                }
                sharedState->globals[it->second] = val;
            }
            DISPATCH();

        case OpCode::SET_GLOBAL_FAST:
            {
                uint8_t index = READ_BYTE();
                std::lock_guard<std::recursive_mutex> lock(sharedState->globalMutex);
                sharedState->globals[index] = sp[-1];
            }
            DISPATCH();

        case OpCode::GET_GLOBAL_FAST:
            {
                uint8_t index = READ_BYTE();
                std::lock_guard<std::recursive_mutex> lock(sharedState->globalMutex);
                *sp++ = sharedState->globals[index];
            }
            DISPATCH();

        case OpCode::GET_PROPERTY:
            {
                uint8_t constIdx = READ_BYTE();
                Value nameVal = constantsPtr[constIdx];
                const std::string& property = *nameVal.asString();
                
                Value objVal = *(--sp);
                // Implicit future unwrapping: only if NOT accessing future-specific methods
                if (objVal.isFuture() && property != "wait" && property != "error" && property != "then" && property != "catch") {
                    objVal = objVal.asFuture()->get();
                }

                Value res = sp_get_property(interpreter, objVal, property);
                if (res.isUndefined()) {
                    runtimeError("Property '" + property + "' not found", getLine(frame, ip));
                    return;
                }
                *sp++ = res;
            }
            DISPATCH();

        case OpCode::SET_PROPERTY:
            {
                Value val = *(--sp);
                Value objVal = *(--sp);
                
                uint8_t constIdx = READ_BYTE();
                Value nameVal = constantsPtr[constIdx];
                const std::string& property = *nameVal.asString();

                // Implicit future unwrapping
                if (objVal.isFuture() && property != "wait" && property != "error") {
                    objVal = objVal.asFuture()->get();
                }
                
                if (objVal.isInstance()) {
                    const std::string& property = *nameVal.asString();
                    auto* instance = objVal.asInstance();
                    for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
                        if (instance->klass->properties[i].name == property) {
                            if (instance->klass->properties[i].isReadonly) {
                                runtimeError("Property '" + property + "' is readonly.", getLine(frame, ip));
                                return;
                            }
                            instance->fields[i] = val;
                            *sp++ = objVal;
                            DISPATCH();
                        }
                    }
                    runtimeError("Property '" + property + "' not found on instance.", getLine(frame, ip));
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
                *sp++ = objVal;
            }
            DISPATCH();

        case OpCode::MAKE_OBJECT:
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

        case OpCode::GET_ELEMENT:
            {
                Value indexVal = *(--sp);
                Value objVal = *(--sp);
                if (objVal.isFuture()) objVal = objVal.asFuture()->get();
                
                if (objVal.isArray()) {
                    if (!indexVal.isNumber()) {
                        *sp++ = Value(Type::UNDEFINED);
                    } else {
                        double d = indexVal.asNumber();
                        if (d < 0 || std::floor(d) != d) {
                            *sp++ = Value(Type::UNDEFINED);
                        } else {
                            size_t idx = static_cast<size_t>(d);
                            const auto& arr = *objVal.asArray();
                            if (idx >= arr.size()) {
                                *sp++ = Value(Type::UNDEFINED);
                            } else {
                                *sp++ = arr[idx];
                            }
                        }
                    }
                } else if (objVal.isMap()) {
                    auto* m = objVal.asMap();
                    auto it = m->map.find(indexVal);
                    if (it == m->map.end()) *sp++ = Value(Type::UNDEFINED);
                    else *sp++ = it->second;
                } else if (objVal.isObject() && !objVal.isInstance()) {
                    if (!indexVal.isString()) *sp++ = Value(Type::UNDEFINED);
                    else {
                        std::string key = *indexVal.asString();
                        const auto& obj = *objVal.asObject();
                        bool found = false;
                        for (const auto& pair : obj) {
                            if (pair.first == key) {
                                *sp++ = pair.second;
                                found = true;
                                break;
                            }
                        }
                        if (!found) *sp++ = Value(Type::UNDEFINED);
                    }
                } else if (objVal.isInstance()) {
                    if (!indexVal.isString()) *sp++ = Value(Type::UNDEFINED);
                    else {
                        std::string key = *indexVal.asString();
                        auto* instance = objVal.asInstance();
                        bool found = false;
                        for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
                            if (instance->klass->properties[i].name == key) {
                                *sp++ = instance->fields[i];
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            auto it = instance->klass->methods.find(key);
                            if (it != instance->klass->methods.end()) {
                                Value methodVal = it->second;
                                if (methodVal.isFunction()) {
                                    *sp++ = Value(interpreter.makeFunction(std::make_shared<BoundMethod>(methodVal.asFunction(), objVal)));
                                } else {
                                    *sp++ = methodVal;
                                }
                            } else {
                                *sp++ = Value(Type::UNDEFINED);
                            }
                        }
                    }
                } else {
                    // Success/Error Destructuring Pattern for non-collections
                    if (indexVal.isNumber() && indexVal.asNumber() == 0) {
                        // [0] -> Result
                        if (objVal.isError()) {
                            *sp++ = Value(Type::NULL_VAL); // Error object yields null for result
                        } else {
                            *sp++ = objVal; // Success value yields itself
                        }
                    } else if (indexVal.isNumber() && indexVal.asNumber() == 1) {
                        // [1] -> Error
                        if (objVal.isError()) {
                            *sp++ = objVal; // Error object yields itself
                        } else {
                            *sp++ = Value(Type::NULL_VAL); // Success value yields null error
                        }
                    } else {
                        *sp++ = Value(Type::UNDEFINED);
                    }
                }
            }
            DISPATCH();

        case OpCode::SET_ELEMENT:
            {
                Value val = *(--sp);
                Value indexVal = *(--sp);
                Value objVal = *(--sp);
                if (objVal.isFuture()) objVal = objVal.asFuture()->get();
                
                if (objVal.isArray()) {
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
                    auto* arr = objVal.asArray();
                    while (idx >= arr->size()) {
                        arr->push_back(Value(Type::UNDEFINED));
                    }
                    (*arr)[idx] = val;
                } else if (objVal.isMap()) {
                    auto* m = objVal.asMap();
                    m->map[indexVal] = val;
                } else if (objVal.isObject() && !objVal.isInstance()) {
                    if (!indexVal.isString()) {
                        runtimeError("Object properties must be indexed by string", getLine(frame, ip));
                        return;
                    }
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
                    if (!indexVal.isString()) {
                        runtimeError("Instance properties must be indexed by string", getLine(frame, ip));
                        return;
                    }
                    std::string key = *indexVal.asString();
                    auto* instance = objVal.asInstance();
                    bool found = false;
                    for (size_t i = 0; i < instance->klass->properties.size(); ++i) {
                        if (instance->klass->properties[i].name == key) {
                            if (instance->klass->properties[i].isReadonly) {
                                runtimeError("Cannot assign to readonly property '" + key + "'", getLine(frame, ip));
                                return;
                            }
                            instance->fields[i] = val;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        runtimeError("Cannot add new properties to instance dynamically", getLine(frame, ip));
                        return;
                    }
                } else {
                    runtimeError("Invalid assignment target for index assignment", getLine(frame, ip));
                    return;
                }
                *sp++ = val;
            }
            DISPATCH();

        case OpCode::ASYNC:
            {
                Value funcVal = *(--sp);
                if (!funcVal.isVMFunction() && !funcVal.isFunction()) {
                    *sp++ = pushError("async requires a callable", getLine(frame, ip));
                    DISPATCH();
                }

                auto sharedStatePtr = this->sharedState;
                auto sharedFuncVal = std::make_shared<Value>(funcVal);
                auto fut = std::make_shared<std::future<Value>>(
                    std::async(std::launch::async, [sharedFuncVal, sharedStatePtr]() -> Value {
                        try {
                            VM threadVm(sharedStatePtr);
                            Interpreter threadInterp(&threadVm);
                            Value::CurrentContext = &threadInterp;
                            
                            threadInterp.callHandler = [&threadVm, &threadInterp](ICallable* c, const std::vector<Value>& args) {
                                return threadVm.call(threadInterp, c, args);
                            };

                            if (sharedFuncVal->isVMFunction()) {
                                VMFunction* vmfunc = static_cast<VMFunction*>(sharedFuncVal->asFunction());
                                return threadVm.call(threadInterp, vmfunc, {});
                            } else if (sharedFuncVal->isFunction()) {
                                return sharedFuncVal->asFunction()->call(threadInterp, {});
                            }
                            return Value();
                        } catch (const std::exception& e) {
                            auto err = std::make_shared<ErrorData>(e.what(), -1);
                            Value::registerError(err);
                            return Value(err.get());
                        }
                    })
                );
                auto futData = std::make_shared<FutureData>(std::move(fut));
                Value::registerFuture(futData);
                *sp++ = Value(futData.get());
            }
            DISPATCH();

        case OpCode::AFTER:
            {
                Value funcVal = *(--sp);
                Value delayVal = *(--sp);
                double delayMs = delayVal.asNumber();

                auto sharedStatePtr = this->sharedState;
                auto sharedFuncVal = std::make_shared<Value>(funcVal);
                
                auto timer = std::make_shared<TimerData>();
                timer->thread = std::thread([delayMs, sharedFuncVal, sharedStatePtr, timer]() {
                    if (delayMs > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds((long long)delayMs));
                    }
                    if (timer->active) {
                        try {
                            VM threadVm(sharedStatePtr);
                            Interpreter threadInterp(&threadVm);
                            Value::CurrentContext = &threadInterp;
                            
                            threadInterp.callHandler = [&threadVm, &threadInterp](ICallable* c, const std::vector<Value>& args) {
                                return threadVm.call(threadInterp, c, args);
                            };

                            if (sharedFuncVal->isVMFunction()) {
                                VMFunction* vmfunc = static_cast<VMFunction*>(sharedFuncVal->asFunction());
                                threadVm.call(threadInterp, vmfunc, {});
                            } else if (sharedFuncVal->isFunction()) {
                                sharedFuncVal->asFunction()->call(threadInterp, {});
                            }
                        } catch (...) {}
                    }
                });

                Value::registerTimer(timer);
                *sp++ = Value(timer.get());
            }
            DISPATCH();

        case OpCode::EVERY:
            {
                Value funcVal = *(--sp);
                Value delayVal = *(--sp);
                double delayMs = delayVal.asNumber();

                auto sharedStatePtr = this->sharedState;
                auto sharedFuncVal = std::make_shared<Value>(funcVal);
                
                auto timer = std::make_shared<TimerData>();
                timer->thread = std::thread([delayMs, sharedFuncVal, sharedStatePtr, timer]() {
                    while (timer->active) {
                        if (delayMs > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds((long long)delayMs));
                        }
                        if (!timer->active) break;
                        
                        try {
                            VM threadVm(sharedStatePtr);
                            Interpreter threadInterp(&threadVm);
                            Value::CurrentContext = &threadInterp;
                            
                            threadInterp.callHandler = [&threadVm, &threadInterp](ICallable* c, const std::vector<Value>& args) {
                                return threadVm.call(threadInterp, c, args);
                            };

                            if (sharedFuncVal->isVMFunction()) {
                                VMFunction* vmfunc = static_cast<VMFunction*>(sharedFuncVal->asFunction());
                                threadVm.call(threadInterp, vmfunc, {});
                            } else if (sharedFuncVal->isFunction()) {
                                sharedFuncVal->asFunction()->call(threadInterp, {});
                            }
                        } catch (...) {}
                    }
                });

                Value::registerTimer(timer);
                *sp++ = Value(timer.get());
            }
            DISPATCH();

        case OpCode::CHECK_TYPE:
            {
                uint8_t typeIdx = READ_BYTE();
                Value typeVal = constantsPtr[typeIdx];
                std::string typeStr = *typeVal.asString();
                Value val = sp[-1];
                if (!checkTypeInternal(val, typeStr)) {
                    throw std::runtime_error("Type mismatch: expected " + typeStr + ", but got " + val.toString());
                }
            }
            DISPATCH();

        case OpCode::TYPEOF:
            {
                Value val = *(--sp);
                std::string typeStr;
                if (val.isNumber()) typeStr = "number";
                else if (val.isBigInt()) typeStr = "number";
                else if (val.isString()) typeStr = "string";
                else if (val.isBoolean()) typeStr = "boolean";
                else if (val.isArray()) typeStr = "array";
                else if (val.isFunction()) typeStr = "function";
                else if (val.isNil()) typeStr = "null";
                else if (val.isUndefined()) typeStr = "undefined";
                else if (val.isError()) typeStr = "error";
                else if (val.isRegex()) typeStr = "regex";
                else if (val.isFuture()) typeStr = "future";
                else if (val.isMap()) typeStr = "map";
                else if (val.isTimer()) typeStr = "timer";
                else if (val.isObject()) typeStr = "object";
                else typeStr = "unknown";
                *sp++ = Value(interpreter.makeString(typeStr));
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
            BINARY_OP(+);
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
            BINARY_OP(/);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_modulo:
#else
        case OpCode::MODULO:
#endif
            BINARY_OP(%);
            DISPATCH();

#if 0 && defined(__GNUC__)
        op_equal:
#else
        case OpCode::EQUAL:
#endif
            {
                Value b = *(--sp);
                Value a = *(--sp);
                *sp++ = (a == b);
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
                    *sp++ = pushError("Cannot open file: " + *pathVal.asString(), getLine(frame, ip));
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

                info->emplace_back("path", makeStrValue(absPathStr));
                info->emplace_back("dirname", makeStrValue(dirStr));
                info->emplace_back("name", makeStrValue(nameStr));
                info->emplace_back("ext", makeStrValue(extStr));
                info->emplace_back("size", Value(size));
                info->emplace_back("length", Value(size));
                info->emplace_back("exists", Value(exists));

                if (exists) {
                    auto mDate = std::make_shared<DateData>((double)st.st_mtime * 1000.0);
                    Value::registerDate(mDate);
                    info->emplace_back("modifiedAt", Value(mDate.get()));
                    
                    auto cDate = std::make_shared<DateData>((double)st.st_ctime * 1000.0);
                    Value::registerDate(cDate);
                    info->emplace_back("createdAt", Value(cDate.get()));
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
                    *sp++ = pushError("Cannot open file for reading: " + *pathVal.asString(), getLine(frame, ip));
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
                    if (callee.isObject() && !callee.isInstance()) {
                        auto* obj = callee.asObject();
                        for (auto& pair : *obj) {
                            if (pair.first == "__call__") {
                                if (pair.second.isFunction()) {
                                    callee = pair.second;
                                    sp[-(argCount + 1)] = callee;
                                    goto re_dispatch_call;
                                }
                            }
                        }
                    }
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
                            stack.get()[base + i] = sp[-argCount + i];
                        }
                        sp = stack.get() + base + argCount;
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        ip = frame->ip;
                        constantsPtr = vmFunc->chunk.constants.data();
                        for (int i = argCount; i < vmFunc->localCount; i++) *sp++ = Value();
                    } else {
                        frame->ip = ip;
                        frame++;
                        if (__builtin_expect(frame >= frames.get() + FRAMES_MAX, 0)) {
                            runtimeError("Stack overflow (CallFrame).", getLine(frame, ip));
                            return;
                        }
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        frame->stackBase = (int)(sp - stack.get()) - argCount;
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
                            stack.get()[base + i] = sp[-argCount + i];
                        }
                        sp = stack.get() + base + argCount;
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        ip = frame->ip;
                        constantsPtr = vmFunc->chunk.constants.data();
                        for (int i = argCount; i < vmFunc->localCount; i++) *sp++ = Value();
                    } else {
                        frame->ip = ip;
                        frame++;
                        if (__builtin_expect(frame >= frames.get() + FRAMES_MAX, 0)) throw std::runtime_error("Stack overflow (CallFrame).");
                        frame->function = vmFunc;
                        frame->ip = vmFunc->chunk.code.data();
                        frame->stackBase = (int)(sp - stack.get()) - argCount;
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
                auto* objA = a.isObject() ? a.asObject() : nullptr;
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
                Value result = (sp > stack.get()) ? *(--sp) : Value(Type::NULL_VAL);
                int base = frame->stackBase;
                
                if (__builtin_expect((int)(frame - frames.get()) == stopFrameIndex, 0)) {
                    *sp++ = result;
                    return;
                }
                frame--;
                
                ip = frame->ip;
                constantsPtr = frame->function->chunk.constants.data();
                fp = stack.get() + frame->stackBase;
                sp = stack.get() + base - 1;
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

        case OpCode::PROCESS_SLEEP:
            {
                Value delayVal = *(--sp);
                if (!delayVal.isNumber()) {
                    runtimeError("process.sleep requires a number delay.", getLine(frame, ip));
                    return;
                }
                double delayMs = delayVal.asNumber();
                if (delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds((long long)delayMs));
                }
                *sp++ = Value();
            }
            DISPATCH();
        } // switch
    } // while
} // VM::run

bool checkTypeInternal(const Value& val, const std::string& typeStr) {
    if (typeStr == "" || typeStr == "any") return true; 
    if (Value::Layouts.count(typeStr)) {
        return checkTypeInternal(val, Value::Layouts[typeStr]);
    }
    // Handle union types: split by " | " but only at the top level (respect < >)
    if (typeStr.find(" | ") != std::string::npos) {
        std::vector<std::string> parts;
        std::string current;
        int depth = 0;
        for (size_t i = 0; i < typeStr.size(); ++i) {
            if (typeStr[i] == '<' || typeStr[i] == '{') depth++;
            else if (typeStr[i] == '>' || typeStr[i] == '}') depth--;
            
            if (depth == 0 && i + 2 < typeStr.size() && typeStr.substr(i, 3) == " | ") {
                parts.push_back(current);
                current = "";
                i += 2; // Skip " | "
                continue;
            }
            current += typeStr[i];
        }
        parts.push_back(current);

        if (parts.size() > 1) {
            for (const auto& part : parts) {
                if (checkTypeInternal(val, part)) return true;
            }
            return false;
        }
    }

    // Handle intersection types: split by " & " but only at the top level
    if (typeStr.find(" & ") != std::string::npos) {
        std::vector<std::string> parts;
        std::string current;
        int depth = 0;
        for (size_t i = 0; i < typeStr.size(); ++i) {
            if (typeStr[i] == '<' || typeStr[i] == '{') depth++;
            else if (typeStr[i] == '>' || typeStr[i] == '}') depth--;
            
            if (depth == 0 && i + 2 < typeStr.size() && typeStr.substr(i, 3) == " & ") {
                parts.push_back(current);
                current = "";
                i += 2; // Skip " & "
                continue;
            }
            current += typeStr[i];
        }
        parts.push_back(current);

        if (parts.size() > 1) {
            for (const auto& part : parts) {
                if (!checkTypeInternal(val, part)) return false;
            }
            return true;
        }
    }

    if (typeStr == "number") return val.isNumber();
    if (typeStr == "string") return val.isString();
    if (typeStr == "boolean") return val.isBoolean();
    if (typeStr == "bigint") return val.isBigInt();
    
    if (typeStr.find("Array<") == 0) {
        if (!val.isArray()) return false;
        std::string innerType = typeStr.substr(6, typeStr.size() - 7);
        for (const auto& item : *val.asArray()) {
            if (!checkTypeInternal(item, innerType)) return false;
        }
        return true;
    }

    // Structural check for literal: { prop: type, ... }
    if (typeStr.size() > 2 && typeStr.front() == '{' && typeStr.back() == '}') {
        if (!val.isObject()) return false;
        auto* obj = val.asObject();
        std::string inner = typeStr.substr(1, typeStr.size() - 2);
        std::vector<std::pair<std::string, std::string>> props;
        size_t start = 0;
        int depth = 0;
        while (start < inner.size()) {
            size_t colon = std::string::npos;
            size_t comma = std::string::npos;
            for (size_t i = start; i < inner.size(); ++i) {
                if (inner[i] == '{') depth++;
                else if (inner[i] == '}') depth--;
                else if (depth == 0) {
                    if (inner[i] == ':' && colon == std::string::npos) colon = i;
                    if (inner[i] == ',' && comma == std::string::npos) {
                        comma = i;
                        break;
                    }
                }
            }
            if (colon != std::string::npos) {
                std::string key = inner.substr(start, colon - start);
                key.erase(0, key.find_first_not_of(" "));
                key.erase(key.find_last_not_of(" ") + 1);
                std::string type;
                if (comma != std::string::npos) {
                    type = inner.substr(colon + 1, comma - colon - 1);
                    start = comma + 1;
                } else {
                    type = inner.substr(colon + 1);
                    start = inner.size();
                }
                type.erase(0, type.find_first_not_of(" "));
                type.erase(type.find_last_not_of(" ") + 1);
                props.push_back({key, type});
            } else break;
        }

        for (const auto& p : props) {
            bool found = false;
            for (const auto& entry : *obj) {
                if (entry.first == p.first) {
                    if (!checkTypeInternal(entry.second, p.second)) return false;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }

    // Named Layout check
    if (Value::Layouts.count(typeStr)) {
        return checkTypeInternal(val, Value::Layouts[typeStr]);
    }
    
    if (val.isInstance()) {
        return val.asInstance()->klass->name == typeStr;
    }
    
    return false; // Type not recognized or mismatch
}

Value VM::call(Interpreter& interpreter, ICallable* callable, const std::vector<Value>& args) {

    if (!callable) return Value(Type::UNDEFINED);
    
    if (callable->isNative()) {
        return callable->call(interpreter, args);
    }
    
    if (!callable->isVMFunction()) {
        return callable->call(interpreter, args);
    }

    VMFunction* func = static_cast<VMFunction*>(callable);
    
    CallFrame* newFrame = frame + 1;
    if (newFrame >= frames.get() + FRAMES_MAX) {
        throw std::runtime_error("Stack overflow");
    }
    
    
    Value* savedSp = sp;
    // Push a dummy callee value so that OpCode::RETURN can safely overwrite it
    // without corrupting the caller's stack (e.g., when called from C++ native methods).
    *sp++ = Value();
    
    newFrame->function = func;
    newFrame->ip = func->chunk.code.data();
    newFrame->stackBase = (int)(sp - stack.get());
    
    // Save current frame state
    CallFrame* oldFrame = frame;
    
    // Push arguments and reserve locals
    for (int i = 0; i < (int)args.size() && i < func->arity; ++i) *sp++ = args[i];
    for (int i = (int)args.size(); i < func->localCount; ++i) *sp++ = Value();
    
    frame = newFrame;
    fp = stack.get() + frame->stackBase;
    
    runLoop((int)(frame - frames.get()), interpreter);
    
    Value result = *(--sp);
    sp = savedSp;
    frame = oldFrame;
    fp = (frame >= frames.get()) ? (stack.get() + frame->stackBase) : stack.get();
    
    return result;
}
