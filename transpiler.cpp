#include "transpiler.h"
#include <iostream>

static std::string escapeString(const std::string &s);

std::string
Transpiler::transpile(const std::vector<std::shared_ptr<Statement>> &ast) {
  mainCode << "int main(int argc, char* argv[]) {\n";
  mainCode << "    // Initialize context for AOT\n";
  mainCode << "    static VM aotVM;\n";
  mainCode << "    static Interpreter interp(&aotVM);\n";
  mainCode << "    for (int i = 1; i < argc; ++i) interp.cliArgs.push_back(std::string(argv[i]));\n";
  mainCode << "    Value::CurrentContext = &interp;\n";
  mainCode << "    Value sp_this = Value(Type::UNDEFINED);\n\n";

  mainCode << "    // Inject global objects\n";
  mainCode << "    {\n";
  mainCode << "        auto* dObj = new std::vector<std::pair<std::string, Value>>();\n";
  mainCode << "        dObj->push_back({\"now\", Value(new NativeFunction(sp_builtin_date_now), true)});\n";
  mainCode << "        sp_Date = Value(dObj);\n";
  mainCode << "    }\n";
  mainCode << "    {\n";
  mainCode << "        auto* cObj = new std::vector<std::pair<std::string, Value>>();\n";
  mainCode << "        cObj->emplace_back(\"args\", Value(new NativeFunction(sp_builtin_console_args), true));\n";
  mainCode << "        cObj->emplace_back(\"read\", Value(new NativeFunction(sp_builtin_console_read), true));\n";
  mainCode << "        sp_console = Value(cObj);\n";
  mainCode << "    }\n";
  mainCode << "    sp_Map = Value(new NativeFunction(sp_builtin_map_new), true);\n";
  mainCode << "    sp_HashMap = sp_Map;\n";
  mainCode << "    sp_time = Value(new NativeFunction(sp_builtin_time), true);\n";
  mainCode << "    sp_timeMicro = Value(new NativeFunction(sp_builtin_timeMicro), true);\n";
  mainCode << "    sp_timeNano = Value(new NativeFunction(sp_builtin_timeNano), true);\n";
  mainCode << "    sp_floor = Value(new NativeFunction(sp_builtin_floor), true);\n";
  mainCode << "    sp_range = Value(new NativeFunction(sp_builtin_range), true);\n";
  mainCode << "    sp_Error = Value(new NativeFunction(sp_builtin_error), true);\n\n";

  for (const auto &stmt : ast) {
    generateStatement(stmt.get(), mainCode);
  }

  mainCode << "    return 0;\n}\n";

  std::ostringstream finalCode;
  finalCode << "#include \"types.h\"\n";
  finalCode << "#include <iostream>\n";
  finalCode << "#include <vector>\n";
  finalCode << "#include <chrono>\n";
  finalCode << "#include <cmath>\n";
  finalCode << "#include <fstream>\n";
  finalCode << "#include <filesystem>\n";
  finalCode << "#include <sys/stat.h>\n";
  finalCode << "#include <unistd.h>\n";
  finalCode << "#include <cstdio>\n";
  finalCode << "#include <algorithm>\n";
  finalCode << "#include <iomanip>\n";
  finalCode << "#include <sstream>\n";
  finalCode << "#include <cctype>\n";
  finalCode << "#include <array>\n";
  finalCode << "#include \"interpreter.h\"\n";
  finalCode << "#include \"vm.h\"\n\n";

  finalCode << "namespace fs = std::filesystem;\n\n";

  finalCode << "struct ProcessResult {\n";
  finalCode << "    std::string output;\n";
  finalCode << "    int exitCode;\n";
  finalCode << "    bool failed;\n";
  finalCode << "};\n\n";

  finalCode << "Value makeProcessResult(const std::string& out, int code) {\n";
  finalCode << "    auto* obj = new std::vector<std::pair<std::string, Value>>();\n";
  finalCode << "    obj->emplace_back(\"output\", Value(new std::string(out)));\n";
  finalCode << "    obj->emplace_back(\"status\", Value((double)code));\n";
  finalCode << "    obj->emplace_back(\"failed\", Value(code != 0));\n";
  finalCode << "    return Value(obj);\n";
  finalCode << "}\n\n";

  // Inject built-ins
  finalCode << "Value sp_builtin_time(Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    auto now = std::chrono::system_clock::now();\n";
  finalCode << "    auto duration = "
               "std::chrono::duration_cast<std::chrono::milliseconds>(now.time_"
               "since_epoch());\n";
  finalCode << "    return Value((double)duration.count());\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_builtin_timeMicro(Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();\n";
  finalCode << "    return Value((double)std::chrono::duration_cast<std::chrono::microseconds>(now).count());\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_builtin_timeNano(Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();\n";
  finalCode << "    return Value((double)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_builtin_floor(Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "    if (args.empty()) return Value(0.0);\n";
  finalCode << "    return Value(std::floor(args[0].asNumber()));\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_builtin_range(Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "    if (args.size() != 2 || !args[0].isNumber() || !args[1].isNumber()) {\n";
  finalCode << "        throw std::runtime_error(\"range expects 2 numeric arguments (start, end)\");\n";
  finalCode << "    }\n";
  finalCode << "    int start = (int)args[0].asNumber();\n";
  finalCode << "    int end = (int)args[1].asNumber();\n";
  finalCode << "    auto* array = interp.makeArray();\n";
  finalCode << "    for (int i = start; i < end; ++i) {\n";
  finalCode << "        array->push_back(Value((double)i));\n";
  finalCode << "    }\n";
  finalCode << "    return Value(array);\n";
  finalCode << "}\n\n";
  finalCode << "Value sp_builtin_error(Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "    std::string msg = args.empty() ? \"Error\" : args[0].toPureString();\n";
  finalCode << "    int line = (args.size() > 1 && args[1].isNumber()) ? (int)args[1].asNumber() : -1;\n";
  finalCode << "    return Value(new ErrorData(msg, line));\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_get_element(Value arr, Value index) {\n";
  finalCode << "    if (!arr.isArray()) throw std::runtime_error(\"Cannot index non-array\");\n";
  finalCode << "    auto* a = arr.asArray();\n";
  finalCode << "    int idx = (int)index.asNumber();\n";
  finalCode << "    if (idx < 0 || idx >= (int)a->size()) throw std::runtime_error(\"Index out of bounds\");\n";
  finalCode << "    return (*a)[idx];\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_set_property(Value obj, const std::string& prop, Value val) {\n";
  finalCode << "    if (obj.isInstance()) {\n";
  finalCode << "        auto* inst = obj.asInstance();\n";
  finalCode << "        for (size_t i = 0; i < inst->klass->properties.size(); ++i) {\n";
  finalCode << "            if (inst->klass->properties[i].name == prop) {\n";
  finalCode << "                inst->fields[i] = val;\n";
  finalCode << "                return val;\n";
  finalCode << "            }\n";
  finalCode << "        }\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isObject()) {\n";
  finalCode << "        auto* o = obj.asObject();\n";
  finalCode << "        for (auto& p : *o) {\n";
  finalCode << "            if (p.first == prop) { p.second = val; return val; }\n";
  finalCode << "        }\n";
  finalCode << "        o->push_back({prop, val});\n";
  finalCode << "    }\n";
  finalCode << "    return val;\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_get_property(Value obj, const std::string& prop) {\n";
  finalCode << "    if (obj.isInstance()) {\n";
  finalCode << "        auto* inst = obj.asInstance();\n";
  finalCode << "        for (size_t i = 0; i < inst->klass->properties.size(); ++i) {\n";
  finalCode << "            if (inst->klass->properties[i].name == prop) return inst->fields[i];\n";
  finalCode << "        }\n";
  finalCode << "        auto it = inst->klass->methods.find(prop);\n";
  finalCode << "        if (it != inst->klass->methods.end()) {\n";
  finalCode << "            return Value(new BoundMethod(it->second.asFunction(), obj));\n";
  finalCode << "        }\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isArray()) {\n";
  finalCode << "      auto* arr = obj.asArray();\n";
  finalCode << "      if (prop == \"length\") return Value((double)arr->size());\n";
  finalCode << "      if (prop == \"push\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              for (const auto& arg : args) arr->push_back(arg);\n";
  finalCode << "              return Value((double)arr->size());\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"pop\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter&, const std::vector<Value>&) {\n";
  finalCode << "              if (arr->empty()) return Value(Type::NULL_VAL);\n";
  finalCode << "              Value val = arr->back();\n";
  finalCode << "              arr->pop_back();\n";
  finalCode << "              return val;\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"shift\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter&, const std::vector<Value>&) {\n";
  finalCode << "              if (arr->empty()) return Value(Type::NULL_VAL);\n";
  finalCode << "              Value val = (*arr)[0];\n";
  finalCode << "              arr->erase(arr->begin());\n";
  finalCode << "              return val;\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"unshift\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              arr->insert(arr->begin(), args.begin(), args.end());\n";
  finalCode << "              return Value((double)arr->size());\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"join\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              std::string sep = (args.empty() || !args[0].isString()) ? \",\" : *args[0].asString();\n";
  finalCode << "              std::string res = \"\";\n";
  finalCode << "              for (size_t i = 0; i < arr->size(); ++i) {\n";
  finalCode << "                  res += (*arr)[i].toPureString();\n";
  finalCode << "                  if (i < arr->size() - 1) res += sep;\n";
  finalCode << "              }\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"reverse\") {\n";
  finalCode << "          return Value(new NativeFunction([arr, obj](Interpreter&, const std::vector<Value>&) {\n";
  finalCode << "              std::reverse(arr->begin(), arr->end());\n";
  finalCode << "              return obj;\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"slice\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              size_t start = (args.size() > 0 && args[0].isNumber()) ? (size_t)args[0].asNumber() : 0;\n";
  finalCode << "              size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : arr->size();\n";
  finalCode << "              if (start > arr->size()) start = arr->size();\n";
  finalCode << "              if (end > arr->size()) end = arr->size();\n";
  finalCode << "              if (start > end) std::swap(start, end);\n";
  finalCode << "              auto* newArr = interp.makeArray();\n";
  finalCode << "              for (size_t i = start; i < end; ++i) newArr->push_back((*arr)[i]);\n";
  finalCode << "              return Value(newArr);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"contains\" || prop == \"includes\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty()) return Value(false);\n";
  finalCode << "              for (const auto& item : *arr) {\n";
  finalCode << "                  if ((item == args[0]).asBoolean()) return Value(true);\n";
  finalCode << "              }\n";
  finalCode << "              return Value(false);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"indexOf\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty()) return Value(-1.0);\n";
  finalCode << "              for (size_t i = 0; i < arr->size(); ++i) {\n";
  finalCode << "                  if (((*arr)[i] == args[0]).asBoolean()) return Value((double)i);\n";
  finalCode << "              }\n";
  finalCode << "              return Value(-1.0);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"forEach\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isFunction()) throw std::runtime_error(\"forEach requires a function callback.\");\n";
  finalCode << "              ICallable* callback = args[0].asFunction();\n";
  finalCode << "              for (size_t i = 0; i < arr->size(); ++i) {\n";
  finalCode << "                  callback->call(interp, {(*arr)[i], Value((double)i)});\n";
  finalCode << "              }\n";
  finalCode << "              return Value(Type::NULL_VAL);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"map\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isFunction()) throw std::runtime_error(\"map requires a function callback.\");\n";
  finalCode << "              ICallable* callback = args[0].asFunction();\n";
  finalCode << "              auto* newArr = interp.makeArray();\n";
  finalCode << "              for (size_t i = 0; i < arr->size(); ++i) {\n";
  finalCode << "                  newArr->push_back(callback->call(interp, {(*arr)[i], Value((double)i)}));\n";
  finalCode << "              }\n";
  finalCode << "              return Value(newArr);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"filter\") {\n";
  finalCode << "          return Value(new NativeFunction([arr](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isFunction()) throw std::runtime_error(\"filter requires a function callback.\");\n";
  finalCode << "              ICallable* callback = args[0].asFunction();\n";
  finalCode << "              auto* newArr = interp.makeArray();\n";
  finalCode << "              for (size_t i = 0; i < arr->size(); ++i) {\n";
  finalCode << "                  Value res = callback->call(interp, {(*arr)[i], Value((double)i)});\n";
  finalCode << "                  bool isTrue = (res.isBoolean()) ? res.asBoolean() : (!res.isNil() && !res.isUndefined());\n";
  finalCode << "                  if (isTrue) newArr->push_back((*arr)[i]);\n";
  finalCode << "              }\n";
  finalCode << "              return Value(newArr);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isString()) {\n";
  finalCode << "      auto* s = obj.asString();\n";
  finalCode << "      if (prop == \"length\" || prop == \"size\") return Value((double)s->length());\n";
  finalCode << "      if (prop == \"trim\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "              std::string res = *s;\n";
  finalCode << "              res.erase(0, res.find_first_not_of(\" \\t\\n\\r\\f\\v\"));\n";
  finalCode << "              size_t last = res.find_last_not_of(\" \\t\\n\\r\\f\\v\");\n";
  finalCode << "              if (last != std::string::npos) res.erase(last + 1);\n";
  finalCode << "              else if (!res.empty() && isspace(res[0])) res.clear();\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"toLowerCase\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "              std::string res = *s;\n";
  finalCode << "              for (auto& c : res) c = std::tolower(c);\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"toUpperCase\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "              std::string res = *s;\n";
  finalCode << "              for (auto& c : res) c = std::toupper(c);\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"contains\" || prop == \"includes\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isString()) return Value(false);\n";
  finalCode << "              return Value(s->find(*args[0].asString()) != std::string::npos);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"startsWith\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isString()) return Value(false);\n";
  finalCode << "              const std::string& sub = *args[0].asString();\n";
  finalCode << "              return Value(s->compare(0, sub.length(), sub) == 0);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"endsWith\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isString()) return Value(false);\n";
  finalCode << "              const std::string& sub = *args[0].asString();\n";
  finalCode << "              if (sub.length() > s->length()) return Value(false);\n";
  finalCode << "              return Value(s->compare(s->length() - sub.length(), sub.length(), sub) == 0);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"indexOf\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isString()) return Value(-1.0);\n";
  finalCode << "              auto pos = s->find(*args[0].asString());\n";
  finalCode << "              return Value(pos == std::string::npos ? -1.0 : (double)pos);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"split\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              std::string sep = (args.empty() || !args[0].isString()) ? \"\" : *args[0].asString();\n";
  finalCode << "              auto* arr = interp.makeArray();\n";
  finalCode << "              if (sep.empty()) {\n";
  finalCode << "                  for (char c : *s) arr->push_back(Value(interp.makeString(std::string(1, c))));\n";
  finalCode << "              } else {\n";
  finalCode << "                  size_t last = 0;\n";
  finalCode << "                  size_t next = 0;\n";
  finalCode << "                  while ((next = s->find(sep, last)) != std::string::npos) {\n";
  finalCode << "                      arr->push_back(Value(interp.makeString(s->substr(last, next - last))));\n";
  finalCode << "                      last = next + sep.length();\n";
  finalCode << "                  }\n";
  finalCode << "                  arr->push_back(Value(interp.makeString(s->substr(last))));\n";
  finalCode << "              }\n";
  finalCode << "              return Value(arr);\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"replace\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.size() < 2 || !args[0].isString() || !args[1].isString()) return Value(interp.makeString(*s));\n";
  finalCode << "              std::string res = *s;\n";
  finalCode << "              const std::string& from = *args[0].asString();\n";
  finalCode << "              const std::string& to = *args[1].asString();\n";
  finalCode << "              size_t start_pos = res.find(from);\n";
  finalCode << "              if (start_pos != std::string::npos) {\n";
  finalCode << "                  res.replace(start_pos, from.length(), to);\n";
  finalCode << "              }\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"substring\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));\n";
  finalCode << "              size_t start = (size_t)args[0].asNumber();\n";
  finalCode << "              size_t end = (args.size() > 1 && args[1].isNumber()) ? (size_t)args[1].asNumber() : s->length();\n";
  finalCode << "              if (start > s->length()) start = s->length();\n";
  finalCode << "              if (end > s->length()) end = s->length();\n";
  finalCode << "              if (start > end) std::swap(start, end);\n";
  finalCode << "              return Value(interp.makeString(s->substr(start, end - start)));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"repeat\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));\n";
  finalCode << "              int count = (int)args[0].asNumber();\n";
  finalCode << "              if (count <= 0) return Value(interp.makeString(\"\"));\n";
  finalCode << "              std::string res = \"\";\n";
  finalCode << "              for (int i = 0; i < count; i++) res += *s;\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"padStart\") {\n";
  finalCode << "          return Value(new NativeFunction([s](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              if (args.empty() || !args[0].isNumber()) return Value(interp.makeString(*s));\n";
  finalCode << "              size_t targetLen = (size_t)args[0].asNumber();\n";
  finalCode << "              std::string pad = (args.size() > 1 && args[1].isString()) ? *args[1].asString() : \" \";\n";
  finalCode << "              if (s->length() >= targetLen) return Value(interp.makeString(*s));\n";
  finalCode << "              std::string res = *s;\n";
  finalCode << "              while (res.length() < targetLen) {\n";
  finalCode << "                  if (res.length() + pad.length() <= targetLen) res = pad + res;\n";
  finalCode << "                  else res = pad.substr(0, targetLen - res.length()) + res;\n";
  finalCode << "              }\n";
  finalCode << "              return Value(interp.makeString(res));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isDate()) {\n";
  finalCode << "        auto* d = obj.asDate();\n";
  finalCode << "        time_t tt = (time_t)(d->timestamp / 1000);\n";
  finalCode << "        struct tm* tm_info = localtime(&tt);\n";
  finalCode << "        if (prop == \"year\") return Value((double)(tm_info->tm_year + 1900));\n";
  finalCode << "        if (prop == \"month\") return Value((double)(tm_info->tm_mon + 1));\n";
  finalCode << "        if (prop == \"day\") return Value((double)tm_info->tm_mday);\n";
  finalCode << "        if (prop == \"hour\") return Value((double)tm_info->tm_hour);\n";
  finalCode << "        if (prop == \"minute\") return Value((double)tm_info->tm_min);\n";
  finalCode << "        if (prop == \"second\") return Value((double)tm_info->tm_sec);\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isMap()) {\n";
  finalCode << "        auto* m = obj.asMap();\n";
  finalCode << "        if (prop == \"size\") return Value((double)m->map.size());\n";
  finalCode << "        if (prop == \"set\") {\n";
  finalCode << "            return Value(new NativeFunction([m, obj](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "                if (args.size() < 2) throw std::runtime_error(\"Map.set requires 2 arguments.\");\n";
  finalCode << "                m->map[args[0]] = args[1];\n";
  finalCode << "                return obj;\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"get\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "                if (args.empty()) throw std::runtime_error(\"Map.get requires 1 argument.\");\n";
  finalCode << "                auto it = m->map.find(args[0]);\n";
  finalCode << "                return (it == m->map.end()) ? Value(Type::NULL_VAL) : it->second;\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"has\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "                if (args.empty()) throw std::runtime_error(\"Map.has requires 1 argument.\");\n";
  finalCode << "                return Value(m->map.find(args[0]) != m->map.end());\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"delete\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "                if (args.empty()) throw std::runtime_error(\"Map.delete requires 1 argument.\");\n";
  finalCode << "                m->map.erase(args[0]);\n";
  finalCode << "                return Value(Type::NULL_VAL);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"clear\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "                m->map.clear();\n";
  finalCode << "                return Value(Type::NULL_VAL);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"keys\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "                auto* arr = new std::vector<Value>();\n";
  finalCode << "                for (auto const& [key, val] : m->map) arr->push_back(key);\n";
  finalCode << "                return Value(arr);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"values\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "                auto* arr = new std::vector<Value>();\n";
  finalCode << "                for (auto const& [key, val] : m->map) arr->push_back(val);\n";
  finalCode << "                return Value(arr);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"forEach\") {\n";
  finalCode << "            return Value(new NativeFunction([m](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "                if (args.empty()) throw std::runtime_error(\"Map.forEach requires 1 function argument.\");\n";
  finalCode << "                ICallable* func = args[0].asFunction();\n";
  finalCode << "                for (auto const& [key, val] : m->map) {\n";
  finalCode << "                    func->call(interp, {key, val});\n";
  finalCode << "                }\n";
  finalCode << "                return Value(Type::NULL_VAL);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isNumber()) {\n";
  finalCode << "      double n = obj.asNumber();\n";
  finalCode << "      if (prop == \"toFixed\") {\n";
  finalCode << "          return Value(new NativeFunction([n](Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "              int digits = (args.size() > 0 && args[0].isNumber()) ? (int)args[0].asNumber() : 0;\n";
  finalCode << "              std::stringstream ss;\n";
  finalCode << "              ss << std::fixed << std::setprecision(digits) << n;\n";
  finalCode << "              return Value(interp.makeString(ss.str()));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "      if (prop == \"toString\") {\n";
  finalCode << "          return Value(new NativeFunction([obj](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "              return Value(interp.makeString(obj.toPureString()));\n";
  finalCode << "          }), true);\n";
  finalCode << "      }\n";
  finalCode << "    }\n";
  finalCode << "    if (obj.isObject()) {\n";
  finalCode << "        auto* o = obj.asObject();\n";
  finalCode << "        for (const auto& p : *o) {\n";
  finalCode << "            if (p.first == prop) return p.second;\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"keys\") {\n";
  finalCode << "            return Value(new NativeFunction([o](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "                auto* res = interp.makeArray();\n";
  finalCode << "                for (const auto& pair : *o) res->push_back(Value(interp.makeString(pair.first)));\n";
  finalCode << "                return Value(res);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"values\") {\n";
  finalCode << "            return Value(new NativeFunction([o](Interpreter& interp, const std::vector<Value>&) {\n";
  finalCode << "                auto* res = interp.makeArray();\n";
  finalCode << "                for (const auto& pair : *o) res->push_back(pair.second);\n";
  finalCode << "                return Value(res);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "        if (prop == \"has\") {\n";
  finalCode << "            return Value(new NativeFunction([o](Interpreter&, const std::vector<Value>& args) {\n";
  finalCode << "                if (args.empty() || !args[0].isString()) return Value(false);\n";
  finalCode << "                const std::string& key = *args[0].asString();\n";
  finalCode << "                for (const auto& pair : *o) if (pair.first == key) return Value(true);\n";
  finalCode << "                return Value(false);\n";
  finalCode << "            }), true);\n";
  finalCode << "        }\n";
  finalCode << "    }\n";
  finalCode << "    return Value(Type::UNDEFINED);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_builtin_date_now(Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    auto now = std::chrono::system_clock::now().time_since_epoch();\n";
  finalCode << "    auto ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();\n";
  finalCode << "    return Value(interp.makeDate(ms));\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_builtin_console_args(Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    auto* arr = interp.makeArray();\n";
  finalCode << "    for (const auto& a : interp.cliArgs) arr->push_back(Value(interp.makeString(a)));\n";
  finalCode << "    return Value(arr);\n";
  finalCode << "}\n\n";
  finalCode << "Value sp_builtin_console_read(Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    std::string line;\n";
  finalCode << "    if (std::getline(std::cin, line)) return Value(interp.makeString(line));\n";
  finalCode << "    return Value(Type::NULL_VAL);\n";
  finalCode << "}\n\n";
  finalCode << "Value sp_builtin_map_new(Interpreter& interp, const std::vector<Value>& args) {\n";
  finalCode << "    (void)args;\n";
  finalCode << "    return Value(interp.makeMap());\n";
  finalCode << "}\n\n";

  finalCode << "template<typename... Args>\n";
  finalCode << "std::vector<Value> sp_make_args(Args... args) {\n";
  finalCode << "    std::vector<Value> v;\n";
  finalCode << "    v.reserve(sizeof...(args));\n";
  finalCode << "    (v.push_back(args), ...);\n";
  finalCode << "    return v;\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_process_run(const std::string& cmd, const std::vector<Value>& args) {\n";
  finalCode << "    std::string fullCmd = cmd;\n";
  finalCode << "    for (const auto& arg : args) {\n";
  finalCode << "        if (arg.isArray()) {\n";
  finalCode << "            for (const auto& a : *arg.asArray()) fullCmd += \" \" + a.toPureString();\n";
  finalCode << "        } else {\n";
  finalCode << "            fullCmd += \" \" + arg.toPureString();\n";
  finalCode << "        }\n";
  finalCode << "    }\n";
  finalCode << "    std::array<char, 128> buffer;\n";
  finalCode << "    std::string result;\n";
  finalCode << "    FILE* pipe;\n";
  finalCode << "#ifdef _WIN32\n";
  finalCode << "    pipe = _popen((fullCmd + \" 2>&1\").c_str(), \"r\");\n";
  finalCode << "#else\n";
  finalCode << "    pipe = popen((fullCmd + \" 2>&1\").c_str(), \"r\");\n";
  finalCode << "#endif\n";
  finalCode << "    if (!pipe) return makeProcessResult(\"\", 1);\n";
  finalCode << "    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {\n";
  finalCode << "        result += buffer.data();\n";
  finalCode << "    }\n";
  finalCode << "    int status;\n";
  finalCode << "#ifdef _WIN32\n";
  finalCode << "    status = _pclose(pipe);\n";
  finalCode << "#else\n";
  finalCode << "    status = pclose(pipe);\n";
  finalCode << "#endif\n";
  finalCode << "    return makeProcessResult(result, status);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_create(const std::string& path, const std::string& content) {\n";
  finalCode << "    if (fs::exists(path)) {\n";
  finalCode << "        auto* err = new std::vector<std::pair<std::string, Value>>();\n";
  finalCode << "        err->emplace_back(\"error\", Value(new std::string(\"File already exists\")));\n";
  finalCode << "        return Value(err);\n";
  finalCode << "    }\n";
  finalCode << "    std::ofstream f(path);\n";
  finalCode << "    f << content;\n";
  finalCode << "    return Value(Type::NULL_VAL);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_delete(const std::string& path) {\n";
  finalCode << "    fs::remove(path);\n";
  finalCode << "    return Value(Type::NULL_VAL);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_append(const std::string& path, const std::string& content) {\n";
  finalCode << "    std::ofstream f(path, std::ios::app);\n";
  finalCode << "    f << content;\n";
  finalCode << "    return Value(Type::NULL_VAL);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_read_json(const std::string& path) {\n";
  finalCode << "    std::ifstream in(path);\n";
  finalCode << "    if (!in.is_open()) throw std::runtime_error(\"Could not open file for fs.readJson: \" + path);\n";
  finalCode << "    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());\n";
  finalCode << "    size_t pos = 0;\n";
  finalCode << "    return parseJSONValue(content, pos, *Value::CurrentContext);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_write_json(const std::string& path, const Value& val) {\n";
  finalCode << "    std::string json = stringifyJSON(val);\n";
  finalCode << "    std::ofstream out(path, std::ios::trunc);\n";
  finalCode << "    out << json;\n";
  finalCode << "    out.close();\n";
  finalCode << "    return Value(Type::NULL_VAL);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_overwrite(const std::string& path, const std::string& content) {\n";
  finalCode << "    std::ofstream f(path);\n";
  finalCode << "    f << content;\n";
  finalCode << "    f.close();\n";
  finalCode << "    return Value(Type::NULL_VAL);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_read(const std::string& path) {\n";
  finalCode << "    std::ifstream in(path);\n";
  finalCode << "    if (!in.is_open()) return Value(Type::NULL_VAL);\n";
  finalCode << "    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());\n";
  finalCode << "    return Value(new std::string(content));\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_fs_info(Interpreter& interp, const std::string& path) {\n";
  finalCode << "    struct stat st;\n";
  finalCode << "    bool exists = (stat(path.c_str(), &st) == 0);\n";
  finalCode << "    auto* obj = new std::vector<std::pair<std::string, Value>>();\n";
  finalCode << "    obj->emplace_back(\"exists\", Value(exists));\n";
  finalCode << "    if (exists) {\n";
  finalCode << "        obj->emplace_back(\"size\", Value((double)st.st_size));\n";
  finalCode << "        obj->emplace_back(\"length\", Value((double)st.st_size));\n";
  finalCode << "        obj->emplace_back(\"modifiedAt\", interp.makeDate((double)st.st_mtime * 1000.0));\n";
  finalCode << "        obj->emplace_back(\"createdAt\", interp.makeDate((double)st.st_ctime * 1000.0));\n";
  finalCode << "        obj->emplace_back(\"atime\", Value((double)st.st_atime));\n";
  finalCode << "        obj->emplace_back(\"mtime\", Value((double)st.st_mtime));\n";
  finalCode << "        obj->emplace_back(\"ctime\", Value((double)st.st_ctime));\n";
  finalCode << "        obj->emplace_back(\"isDir\", Value(S_ISDIR(st.st_mode)));\n";
  finalCode << "        obj->emplace_back(\"isFile\", Value(S_ISREG(st.st_mode)));\n";
  finalCode << "    } else {\n";
  finalCode << "        obj->emplace_back(\"size\", Value(0.0));\n";
  finalCode << "        obj->emplace_back(\"length\", Value(0.0));\n";
  finalCode << "        obj->emplace_back(\"modifiedAt\", Value(Type::NULL_VAL));\n";
  finalCode << "        obj->emplace_back(\"createdAt\", Value(Type::NULL_VAL));\n";
  finalCode << "    }\n";
  finalCode << "    obj->emplace_back(\"path\", Value(new std::string(path)));\n";
  finalCode << "    // Simple name/ext extraction\n";
  finalCode << "    size_t lastSlash = path.find_last_of(\"/\\\\\");\n";
  finalCode << "    std::string name = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);\n";
  finalCode << "    obj->emplace_back(\"name\", Value(new std::string(name)));\n";
  finalCode << "    size_t lastDot = name.find_last_of('.');\n";
  finalCode << "    std::string ext = (lastDot == std::string::npos || lastDot == 0) ? \"\" : name.substr(lastDot);\n";
  finalCode << "    obj->emplace_back(\"ext\", Value(new std::string(ext)));\n";
  finalCode << "    return Value(obj);\n";
  finalCode << "}\n\n";

  finalCode << "Value sp_process_spawn(const std::string& cmd, const std::vector<Value>& args) {\n";
  finalCode << "    // Simplified spawn for AOT: just run in background or similar\n";
  finalCode << "    // For now, matching run but could be improved\n";
  finalCode << "    return sp_process_run(cmd, args);\n";
  finalCode << "}\n\n";

  // Variables & Functions
  // Ensure globals for built-in objects are declared
  globalsCode << "Value sp_Date;\nValue sp_Map;\nValue sp_HashMap;\nValue sp_console;\n";
  globalsCode << "Value sp_time;\nValue sp_timeMicro;\nValue sp_timeNano;\nValue sp_floor;\nValue sp_range;\nValue sp_Error;\n";

  forwardDecls << "Value sp_builtin_time(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_timeMicro(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_timeNano(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_floor(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_range(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_date_now(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_map_new(Interpreter&, const std::vector<Value>&);\n";
  forwardDecls << "Value sp_builtin_error(Interpreter&, const std::vector<Value>&);\n";

  finalCode << globalsCode.str() << "\n";
  finalCode << forwardDecls.str() << "\n";
  finalCode << functionsCode.str() << "\n";
  finalCode << mainCode.str();

  return finalCode.str();
}

static std::string escapeString(const std::string &s) {
  std::string res;
  for (char c : s) {
    switch (c) {
    case '"': res += "\\\""; break;
    case '\\': res += "\\\\"; break;
    case '\n': res += "\\n"; break;
    case '\r': res += "\\r"; break;
    case '\t': res += "\\t"; break;
    default: res += c; break;
    }
  }
  return res;
}

std::string Transpiler::generateExpression(Expression *expr,
                                           std::ostringstream &outStream) {
  if (dynamic_cast<ThisExpression *>(expr)) {
      return "sp_this";
  }
  if (auto e = dynamic_cast<LiteralExpression *>(expr)) {
    if (e->value.isNumber())
      return "Value(" + std::to_string(e->value.asNumber()) + ")";
    if (e->value.isBoolean())
      return e->value.asBoolean() ? "Value(true)" : "Value(false)";
    if (e->value.isString())
      return "Value(new std::string(\"" + escapeString(*e->value.asString()) + "\"))";
    return "Value(Type::NULL_VAL)";
  }
  if (auto e = dynamic_cast<BigIntLiteralExpression *>(expr)) {
    return "Value(new int64_t(" + e->value + "))";
  }
  if (auto e = dynamic_cast<IdentifierExpression *>(expr)) {
    return "sp_" + e->name;
  }
  if (auto e = dynamic_cast<BinaryExpression *>(expr)) {
    std::string op = e->op;
    if (op == "|>") {
        return "( [&]() -> Value { Value sp__ = " + generateExpression(e->left.get(), outStream) + "; Value r = " + generateExpression(e->right.get(), outStream) + "; if (r.isFunction()) return r.asFunction()->call(interp, {sp__}); return r; }() )";
    }
    return "(" + generateExpression(e->left.get(), outStream) + " " + op + " " +
           generateExpression(e->right.get(), outStream) + ")";
  }
  if (auto e = dynamic_cast<ProcessRunExpression*>(expr)) {
      std::string argsCode = "{";
      for (size_t i = 0; i < e->arguments.size(); ++i) {
          argsCode += generateExpression(e->arguments[i].get(), outStream);
          if (i < e->arguments.size() - 1) argsCode += ", ";
      }
      argsCode += "}";
      return "sp_process_run(" + generateExpression(e->command.get(), outStream) + ".toPureString(), " + argsCode + ")";
  }
  if (auto e = dynamic_cast<ProcessSpawnExpression*>(expr)) {
      std::string argsCode = "{";
      for (size_t i = 0; i < e->arguments.size(); ++i) {
          argsCode += generateExpression(e->arguments[i].get(), outStream);
          if (i < e->arguments.size() - 1) argsCode += ", ";
      }
      argsCode += "}";
      return "sp_process_spawn(" + generateExpression(e->command.get(), outStream) + ".toPureString(), " + argsCode + ")";
  }
  if (auto e = dynamic_cast<FSInfoExpression*>(expr)) {
      return "sp_fs_info(interp, " + generateExpression(e->path.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<FSReadExpression*>(expr)) {
      return "sp_fs_read(" + generateExpression(e->path.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<CallExpression *>(expr)) {
    std::string argsCode = "sp_make_args(";
    for (size_t i = 0; i < e->arguments.size(); ++i) {
        if (e->arguments[i]) argsCode += generateExpression(e->arguments[i].get(), outStream);
        else argsCode += "Value(Type::NULL_VAL)";
        if (i < e->arguments.size() - 1) argsCode += ", ";
    }
    argsCode += ")";
    
    if (auto id = dynamic_cast<IdentifierExpression *>(e->callee.get())) {
        std::string name = id->name;
        return "sp_" + name + ".asFunction()->call(interp, " + argsCode + ")";
    }
    return generateExpression(e->callee.get(), outStream) + ".asFunction()->call(interp, " + argsCode + ")";
  }
  if (auto e = dynamic_cast<ArrayExpression *>(expr)) {
      std::string code = "Value(new std::vector<Value>{";
      for (size_t i = 0; i < e->elements.size(); ++i) {
          code += generateExpression(e->elements[i].get(), outStream);
          if (i < e->elements.size() - 1) code += ", ";
      }
      code += "})";
      return code;
  }
  if (auto e = dynamic_cast<ObjectExpression *>(expr)) {
      std::string code = "Value(new std::vector<std::pair<std::string, Value>>{";
      for (size_t i = 0; i < e->properties.size(); ++i) {
          code += "{\"" + e->properties[i].first + "\", " + generateExpression(e->properties[i].second.get(), outStream) + "}";
          if (i < e->properties.size() - 1) code += ", ";
      }
      code += "})";
      return code;
  }
  if (auto e = dynamic_cast<IndexExpression*>(expr)) {
      return "sp_get_element(" + generateExpression(e->object.get(), outStream) + ", " + generateExpression(e->index.get(), outStream) + ")";
  }
  if (auto e = dynamic_cast<MemberExpression*>(expr)) {
      return "sp_get_property(" + generateExpression(e->object.get(), outStream) + ", \"" + e->property + "\")";
  }
  if (auto e = dynamic_cast<LambdaExpression*>(expr)) {
      static int lambdaId = 0;
      std::string lambdaName = "sp_lambda_" + std::to_string(lambdaId++);
      
      forwardDecls << "Value " << lambdaName << "(Interpreter& interp, const std::vector<Value>& args, Value capturedThis);\n";
      functionsCode << "Value " << lambdaName << "(Interpreter& interp, const std::vector<Value>& args, Value capturedThis) {\n";
      functionsCode << "    (void)args; (void)interp;\n";
      functionsCode << "    Value sp_this = capturedThis;\n";
      for (size_t i = 0; i < e->parameters.size(); ++i) {
          const std::string& pName = e->parameters[i].first;
          functionsCode << "    Value sp_" << pName << " = (args.size() > " << i << ") ? args[" << i << "] : Value(Type::UNDEFINED);\n";
          if (e->parameters[i].second.isPresent) {
              std::string typeStr = e->parameters[i].second.toString();
              functionsCode << "    if (!checkTypeInternal(sp_" << pName << ", \"" << typeStr << "\")) {\n";
              functionsCode << "        sp_" << pName << " = Value(new ErrorData(\"Type mismatch: expected " << typeStr << "\"));\n";
              functionsCode << "    }\n";
          }
      }
      if (auto block = dynamic_cast<BlockExpression*>(e->body.get())) {
          for (auto &bodyStmt : block->statements) {
              generateStatement(&bodyStmt, functionsCode);
          }
          functionsCode << "    return Value(Type::NULL_VAL);\n";
      } else {
          functionsCode << "    return " << generateExpression(e->body.get(), functionsCode) << ";\n";
      }
      functionsCode << "}\n\n";
      
      return "Value(new NativeFunction([sp_this](Interpreter& interp, const std::vector<Value>& args) { return " + lambdaName + "(interp, args, sp_this); }), true)";
  }
  if (auto e = dynamic_cast<ConsoleShowExpression*>(expr)) {
      std::string code = "( [&]() -> Value { ";
      code += "Value last = Value(Type::NULL_VAL); ";
      for (size_t i = 0; i < e->arguments.size(); ++i) {
          code += "last = " + generateExpression(e->arguments[i].get(), outStream) + "; ";
          code += "std::cout << last.toString()";
          if (i < e->arguments.size() - 1) code += " << \" \"";
          code += "; ";
      }
      code += "std::cout << std::endl; return last; }() )";
      return code;
  }
  if (auto e = dynamic_cast<ConsoleWarnExpression*>(expr)) {
      std::string code = "( [&]() -> Value { ";
      code += "Value last = Value(Type::NULL_VAL); ";
      for (size_t i = 0; i < e->arguments.size(); ++i) {
          code += "last = " + generateExpression(e->arguments[i].get(), outStream) + "; ";
          code += "std::cerr << \"\\033[33m[Warning] \\033[0m\" << last.toString()";
          if (i < e->arguments.size() - 1) code += " << \" \"";
          code += "; ";
      }
      code += "std::cerr << std::endl; return last; }() )";
      return code;
  }
  if (auto e = dynamic_cast<IfExpression*>(expr)) {
      // Use a lambda so block bodies with `return` statements work correctly
      std::string code = "( [&]() -> Value { ";
      code += "if ((" + generateExpression(e->condition.get(), outStream) + ").asBoolean()) { ";
      if (auto block = dynamic_cast<BlockExpression*>(e->thenBranch.get())) {
          // Inline the block's statements into the lambda
          std::ostringstream blockStream;
          int savedIndent = indentLevel; indentLevel = 0;
          for (auto& s : block->statements) {
              generateStatement(&s, blockStream);
          }
          indentLevel = savedIndent;
          code += blockStream.str();
          code += "return Value(Type::NULL_VAL); ";
      } else {
          code += "return (" + generateExpression(e->thenBranch.get(), outStream) + "); ";
      }
      code += "} ";
      if (e->elseBranch) {
          code += "else { ";
          if (auto block = dynamic_cast<BlockExpression*>(e->elseBranch.get())) {
              std::ostringstream blockStream;
              int savedIndent = indentLevel; indentLevel = 0;
              for (auto& s : block->statements) {
                  generateStatement(&s, blockStream);
              }
              indentLevel = savedIndent;
              code += blockStream.str();
              code += "return Value(Type::NULL_VAL); ";
          } else {
              code += "return (" + generateExpression(e->elseBranch.get(), outStream) + "); ";
          }
          code += "} ";
      }
      code += "return Value(Type::NULL_VAL); }() )";
      return code;
  }
  if (auto e = dynamic_cast<MatchExpression*>(expr)) {
      std::string code = "( [&]() -> Value { ";
      code += "Value val = " + generateExpression(e->valueToMatch.get(), outStream) + "; ";
      for (const auto& c : e->cases) {
          if (c.pattern) {
              code += "if (val == " + generateExpression(c.pattern.get(), outStream) + ") ";
          }
          code += "return " + generateExpression(c.body.get(), outStream) + "; ";
      }
      code += "return Value(Type::NULL_VAL); }() )";
      return code;
  }
  if (auto e = dynamic_cast<FSCreateExpression*>(expr)) {
      return "sp_fs_create(" + generateExpression(e->path.get(), outStream) + ".toPureString(), " + generateExpression(e->content.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<FSDeleteExpression*>(expr)) {
      return "sp_fs_delete(" + generateExpression(e->path.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<FSAppendExpression*>(expr)) {
      return "sp_fs_append(" + generateExpression(e->path.get(), outStream) + ".toPureString(), " + generateExpression(e->content.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<FSReadJsonExpression*>(expr)) {
      return "sp_fs_read_json(" + generateExpression(e->path.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<FSWriteJsonExpression*>(expr)) {
      return "sp_fs_write_json(" + generateExpression(e->path.get(), outStream) + ".toPureString(), " + generateExpression(e->value.get(), outStream) + ")";
  }
  if (auto e = dynamic_cast<FSOverwriteExpression*>(expr)) {
      return "sp_fs_overwrite(" + generateExpression(e->path.get(), outStream) + ".toPureString(), " + generateExpression(e->content.get(), outStream) + ".toPureString())";
  }
  if (auto e = dynamic_cast<MemberAssignmentExpression*>(expr)) {
      return "sp_set_property(" + generateExpression(e->object.get(), outStream) + ", \"" + e->property + "\", " + generateExpression(e->value.get(), outStream) + ")";
  }
  if (auto e = dynamic_cast<TrimExpression*>(expr)) {
      return "Value(new std::string( (*" + generateExpression(e->argument.get(), outStream) + ".asString()) ))"; 
  }
  if (auto e = dynamic_cast<StringSizeExpression*>(expr)) {
      return "Value((double)" + generateExpression(e->argument.get(), outStream) + ".asString()->size())";
  }
  if (auto e = dynamic_cast<AssignmentExpression*>(expr)) {
      return "(" + std::string("sp_") + e->name + " = " + generateExpression(e->value.get(), outStream) + ")";
  }
  if (auto e = dynamic_cast<IndexAssignmentExpression*>(expr)) {
      return "sp_set_element(" + generateExpression(e->object.get(), outStream) + ", " +
             generateExpression(e->index.get(), outStream) + ", " +
             generateExpression(e->value.get(), outStream) + ")";
  }
  return "Value(Type::NULL_VAL)";
}

void Transpiler::generateStatement(Statement *stmt,
                                   std::ostringstream &outStream) {
  if (auto funcDecl = std::get_if<FunctionDeclaration>(&*stmt)) {
    std::string implName = "sp_impl_" + funcDecl->name;

    globalsCode << "Value sp_" << funcDecl->name << ";\n";

    forwardDecls << "Value " << implName
                 << "(Interpreter&, const std::vector<Value>&);\n";

    functionsCode << "Value " << implName << "(Interpreter& interp, const std::vector<Value>& args) {\n";
    functionsCode << "    (void)args;\n";
    functionsCode << "    Value sp_this = interp.environment->has(\"this\") ? interp.environment->get(\"this\") : Value(Type::UNDEFINED);\n";
    for (size_t i = 0; i < funcDecl->parameters.size(); ++i) {
      const std::string& pName = funcDecl->parameters[i].first;
      functionsCode << "    Value sp_" << pName << " = (args.size() > " << i << ") ? args["
                    << i << "] : Value(Type::UNDEFINED);\n";
      if (funcDecl->parameters[i].second.isPresent) {
          std::string typeStr = funcDecl->parameters[i].second.toString();
          functionsCode << "    if (!checkTypeInternal(sp_" << pName << ", \"" << typeStr << "\")) {\n";
          functionsCode << "        sp_" << pName << " = Value(new ErrorData(\"Type mismatch: expected " << typeStr << "\"));\n";
          functionsCode << "    }\n";
      }
    }

    indentLevel++;

    if (auto block = dynamic_cast<BlockExpression *>(funcDecl->body.get())) {
      bool prevInFunction = inFunction;
      std::unordered_set<std::string> prevVariables = variables;
      inFunction = true;
      for (auto &bodyStmt : block->statements) {
        generateStatement(&bodyStmt, functionsCode);
      }
      inFunction = prevInFunction;
      variables = prevVariables;
      functionsCode << "    return Value(Type::NULL_VAL);\n";
    } else {
      functionsCode << "    return " << generateExpression(funcDecl->body.get(), functionsCode) << ";\n";
    }

    indentLevel--;
    functionsCode << "}\n\n";
    
    outStream << indent() << "sp_" << funcDecl->name << " = Value(new NativeFunction([](Interpreter& interp, const std::vector<Value>& args) { return " << implName << "(interp, args); }), true);\n";
    outStream << indent() << "interp.environment->define(\"" << funcDecl->name << "\", sp_" << funcDecl->name << ");\n";

  } else if (auto varDecl = std::get_if<VariableDeclaration>(&*stmt)) {
    if (inFunction) {
      if (!variables.count(varDecl->name)) {
        outStream << indent() << "Value sp_" << varDecl->name << " = "
                  << generateExpression(varDecl->value.get(), outStream) << ";\n";
        variables.insert(varDecl->name);
      } else {
        outStream << indent() << "sp_" << varDecl->name << " = "
                  << generateExpression(varDecl->value.get(), outStream) << ";\n";
      }
    } else {
      if (!variables.count(varDecl->name)) {
          globalsCode << "Value sp_" << varDecl->name << ";\n";
          variables.insert(varDecl->name);
      }
      outStream << indent() << "sp_" << varDecl->name << " = "
                << generateExpression(varDecl->value.get(), outStream) << ";\n";
      outStream << indent() << "interp.environment->define(\"" << varDecl->name << "\", sp_" << varDecl->name << ");\n";
    }
  } else if (auto classDecl = std::get_if<ClassDeclaration>(&*stmt)) {
      std::string className = "sp_" + classDecl->name;
      if (!variables.count(classDecl->name)) {
          globalsCode << "Value " << className << ";\n";
          variables.insert(classDecl->name);
      }
      outStream << indent() << "{\n";
      indentLevel++;
      outStream << indent() << "auto* k = interp.makeClass(\"" << classDecl->name << "\", " << (classDecl->isAbstract ? "true" : "false") << ");\n";
      for (const auto& prop : classDecl->properties) {
          std::string initVal = "Value()";
          if (prop.initializer_value.bits != Value().bits && !prop.initializer_value.isUndefined()) {
              if (prop.initializer_value.isNumber()) initVal = "Value(" + std::to_string(prop.initializer_value.asNumber()) + ")";
              else if (prop.initializer_value.isString()) initVal = "Value(interp.makeString(\"" + escapeString(*prop.initializer_value.asString()) + "\"))";
              else if (prop.initializer_value.isBoolean()) initVal = "Value(" + std::string(prop.initializer_value.asBoolean() ? "true" : "false") + ")";
          }
          outStream << indent() << "k->properties.push_back({\"" << prop.name << "\", nullptr, " << initVal << ", " << (prop.isPrivate ? "true" : "false") << ", " << (prop.isReadonly ? "true" : "false") << "});\n";
      }
      for (const auto& method : classDecl->methods) {
          std::string methodName = "sp_method_" + classDecl->name + "_" + method.name;
          forwardDecls << "Value " << methodName << "(Interpreter& interp, const std::vector<Value>& args);\n";
          functionsCode << "Value " << methodName << "(Interpreter& interp, const std::vector<Value>& args) {\n";
          functionsCode << "    (void)args; (void)interp;\n";
          functionsCode << "    Value sp_this = args[0];\n";
          // For methods, args[0] is 'this', args[1..N] are parameters
          for (size_t i = 0; i < method.parameters.size(); ++i) {
              const std::string& pName = method.parameters[i].first;
              functionsCode << "    Value sp_" << pName << " = (args.size() > " << i + 1 << ") ? args[" << i + 1 << "] : Value(Type::UNDEFINED);\n";
              if (method.parameters[i].second.isPresent) {
                  std::string typeStr = method.parameters[i].second.toString();
                  functionsCode << "    if (!checkTypeInternal(sp_" << pName << ", \"" << typeStr << "\")) {\n";
                  functionsCode << "        sp_" << pName << " = Value(new ErrorData(\"Type mismatch: expected " << typeStr << "\"), Type::ERROR);\n";
                  functionsCode << "    }\n";
              }
          }
          if (auto block = dynamic_cast<BlockExpression*>(method.body.get())) {
              for (auto &bodyStmt : block->statements) {
                  generateStatement(&bodyStmt, functionsCode);
              }
              functionsCode << "    return Value(Type::NULL_VAL);\n";
          } else {
              functionsCode << "    return " << generateExpression(method.body.get(), functionsCode) << ";\n";
          }
          functionsCode << "}\n\n";
          outStream << indent() << "k->methods[\"" << method.name << "\"] = Value(new NativeFunction([](Interpreter& interp, const std::vector<Value>& args) { return " << methodName << "(interp, args); }), true);\n";
      }
      outStream << indent() << className << " = Value(k);\n";
      outStream << indent() << "interp.environment->define(\"" << classDecl->name << "\", " << className << ");\n";
      indentLevel--;
      outStream << indent() << "}\n";
  } else if (auto retStmt = std::get_if<ReturnStatement>(&*stmt)) {
    if (retStmt->value) {
      outStream << indent() << "return "
                << generateExpression(retStmt->value.get(), outStream) << ";\n";
    } else {
      outStream << indent() << "return Value(Type::NULL_VAL);\n";
    }
  } else if (auto whileStmt = std::get_if<WhileStatement>(&*stmt)) {
    outStream << indent() << "while (true) {\n";
    indentLevel++;
    outStream << indent() << "Value cond = " << generateExpression(whileStmt->condition.get(), outStream) << ";\n";
    outStream << indent() << "bool isTrue = (cond.isBoolean()) ? cond.asBoolean() : (!cond.isNil() && !cond.isUndefined());\n";
    outStream << indent() << "if (!isTrue) break;\n";
    if (auto block = dynamic_cast<BlockExpression*>(whileStmt->body.get())) {
        for (auto &branchStmt : block->statements) {
            generateStatement(&branchStmt, outStream);
        }
    } else {
        generateExpression(whileStmt->body.get(), outStream);
    }
    indentLevel--;
    outStream << indent() << "}\n";
  } else if (auto forStmt = std::get_if<ForStatement>(&*stmt)) {
    outStream << indent() << "{\n";
    indentLevel++;
    outStream << indent() << "Value col = " << generateExpression(forStmt->collection.get(), outStream) << ";\n";
    outStream << indent() << "if (!col.isArray()) throw std::runtime_error(\"For loop collection must be an array\");\n";
    outStream << indent() << "for (const auto& item : *col.asArray()) {\n";
    indentLevel++;
    
    outStream << indent() << "Value sp_" << forStmt->variableName << " = item;\n";
    
    if (auto block = dynamic_cast<BlockExpression*>(forStmt->body.get())) {
        for (auto &branchStmt : block->statements) {
            generateStatement(&branchStmt, outStream);
        }
    } else {
        generateExpression(forStmt->body.get(), outStream);
    }
    
    indentLevel--;
    outStream << indent() << "}\n";
    indentLevel--;
    outStream << indent() << "}\n";
  } else if (auto printStmt = std::get_if<PrintStatement>(&*stmt)) {
    outStream << indent() << "std::cout";
    if (!printStmt->exprs.empty()) {
      outStream << " << ";
      for (size_t i = 0; i < printStmt->exprs.size(); ++i) {
        outStream << generateExpression(printStmt->exprs[i].get(), outStream);
        if (i < printStmt->exprs.size() - 1) {
          outStream << " << \" \" << ";
        }
      }
    }
    outStream << " << std::endl;\n";
  } else if (auto warnStmt = std::get_if<WarnStatement>(&*stmt)) {
    outStream << indent() << "std::cerr << \"\\033[33m[Warning] \\033[0m\"";
    if (!warnStmt->exprs.empty()) {
      outStream << " << ";
      for (size_t i = 0; i < warnStmt->exprs.size(); ++i) {
        outStream << generateExpression(warnStmt->exprs[i].get(), outStream);
        if (i < warnStmt->exprs.size() - 1) {
          outStream << " << \" \" << ";
        }
      }
    }
    outStream << " << std::endl;\n";
  } else if (auto exprStmt = std::get_if<std::shared_ptr<Expression>>(&*stmt)) {
    outStream << indent() << generateExpression(exprStmt->get(), outStream) << ";\n";
  }
}