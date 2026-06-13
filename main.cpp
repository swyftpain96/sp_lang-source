#include <cmath>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <chrono>
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "compiler.h"
#include "vm.h"
#include "interpreter.h"
#include "transpiler.h"

void registerNetModule(class VM& vm, class Interpreter& interp);
void registerSqliteModule(class VM& vm, class Interpreter& interp);
void registerStorageModule(class VM& vm, class Interpreter& interp);


int main(int argc, char* argv[]) {
    fflush(stdout);
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << argv[1] << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    bool useAot = false;
    bool useVm = true;
    bool showWarnings = true;
    bool useColor = true;
    std::vector<std::string> cliArgs;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--aot") {
            useAot = true;
        } else if (arg == "--interpreter") {
            useVm = false;
        } else if (arg == "--vm") {
            useVm = true;
        } else if (arg == "--no-warnings") {
            showWarnings = false;
        } else if (arg == "--no-color") {
            useColor = false;
        } else {
            cliArgs.push_back(arg);
        }
    }
    fflush(stdout);

    try {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        std::vector<Statement> statements = parser.parse();

        Resolver resolver;
        resolver.resolveModule(statements);

        if (useAot) {
            std::vector<std::shared_ptr<Statement>> sharedStatements;
            for (auto& s : statements) {
                sharedStatements.push_back(std::make_shared<Statement>(std::move(s)));
            }
            Transpiler transpiler;
            std::string cppCode = transpiler.transpile(sharedStatements);
            std::ofstream outFile("out.cpp");
            outFile << cppCode;
            outFile.close();
            int ret;
#ifdef _WIN32
            ret = system("g++ -std=c++17 -Wall -Wextra -O3 -flto=auto out.cpp types.o interpreter.o vm.o lexer.o parser.o resolver.o compiler.o -o out_bin.exe -lws2_32 -lcrypt32 -lwsock32");
#else
            ret = system("g++ -std=c++17 -Wall -Wextra -O3 -march=native -flto=auto out.cpp types.o interpreter.o vm.o lexer.o parser.o resolver.o compiler.o -o out_bin -ldl -pthread");
#endif
            if (ret != 0) {
                std::cerr << "Native compilation failed." << std::endl;
                return ret;
            }
#ifdef _WIN32
            std::string runCmd = "out_bin.exe";
#else
            std::string runCmd = "./out_bin";
#endif
            for (const auto& arg : cliArgs) runCmd += " \"" + arg + "\"";
            return system(runCmd.c_str());
        }

        VM vm;
        vm.showWarnings = showWarnings;
        vm.useColor = useColor;
        vm.cliArgs = cliArgs;
        
        Interpreter interpreter(&vm);
        interpreter.cliArgs = cliArgs;
        Value::CurrentContext = &interpreter;
        

        Compiler compiler;
        CompilationResult result = compiler.compile("main", statements, resolver.mainLocalCount);

        if (!result.mainFunction) {
            std::cerr << "Compilation failed: no main function" << std::endl;
            return 1;
        }

        auto floorFuncReal = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
            if (args.empty() || !args[0].isNumber()) return Value(0.0);
            return Value(std::floor(args[0].asNumber()));
        });
        Value::registerFunction(floorFuncReal);
        vm.defineGlobal("floor", Value(floorFuncReal.get(), true));
        interpreter.environment->define("floor", Value(floorFuncReal.get(), true));

        auto timeFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return Value(ms);
        });
        Value::registerFunction(timeFunc);
        vm.defineGlobal("time", Value(timeFunc.get(), true));
        interpreter.environment->define("time", Value(timeFunc.get(), true));

        // timeMicro() - returns microseconds since epoch
        auto timeMicroFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
            auto us = (double)std::chrono::duration_cast<std::chrono::microseconds>(now).count();
            return Value(us);
        });
        Value::registerFunction(timeMicroFunc);
        vm.defineGlobal("timeMicro", Value(timeMicroFunc.get(), true));
        interpreter.environment->define("timeMicro", Value(timeMicroFunc.get(), true));

        // timeNano() - returns nanoseconds since epoch
        auto timeNanoFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
            auto ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            return Value(ns);
        });
        Value::registerFunction(timeNanoFunc);
        vm.defineGlobal("timeNano", Value(timeNanoFunc.get(), true));
        interpreter.environment->define("timeNano", Value(timeNanoFunc.get(), true));

        auto errorFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
            std::string msg = args.empty() ? "Error" : args[0].toPureString();
            int line = (args.size() > 1 && args[1].isNumber()) ? (int)args[1].asNumber() : -1;
            auto err = std::make_shared<ErrorData>(msg, line);
            Value::registerError(err);
            return Value(err.get());
        });
        Value::registerFunction(errorFunc);
        vm.defineGlobal("Error", Value(errorFunc.get(), true));
        interpreter.environment->define("Error", Value(errorFunc.get(), true));

        auto rangeFuncNative = std::make_shared<NativeFunction>([](Interpreter& interpreter, const std::vector<Value>& args) {
            if (args.size() != 2 || !args[0].isNumber() || !args[1].isNumber()) {
                throw std::runtime_error("range expects 2 numeric arguments (start, end)");
            }
            int start = (int)args[0].asNumber();
            int end = (int)args[1].asNumber();
            auto* array = interpreter.makeArray();
            for (int i = start; i < end; i++) {
                array->push_back(Value((double)i));
            }
            return Value(array);
        });
        Value::registerFunction(rangeFuncNative);
        vm.defineGlobal("range", Value(rangeFuncNative.get(), true));
        interpreter.environment->define("range", Value(rangeFuncNative.get(), true));

        auto consoleShowFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
            Value last;
            for (size_t i = 0; i < args.size(); ++i) {
                last = args[i];
                std::cout << last.toString();
                if (i < args.size() - 1) std::cout << " ";
            }
            std::cout << std::endl;
            return last;
        });
        Value::registerFunction(consoleShowFunc);

        auto consoleArgsFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            auto* arr = interp.makeArray();
            for (const auto& a : interp.cliArgs) arr->push_back(Value(interp.makeString(a)));
            return Value(arr);
        });
        Value::registerFunction(consoleArgsFunc);

        auto consoleReadFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            std::string line;
            if (std::getline(std::cin, line)) return Value(interp.makeString(line));
            return Value(Type::NULL_VAL);
        });
        Value::registerFunction(consoleReadFunc);

        auto consoleWarnFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
            (void)interp;
            for (size_t i = 0; i < args.size(); ++i) {
                std::cerr << "\033[33m[Warning] \033[0m" << args[i].toString();
                if (i < args.size() - 1) std::cerr << " ";
            }
            std::cerr << std::endl;
            return args.empty() ? Value(Type::NULL_VAL) : args.back();
        });
        Value::registerFunction(consoleWarnFunc);

        auto* cObj = interpreter.makeObject();
        cObj->push_back({"show", Value(consoleShowFunc.get(), true)});
        cObj->push_back({"args", Value(consoleArgsFunc.get(), true)});
        cObj->push_back({"read", Value(consoleReadFunc.get(), true)});
        cObj->push_back({"warn", Value(consoleWarnFunc.get(), true)});
        Value consoleVal(cObj);
        vm.defineGlobal("console", consoleVal);
        interpreter.environment->define("console", consoleVal);

        auto exitFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
            int code = args.empty() ? 0 : (int)args[0].asNumber();
            std::exit(code);
            return Value();
        });
        Value::registerFunction(exitFunc);
        vm.defineGlobal("exit", Value(exitFunc.get(), true));
        interpreter.environment->define("exit", Value(exitFunc.get(), true));

        auto dateNowFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return Value(interp.makeDate(ms));
        });
        Value::registerFunction(dateNowFunc);
        auto* dObj = interpreter.makeObject();
        dObj->push_back({"now", Value(dateNowFunc.get(), true)});
        Value dateVal(dObj);
        vm.defineGlobal("Date", dateVal);
        interpreter.environment->define("Date", dateVal);

        auto mapConstructorFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            return Value(interp.makeMap());
        });
        Value::registerFunction(mapConstructorFunc);
        vm.defineGlobal("Map", Value(mapConstructorFunc.get(), true));
        vm.defineGlobal("HashMap", Value(mapConstructorFunc.get(), true));

        if (interpreter.environment->has("regex")) {
            vm.defineGlobal("regex", interpreter.environment->get("regex"));
        }

        registerNetModule(vm, interpreter);
        registerSqliteModule(vm, interpreter);
        registerStorageModule(vm, interpreter);

        auto jsonStringifyFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
            if (args.empty()) return Value(Type::UNDEFINED);
            return Value(new std::string(stringifyJSON(args[0])));
        });
        Value::registerFunction(jsonStringifyFunc);

        auto jsonParseFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
            if (args.empty() || !args[0].isString()) return Value(Type::NULL_VAL);
            size_t pos = 0;
            try {
                return parseJSONValue(*args[0].asString(), pos, interp);
            } catch (...) {
                return Value(Type::NULL_VAL);
            }
        });
        Value::registerFunction(jsonParseFunc);

        auto* jObj = interpreter.makeObject();
        jObj->push_back({"stringify", Value(jsonStringifyFunc.get(), true)});
        jObj->push_back({"parse", Value(jsonParseFunc.get(), true)});
        Value jsonVal(jObj);
        vm.defineGlobal("JSON", jsonVal);
        interpreter.environment->define("JSON", jsonVal);


        interpreter.callHandler = [&](ICallable* f, const std::vector<Value>& args) {
            return vm.call(interpreter, f, args);
        };
        
        struct FutureJoiner {
            ~FutureJoiner() { Value::joinAllFutures(); }
        } joiner;
        
        if (!useVm) {
            interpreter.interpret(statements);
            return 0;
        }

        vm.run(result.mainFunction.get(), interpreter);
        
    } catch (const std::exception& e) {
        if (useColor) std::cerr << "\033[31m\033[1m[Error] \033[0m\033[31m" << e.what() << "\033[0m" << std::endl;
        else std::cerr << "[Error] " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}