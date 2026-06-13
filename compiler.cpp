#include "compiler.h"
#include <stdexcept>
#include <variant>
#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "builtin_modules.h"

CompilationResult Compiler::compile(const std::string& name, const std::vector<Statement>& statements, int localCount) {
    CompilationResult result;
    currentFunction = std::make_shared<VMFunction>(name, 0, localCount);
    allFunctions.push_back(currentFunction);
    
    compile(statements);
    
    emitByte(static_cast<uint8_t>(OpCode::RETURN));
    
    result.mainFunction = currentFunction;
    result.allFunctions = std::move(allFunctions);
    return result;
}

void Compiler::compile(const std::vector<Statement>& statements) {
    // Hoist
    for (const auto& stmt : statements) {
        if (std::holds_alternative<FunctionDeclaration>(stmt)) {
            compile(stmt);
        }
    }
    // Rest
    for (const auto& stmt : statements) {
        if (std::holds_alternative<FunctionDeclaration>(stmt)) continue;
        compile(stmt);
    }
}

void Compiler::compile(const Statement& stmt) {
    std::visit([this](auto&& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<Expression>>) {
            if (s && s->line != -1) this->currentLine = s->line;
        } else {
            if (s.line != -1) this->currentLine = s.line;
        }
    }, stmt);

    if (std::holds_alternative<VariableDeclaration>(stmt)) {
        const auto& decl = std::get<VariableDeclaration>(stmt);
        compile(decl.value);
        if (decl.type.isPresent) {
            std::string typeStr = decl.type.toString();
            auto strPtr = std::make_shared<std::string>(typeStr);
            Value::registerString(strPtr);
            currentFunction->chunk.constants.push_back(Value(strPtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::CHECK_TYPE), (uint8_t)(currentFunction->chunk.constants.size() - 1));
        }
        if (decl.index != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(decl.index));
            emitByte(static_cast<uint8_t>(OpCode::POP));
        } else {
            auto namePtr = std::make_shared<std::string>(decl.name);
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
            emitByte(decl.isMutable ? 1 : 0);
        }
    } else if (std::holds_alternative<DestructuringDeclaration>(stmt)) {
        const auto& decl = std::get<DestructuringDeclaration>(stmt);
        compile(decl.initializer);
        compileDestructuring(decl.bindings, decl.isMutable);
        emitByte(static_cast<uint8_t>(OpCode::POP)); // Pop the initializer value
    } else if (std::holds_alternative<FunctionDeclaration>(stmt)) {
        const auto& decl = std::get<FunctionDeclaration>(stmt);
        
        auto prevFunc = currentFunction;
        auto nestedFunc = std::make_shared<VMFunction>(decl.name, (int)decl.parameters.size(), decl.localCount);
        nestedFunc->hasRest = decl.hasRest;
        allFunctions.push_back(nestedFunc);
        
        currentFunction = nestedFunc;
        // Emit type checks for parameters
        for (size_t i = 0; i < decl.parameters.size(); ++i) {
            if (decl.parameters[i].second.isPresent) {
                emitBytes(static_cast<uint8_t>(OpCode::GET_LOCAL), static_cast<uint8_t>(i));
                std::string typeStr = decl.parameters[i].second.toString();
                auto strPtr = std::make_shared<std::string>(typeStr);
                Value::registerString(strPtr);
                currentFunction->chunk.constants.push_back(Value(strPtr.get()));
                emitBytes(static_cast<uint8_t>(OpCode::CHECK_TYPE), (uint8_t)(currentFunction->chunk.constants.size() - 1));
                emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(i));
                emitByte(static_cast<uint8_t>(OpCode::POP));
            }
        }
        compile(decl.body);
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
        
        currentFunction = prevFunc;
        
        emitConstant(Value(nestedFunc.get()));
        if (decl.index != -1) {
             emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(decl.index));
        } else {
            auto namePtr = std::make_shared<std::string>(decl.name);
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
            emitByte(0); // Functions are immutable globals
        }
    } else if (std::holds_alternative<PrintStatement>(stmt)) {
        const auto& print = std::get<PrintStatement>(stmt);
        for (const auto& expr : print.exprs) {
            compile(expr);
        }
        emitBytes(static_cast<uint8_t>(OpCode::PRINT), (uint8_t)print.exprs.size());
        emitByte(static_cast<uint8_t>(OpCode::POP));
    } else if (std::holds_alternative<WarnStatement>(stmt)) {
        const auto& warn = std::get<WarnStatement>(stmt);
        for (const auto& expr : warn.exprs) {
            compile(expr);
        }
        emitBytes(static_cast<uint8_t>(OpCode::WARN), (uint8_t)warn.exprs.size());
        emitByte(static_cast<uint8_t>(OpCode::POP));
    } else if (std::holds_alternative<ReturnStatement>(stmt)) {
        const auto& ret = std::get<ReturnStatement>(stmt);
        if (ret.value) compile(ret.value);
        else emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
    } else if (std::holds_alternative<UseStatement>(stmt)) {
        const auto& useStmt = std::get<UseStatement>(stmt);

        std::string rawModuleName = useStmt.moduleName;
        if (rawModuleName.size() >= 2 && rawModuleName.front() == '"' && rawModuleName.back() == '"') {
            rawModuleName = rawModuleName.substr(1, rawModuleName.size() - 2);
        }

        if (rawModuleName == "fs" || rawModuleName == "console" || rawModuleName == "process") {
            return;
        }

        // Search paths:
        // 1. ./{name}.sp
        // 2. ./modules/{name}.sp
        // 3. ./modules/lib{name}.so -> if found, it's native
        
        std::string spPath = rawModuleName + ".sp";
        std::string spdPath = rawModuleName + ".spd";
        std::string modSpPath = "modules/" + rawModuleName + ".sp";
        std::string modSpdPath = "modules/" + rawModuleName + ".spd";
        std::string nativePath = "modules/lib" + rawModuleName + ".so";
        
        std::string selectedPath;
        bool isNative = false;
        bool isBuiltin = false;

        if (builtinModules.find(rawModuleName) != builtinModules.end()) {
            isBuiltin = true;
        } else if (std::ifstream(spPath).good()) {
            selectedPath = spPath;
        } else if (std::ifstream(spdPath).good()) {
            selectedPath = spdPath;
        } else if (std::ifstream(modSpPath).good()) {
            selectedPath = modSpPath;
        } else if (std::ifstream(modSpdPath).good()) {
            selectedPath = modSpdPath;
        } else if (std::ifstream(nativePath).good()) {
            selectedPath = nativePath;
            isNative = true;
        } else if (rawModuleName.find("native_") == 0) {
            isNative = true; // Fallback
        }

        if (isNative) {
            for (const auto& [exp, alias] : useStmt.namedImports) {
                // Module name constant
                auto modNamePtr = std::make_shared<std::string>(rawModuleName);
                Value::registerString(modNamePtr);
                currentFunction->chunk.constants.push_back(Value(modNamePtr.get()));
                uint8_t modIdx = (uint8_t)(currentFunction->chunk.constants.size() - 1);

                // Symbol name constant
                auto symNamePtr = std::make_shared<std::string>(exp);
                Value::registerString(symNamePtr);
                currentFunction->chunk.constants.push_back(Value(symNamePtr.get()));
                uint8_t symIdx = (uint8_t)(currentFunction->chunk.constants.size() - 1);

                emitBytes(static_cast<uint8_t>(OpCode::IMPORT_NATIVE), modIdx);
                emitByte(symIdx);

                // Define as global
                auto aliasPtr = std::make_shared<std::string>(alias);
                Value::registerString(aliasPtr);
                currentFunction->chunk.constants.push_back(Value(aliasPtr.get()));
                emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
                emitByte(0); // Imports are immutable
            }
            return;
        }

        if (!isBuiltin && selectedPath.empty()) {
            throw std::runtime_error("Could not find module: " + rawModuleName);
        }

        std::string source;
        if (isBuiltin) {
            source = builtinModules[rawModuleName];
        } else {
            std::ifstream file(selectedPath);
            if (!file.is_open()) {
                throw std::runtime_error("Could not open module file for compilation: " + selectedPath);
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            source = buffer.str();
        }
        
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        auto modStmts = parser.parse();
        
        Resolver resolver;
        resolver.resolve(modStmts);

        importedModules[rawModuleName] = rawModuleName;
        
        std::string prevPrefix = currentModulePrefix;
        
        if (useStmt.isNamed) {
            // Build a set of (exportName -> localAlias) for fast lookup
            std::unordered_map<std::string,std::string> wanted;
            for (const auto& [exp, alias] : useStmt.namedImports) {
                wanted[exp] = alias;
            }
            // Compile the module with a temporary prefix, then rename exports
            currentModulePrefix = useStmt.moduleName + ".";
            for (const auto& modStmt : modStmts) {
                compile(modStmt);
            }
            // The named symbols are currently stored as "moduleName.exportName"
            // We need to alias them to the local name in the VM globals.
            // Emit a DEFINE_GLOBAL for each alias pointing to the module-prefixed name.
            for (const auto& [exp, alias] : useStmt.namedImports) {
                std::string fullName = useStmt.moduleName + "." + exp;
                // GET_GLOBAL fullName -> push value on stack
                auto fullNamePtr = std::make_shared<std::string>(fullName);
                Value::registerString(fullNamePtr);
                currentFunction->chunk.constants.push_back(Value(fullNamePtr.get()));
                emitBytes(static_cast<uint8_t>(OpCode::GET_GLOBAL),
                          (uint8_t)(currentFunction->chunk.constants.size() - 1));
                // DEFINE_GLOBAL alias -> pop and store under alias
                auto aliasPtr = std::make_shared<std::string>(alias);
                Value::registerString(aliasPtr);
                currentFunction->chunk.constants.push_back(Value(aliasPtr.get()));
                emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL),
                          (uint8_t)(currentFunction->chunk.constants.size() - 1));
                emitByte(0); // Imports are immutable
            }
        } else {
            // Plain use or aliased use: bind all exports under "name." or "alias."
            std::string bindName = useStmt.alias.empty() ? useStmt.moduleName : useStmt.alias;
            importedModules[bindName] = useStmt.moduleName; // alias resolves to real module name
            currentModulePrefix = bindName + ".";
            for (const auto& modStmt : modStmts) {
                compile(modStmt);
            }
        }
        currentModulePrefix = prevPrefix;
    } else if (std::holds_alternative<ExportStatement>(stmt)) {
        const auto& expStmt = std::get<ExportStatement>(stmt);
        if (std::holds_alternative<VariableDeclaration>(expStmt.declaration)) {
            auto decl = std::get<VariableDeclaration>(expStmt.declaration);
            decl.name = currentModulePrefix + decl.name;
            compile(decl);
        } else if (std::holds_alternative<FunctionDeclaration>(expStmt.declaration)) {
            auto decl = std::get<FunctionDeclaration>(expStmt.declaration);
            decl.name = currentModulePrefix + decl.name;
            compile(decl);
        }
    } else if (std::holds_alternative<WhileStatement>(stmt)) {
        const auto& whileStmt = std::get<WhileStatement>(stmt);
        int loopStart = (int)currentFunction->chunk.code.size();
        compile(whileStmt.condition);
        int exitJump = emitJump(OpCode::JUMP_IF_FALSE);
        compile(whileStmt.body);
        emitByte(static_cast<uint8_t>(OpCode::POP));
        emitLoop(loopStart);
        patchJump(exitJump);
    } else if (std::holds_alternative<ForStatement>(stmt)) {
        const auto& forStmt = std::get<ForStatement>(stmt);
        compile(forStmt.collection);
        Value zero((double)0);
        emitConstant(zero);
        int loopStart = (int)currentFunction->chunk.code.size();
        int exitJump = emitJump(OpCode::FOR_ITER);
        if (forStmt.index != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(forStmt.index));
            emitByte(static_cast<uint8_t>(OpCode::POP));
        } else {
            auto namePtr = std::make_shared<std::string>(forStmt.variableName);
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
            emitByte(0); // Loop variables are immutable in this context
        }
        compile(forStmt.body);
        emitByte(static_cast<uint8_t>(OpCode::POP));
        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(static_cast<uint8_t>(OpCode::POP));
        emitByte(static_cast<uint8_t>(OpCode::POP));
    } else if (std::holds_alternative<std::shared_ptr<Expression>>(stmt)) {
        compile(std::get<std::shared_ptr<Expression>>(stmt));
        emitByte(static_cast<uint8_t>(OpCode::POP));
    } else if (std::holds_alternative<ClassDeclaration>(stmt)) {
        const auto& decl = std::get<ClassDeclaration>(stmt);
        
        // Constant for class name
        auto namePtr = std::make_shared<std::string>(decl.name);
        Value::registerString(namePtr);
        currentFunction->chunk.constants.push_back(Value(namePtr.get()));
        uint8_t nameIdx = (uint8_t)(currentFunction->chunk.constants.size() - 1);

        // Push properties data to stack
        for (const auto& prop : decl.properties) {
            auto propNamePtr = std::make_shared<std::string>(prop.name);
            Value::registerString(propNamePtr);
            emitConstant(Value(propNamePtr.get()));
            
            double meta = (prop.isReadonly ? 1.0 : 0.0) + (prop.isPrivate ? 2.0 : 0.0);
            emitConstant(Value(meta));
            
            if (prop.initializer) {
                compile(prop.initializer);
            } else {
                emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
            }
        }
        
        // Push methods data to stack
        for (const auto& method : decl.methods) {
            auto methodNamePtr = std::make_shared<std::string>(method.name);
            Value::registerString(methodNamePtr);
            emitConstant(Value(methodNamePtr.get()));
            
            auto prevFunc = currentFunction;
            auto nestedFunc = std::make_shared<VMFunction>(method.name, (int)method.parameters.size() + 1, method.localCount + 1);
            nestedFunc->hasRest = method.hasRest;
            allFunctions.push_back(nestedFunc);
            
            currentFunction = nestedFunc;
            compile(method.body);
            emitByte(static_cast<uint8_t>(OpCode::RETURN));
            currentFunction = prevFunc;
            
            emitConstant(Value(nestedFunc.get()));
        }
        
        emitBytes(static_cast<uint8_t>(OpCode::CLASS), nameIdx);
        emitByte(decl.isAbstract ? 1 : 0);
        emitByte((uint8_t)decl.properties.size());
        emitByte((uint8_t)decl.methods.size());
        
        emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), nameIdx);
        emitByte(0); // Classes are immutable globals
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
    } else {
        // ...
    }
}

void Compiler::compile(std::shared_ptr<Expression> expr) {
    if (!expr) return;
    if (expr->line != -1) currentLine = expr->line;

    if (auto e = dynamic_cast<LiteralExpression*>(expr.get())) {
        if (e->value.isNil()) emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        else if (e->value.isBoolean()) {
            emitByte(static_cast<uint8_t>(e->value.asBoolean() ? OpCode::TRUE_VAL : OpCode::FALSE_VAL));
        } else if (e->value.isString()) {
             auto namePtr = std::make_shared<std::string>(*e->value.asString());
             Value::registerString(namePtr);
             emitConstant(Value(namePtr.get()));
        } else {
            emitConstant(e->value);
        }
    } else if (auto e = dynamic_cast<BigIntLiteralExpression*>(expr.get())) {
        int64_t val = std::stoll(e->value);
        auto ptr = std::make_shared<int64_t>(val);
        Value::registerBigInt(ptr);
        emitConstant(Value(ptr.get()));
    } else if (auto e = dynamic_cast<IdentifierExpression*>(expr.get())) {
        if (e->depth != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::GET_LOCAL), static_cast<uint8_t>(e->index));
        } else {
            auto namePtr = std::make_shared<std::string>(e->name);
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::GET_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
        }
    } else if (auto e = dynamic_cast<ThisExpression*>(expr.get())) {
        if (e->depth != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::GET_LOCAL), static_cast<uint8_t>(e->index));
        } else {
            auto namePtr = std::make_shared<std::string>("this");
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::GET_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
        }
    } else if (auto e = dynamic_cast<AssignmentExpression*>(expr.get())) {
        compile(e->value);
        if (e->index != -1) {
            // local assignment
            emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(e->index));
        } else {
            // global assignment
            auto namePtr = std::make_shared<std::string>(e->name);
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::SET_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
        }
    } else if (dynamic_cast<ConsoleArgsExpression*>(expr.get())) {
        emitByte(static_cast<uint8_t>(OpCode::CONSOLE_ARGS));
    } else if (dynamic_cast<ConsoleReadExpression*>(expr.get())) {
        emitByte(static_cast<uint8_t>(OpCode::CONSOLE_READ));
    } else if (auto e = dynamic_cast<ConsoleShowExpression*>(expr.get())) {
        for (const auto& arg : e->arguments) compile(arg);
        emitBytes(static_cast<uint8_t>(OpCode::PRINT), (uint8_t)e->arguments.size());
    } else if (auto e = dynamic_cast<ConsoleWarnExpression*>(expr.get())) {
        for (const auto& arg : e->arguments) compile(arg);
        emitBytes(static_cast<uint8_t>(OpCode::WARN), (uint8_t)e->arguments.size());
    } else if (auto e = dynamic_cast<ProcessRunExpression*>(expr.get())) {
        compile(e->command);
        uint8_t count = 0;
        for (const auto& arg : e->arguments) {
            compile(arg);
            count++;
        }
        emitBytes(static_cast<uint8_t>(OpCode::PROCESS_RUN), count);
    } else if (auto e = dynamic_cast<ProcessSpawnExpression*>(expr.get())) {
        compile(e->command);
        uint8_t count = 0;
        for (const auto& arg : e->arguments) {
            compile(arg);
            count++;
        }
        emitBytes(static_cast<uint8_t>(OpCode::PROCESS_SPAWN), count);
    } else if (auto e = dynamic_cast<ProcessSleepExpression*>(expr.get())) {
        compile(e->delay);
        emitByte(static_cast<uint8_t>(OpCode::PROCESS_SLEEP));
    } else if (auto e = dynamic_cast<FSCreateExpression*>(expr.get())) {
        compile(e->path);
        compile(e->content);
        emitByte(static_cast<uint8_t>(OpCode::FS_CREATE));
    } else if (auto e = dynamic_cast<FSOverwriteExpression*>(expr.get())) {
        compile(e->path);
        compile(e->content);
        if (e->options) compile(e->options);
        else emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        emitByte(static_cast<uint8_t>(OpCode::FS_OVERWRITE));
    } else if (auto e = dynamic_cast<FSAppendExpression*>(expr.get())) {
        compile(e->path);
        compile(e->content);
        if (e->options) compile(e->options);
        else emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        emitByte(static_cast<uint8_t>(OpCode::FS_APPEND));
    } else if (auto e = dynamic_cast<FSDeleteExpression*>(expr.get())) {
        compile(e->path);
        emitByte(static_cast<uint8_t>(OpCode::FS_DELETE));
    } else if (auto e = dynamic_cast<FSReadExpression*>(expr.get())) {
        compile(e->path);
        emitByte(static_cast<uint8_t>(OpCode::FS_READ));
    } else if (auto e = dynamic_cast<FSReadJsonExpression*>(expr.get())) {
        compile(e->path);
        emitByte(static_cast<uint8_t>(OpCode::FS_READ_JSON));
    } else if (auto e = dynamic_cast<FSWriteJsonExpression*>(expr.get())) {
        compile(e->path);
        compile(e->value);
        emitByte(static_cast<uint8_t>(OpCode::FS_WRITE_JSON));
    } else if (auto e = dynamic_cast<FSInfoExpression*>(expr.get())) {
        compile(e->path);
        emitByte(static_cast<uint8_t>(OpCode::FS_INFO));
    } else if (auto e = dynamic_cast<TrimExpression*>(expr.get())) {
        compile(e->argument);
        emitByte(static_cast<uint8_t>(OpCode::TRIM));
    } else if (auto e = dynamic_cast<StringSizeExpression*>(expr.get())) {
        compile(e->argument);
        emitByte(static_cast<uint8_t>(OpCode::STRING_SIZE));
    } else if (auto e = dynamic_cast<MemberExpression*>(expr.get())) {
        if (auto objId = dynamic_cast<IdentifierExpression*>(e->object.get())) {
            // Check if this identifier refers to an imported module (by bind name or alias)
            auto it = importedModules.find(objId->name);
            if (it != importedModules.end()) {
                // Flatten: bindName.prop -> stored as bindName.prop in globals
                std::string fullName = objId->name + "." + e->property;
                auto namePtr = std::make_shared<std::string>(fullName);
                Value::registerString(namePtr);
                currentFunction->chunk.constants.push_back(Value(namePtr.get()));
                emitBytes(static_cast<uint8_t>(OpCode::GET_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
                return;
            }
        }
        
        // Not a module access, compile as dynamic property access on an object
        compile(e->object); // Will push the object to the stack
        auto namePtr = std::make_shared<std::string>(e->property);
        Value::registerString(namePtr);
        currentFunction->chunk.constants.push_back(Value(namePtr.get()));
        emitBytes(static_cast<uint8_t>(OpCode::GET_PROPERTY), (uint8_t)(currentFunction->chunk.constants.size() - 1));

    } else if (auto e = dynamic_cast<BinaryExpression*>(expr.get())) {
        if (e->op == "&&") {
            compile(e->left);
            int jump = emitJump(OpCode::JUMP_IF_FALSE);
            compile(e->right);
            int endJump = emitJump(OpCode::JUMP);
            patchJump(jump);
            emitByte(static_cast<uint8_t>(OpCode::FALSE_VAL));
            patchJump(endJump);
        } else if (e->op == "||") {
            compile(e->left);
            emitByte(static_cast<uint8_t>(OpCode::NOT));
            int jump = emitJump(OpCode::JUMP_IF_FALSE);
            compile(e->right);
            int endJump = emitJump(OpCode::JUMP);
            patchJump(jump);
            emitByte(static_cast<uint8_t>(OpCode::TRUE_VAL));
            patchJump(endJump);
        } else if (e->op == "|>") {
            if (auto callExpr = dynamic_cast<CallExpression*>(e->right.get())) {
                compile(callExpr->callee);
                
                bool hasSpread = false;
                for (const auto& arg : callExpr->arguments) if (dynamic_cast<SpreadExpression*>(arg.get())) hasSpread = true;
                
                bool foundPlaceholder = false;
                for (const auto& arg : callExpr->arguments) {
                    if (auto idExpr = dynamic_cast<IdentifierExpression*>(arg.get())) {
                        if (idExpr->name == "_") foundPlaceholder = true;
                    }
                }
                
                if (hasSpread) {
                    emitBytes(static_cast<uint8_t>(OpCode::MAKE_ARRAY), 0);
                    if (!foundPlaceholder) {
                        compile(e->left);
                        emitByte(static_cast<uint8_t>(OpCode::APPEND_ELEMENT));
                    }
                    for (const auto& arg : callExpr->arguments) {
                        bool isPlaceholder = false;
                        if (auto idExpr = dynamic_cast<IdentifierExpression*>(arg.get())) {
                            if (idExpr->name == "_") {
                                compile(e->left);
                                emitByte(static_cast<uint8_t>(OpCode::APPEND_ELEMENT));
                                isPlaceholder = true;
                            }
                        }
                        if (!isPlaceholder) {
                            if (auto s = dynamic_cast<SpreadExpression*>(arg.get())) {
                                compile(s->expr);
                                emitByte(static_cast<uint8_t>(OpCode::APPEND_ARRAY));
                            } else {
                                compile(arg);
                                emitByte(static_cast<uint8_t>(OpCode::APPEND_ELEMENT));
                            }
                        }
                    }
                    emitByte(static_cast<uint8_t>(OpCode::SPREAD_ARGS));
                } else {
                    if (!foundPlaceholder) {
                        compile(e->left);
                        for (const auto& arg : callExpr->arguments) {
                            compile(arg);
                        }
                        emitBytes(static_cast<uint8_t>(OpCode::CALL), (uint8_t)(callExpr->arguments.size() + 1));
                    } else {
                        for (const auto& arg : callExpr->arguments) {
                            bool isPlaceholder = false;
                            if (auto idExpr = dynamic_cast<IdentifierExpression*>(arg.get())) {
                                if (idExpr->name == "_") {
                                    compile(e->left);
                                    isPlaceholder = true;
                                }
                            }
                            if (!isPlaceholder) {
                                compile(arg);
                            }
                        }
                        emitBytes(static_cast<uint8_t>(OpCode::CALL), (uint8_t)callExpr->arguments.size());
                    }
                }
            } else {
                // If right-side uses _, evaluate it. Otherwise, call it with left-side.
                // We'll use a simple heuristic: if it's a MemberExpression on _ or contains _, evaluate it.
                // For now, let's just use the SET_LOCAL approach if placeholderIndex is valid.
                
                if (e->placeholderIndex != -1) {
                    compile(e->left);
                    emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), (uint8_t)e->placeholderIndex);
                    emitByte(static_cast<uint8_t>(OpCode::POP));
                    compile(e->right);
                } else {
                    compile(e->right);
                    compile(e->left);
                    emitBytes(static_cast<uint8_t>(OpCode::CALL), 1);
                }
            }
        } else {
            compile(e->left);
            compile(e->right);
            if (e->op == "+") emitByte(static_cast<uint8_t>(OpCode::ADD));
            else if (e->op == "-") emitByte(static_cast<uint8_t>(OpCode::SUBTRACT));
            else if (e->op == "*") emitByte(static_cast<uint8_t>(OpCode::MULTIPLY));
            else if (e->op == "/") emitByte(static_cast<uint8_t>(OpCode::DIVIDE));
            else if (e->op == "%") emitByte(static_cast<uint8_t>(OpCode::MODULO));
            else if (e->op == "==") emitByte(static_cast<uint8_t>(OpCode::EQUAL));
            else if (e->op == "!=") {
                emitByte(static_cast<uint8_t>(OpCode::EQUAL));
                emitByte(static_cast<uint8_t>(OpCode::NOT));
            }
            else if (e->op == "<") emitByte(static_cast<uint8_t>(OpCode::LESS));
            else if (e->op == "<=") emitByte(static_cast<uint8_t>(OpCode::LESS_EQUAL));
            else if (e->op == ">") emitByte(static_cast<uint8_t>(OpCode::GREATER));
            else if (e->op == ">=") emitByte(static_cast<uint8_t>(OpCode::GREATER_EQUAL));
        }
    } else if (auto e = dynamic_cast<CallExpression*>(expr.get())) {
        compile(e->callee);
        bool hasSpread = false;
        for (const auto& arg : e->arguments) if (dynamic_cast<SpreadExpression*>(arg.get())) hasSpread = true;
        
        if (hasSpread) {
            emitBytes(static_cast<uint8_t>(OpCode::MAKE_ARRAY), 0);
            for (const auto& arg : e->arguments) {
                if (auto s = dynamic_cast<SpreadExpression*>(arg.get())) {
                    compile(s->expr);
                    emitByte(static_cast<uint8_t>(OpCode::APPEND_ARRAY));
                } else {
                    compile(arg);
                    emitByte(static_cast<uint8_t>(OpCode::APPEND_ELEMENT));
                }
            }
            emitByte(static_cast<uint8_t>(OpCode::SPREAD_ARGS));
        } else {
            for (const auto& arg : e->arguments) {
                compile(arg);
            }
            emitBytes(static_cast<uint8_t>(OpCode::CALL), (uint8_t)e->arguments.size());
        }
    } else if (auto e = dynamic_cast<IfExpression*>(expr.get())) {
        compile(e->condition);
        int thenJump = emitJump(OpCode::JUMP_IF_FALSE);
        compile(e->thenBranch);
        int elseJump = emitJump(OpCode::JUMP);
        patchJump(thenJump);
        if (e->elseBranch) compile(e->elseBranch);
        else emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        patchJump(elseJump);
    } else if (auto e = dynamic_cast<ObjectExpression*>(expr.get())) {
        bool hasSpread = false;
        for (const auto& p : e->properties) if (dynamic_cast<SpreadExpression*>(p.second.get())) hasSpread = true;
        
        if (hasSpread) {
            emitBytes(static_cast<uint8_t>(OpCode::MAKE_OBJECT), 0);
            for (const auto& p : e->properties) {
                if (auto s = dynamic_cast<SpreadExpression*>(p.second.get())) {
                    compile(s->expr);
                    emitByte(static_cast<uint8_t>(OpCode::MERGE_OBJECT));
                } else {
                    auto namePtr = std::make_shared<std::string>(p.first);
                    Value::registerString(namePtr);
                    emitConstant(Value(namePtr.get()));
                    compile(p.second);
                    emitByte(static_cast<uint8_t>(OpCode::MERGE_PROPERTY));
                }
            }
        } else {
            for (const auto& prop : e->properties) {
                auto namePtr = std::make_shared<std::string>(prop.first);
                Value::registerString(namePtr);
                emitConstant(Value(namePtr.get()));
                compile(prop.second);
            }
            emitBytes(static_cast<uint8_t>(OpCode::MAKE_OBJECT), (uint8_t)e->properties.size());
        }
    } else if (auto e = dynamic_cast<ArrayExpression*>(expr.get())) {
        bool hasSpread = false;
        for (const auto& el : e->elements) if (dynamic_cast<SpreadExpression*>(el.get())) hasSpread = true;
        if (hasSpread) {
            emitBytes(static_cast<uint8_t>(OpCode::MAKE_ARRAY), 0);
            for (const auto& el : e->elements) {
                if (auto s = dynamic_cast<SpreadExpression*>(el.get())) {
                    compile(s->expr);
                    emitByte(static_cast<uint8_t>(OpCode::APPEND_ARRAY));
                } else {
                    compile(el);
                    emitByte(static_cast<uint8_t>(OpCode::APPEND_ELEMENT));
                }
            }
        } else {
            for (const auto& el : e->elements) {
                compile(el);
            }
            emitBytes(static_cast<uint8_t>(OpCode::MAKE_ARRAY), (uint8_t)e->elements.size());
        }
    } else if (auto e = dynamic_cast<IndexExpression*>(expr.get())) {
        compile(e->object);
        compile(e->index);
        emitByte(static_cast<uint8_t>(OpCode::GET_ELEMENT));
    } else if (auto e = dynamic_cast<MatchExpression*>(expr.get())) {
        compile(e->valueToMatch);
        std::vector<int> endJumps;
        for (const auto& case_ : e->cases) {
            if (case_.pattern) {
                emitByte(static_cast<uint8_t>(OpCode::DUP));
                compile(case_.pattern);
                emitByte(static_cast<uint8_t>(OpCode::EQUAL));
                int jumpIfFalse = emitJump(OpCode::JUMP_IF_FALSE);
                emitByte(static_cast<uint8_t>(OpCode::POP)); // Pop the target
                compile(case_.body);
                endJumps.push_back(emitJump(OpCode::JUMP));
                patchJump(jumpIfFalse);
            } else {
                emitByte(static_cast<uint8_t>(OpCode::POP)); // Pop the target
                compile(case_.body);
                endJumps.push_back(emitJump(OpCode::JUMP));
            }
        }
        emitByte(static_cast<uint8_t>(OpCode::POP));
        emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        for (int jump : endJumps) {
            patchJump(jump);
        }
    } else if (auto e = dynamic_cast<UnaryExpression*>(expr.get())) {
        if (e->op == "!") {
            compile(e->expr);
            emitByte(static_cast<uint8_t>(OpCode::NOT));
        } else if (e->op == "-") {
            // Unary minus: 0 - expr
            emitConstant(Value(0.0));
            compile(e->expr);
            emitByte(static_cast<uint8_t>(OpCode::SUBTRACT));
        }
    } else if (auto e = dynamic_cast<TypeofExpression*>(expr.get())) {
        compile(e->expr);
        emitByte(static_cast<uint8_t>(OpCode::TYPEOF));
    } else if (auto e = dynamic_cast<BlockExpression*>(expr.get())) {
        if (e->statements.empty()) {
            emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
        } else {
            for (size_t i = 0; i < e->statements.size(); i++) {
                const auto& stmt = e->statements[i];
                if (i == e->statements.size() - 1 && std::holds_alternative<std::shared_ptr<Expression>>(stmt)) {
                    compile(std::get<std::shared_ptr<Expression>>(stmt));
                } else {
                    compile(stmt);
                }
            }
            if (!std::holds_alternative<std::shared_ptr<Expression>>(e->statements.back())) {
                emitByte(static_cast<uint8_t>(OpCode::NULL_VAL));
            }
        }
    } else if (auto e = dynamic_cast<MemberAssignmentExpression*>(expr.get())) {
        compile(e->object);
        compile(e->value);
        auto namePtr = std::make_shared<std::string>(e->property);
        Value::registerString(namePtr);
        currentFunction->chunk.constants.push_back(Value(namePtr.get()));
        emitBytes(static_cast<uint8_t>(OpCode::SET_PROPERTY), (uint8_t)(currentFunction->chunk.constants.size() - 1));
    } else if (auto e = dynamic_cast<IndexAssignmentExpression*>(expr.get())) {
        compile(e->object);
        compile(e->index);
        compile(e->value);
        emitByte(static_cast<uint8_t>(OpCode::SET_ELEMENT));
    } else if (auto e = dynamic_cast<LambdaExpression*>(expr.get())) {
        auto prevFunc = currentFunction;
        auto nestedFunc = std::make_shared<VMFunction>("lambda", (int)e->parameters.size(), e->localCount);
        nestedFunc->hasRest = e->hasRest;
        allFunctions.push_back(nestedFunc);
        
        currentFunction = nestedFunc;
        // Emit type checks for parameters
        for (size_t i = 0; i < e->parameters.size(); ++i) {
            if (e->parameters[i].second.isPresent) {
                emitBytes(static_cast<uint8_t>(OpCode::GET_LOCAL), static_cast<uint8_t>(i));
                std::string typeStr = e->parameters[i].second.toString();
                auto strPtr = std::make_shared<std::string>(typeStr);
                Value::registerString(strPtr);
                currentFunction->chunk.constants.push_back(Value(strPtr.get()));
                emitBytes(static_cast<uint8_t>(OpCode::CHECK_TYPE), (uint8_t)(currentFunction->chunk.constants.size() - 1));
                emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(i));
                emitByte(static_cast<uint8_t>(OpCode::POP));
            }
        }
        compile(e->body);
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
        
        currentFunction = prevFunc;
        emitConstant(Value(nestedFunc.get()));
    } else if (auto e = dynamic_cast<AsyncExpression*>(expr.get())) {
        // Compile the body into a nested VMFunction, then emit ASYNC opcode
        auto prevFunc = currentFunction;
        auto nestedFunc = std::make_shared<VMFunction>("async_body", 0, 0);
        allFunctions.push_back(nestedFunc);
        currentFunction = nestedFunc;
        compile(e->body);
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
        currentFunction = prevFunc;
        emitConstant(Value(nestedFunc.get()));
        emitByte(static_cast<uint8_t>(OpCode::ASYNC));
    } else if (auto e = dynamic_cast<LayoutExpression*>(expr.get())) {
        std::string typeStr = "{";
        for (size_t i = 0; i < e->properties.size(); ++i) {
            typeStr += e->properties[i].name + ":" + e->properties[i].type.toString();
            if (i < e->properties.size() - 1) typeStr += ",";
        }
        typeStr += "}";
        auto strPtr = std::make_shared<std::string>(typeStr);
        Value::registerString(strPtr);
        emitConstant(Value(strPtr.get()));
    } else if (auto e = dynamic_cast<AfterExpression*>(expr.get())) {
        compile(e->delay);
        auto prevFunc = currentFunction;
        auto nestedFunc = std::make_shared<VMFunction>("after_body", 0, 0);
        allFunctions.push_back(nestedFunc);
        currentFunction = nestedFunc;
        compile(e->body);
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
        currentFunction = prevFunc;
        emitConstant(Value(nestedFunc.get()));
        emitByte(static_cast<uint8_t>(OpCode::AFTER));
    } else if (auto e = dynamic_cast<EveryExpression*>(expr.get())) {
        compile(e->delay);
        auto prevFunc = currentFunction;
        auto nestedFunc = std::make_shared<VMFunction>("every_body", 0, 0);
        allFunctions.push_back(nestedFunc);
        currentFunction = nestedFunc;
        compile(e->body);
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
        currentFunction = prevFunc;
        emitConstant(Value(nestedFunc.get()));
        emitByte(static_cast<uint8_t>(OpCode::EVERY));
    } else if (auto e = dynamic_cast<AsExpression*>(expr.get())) {
        compile(e->left);
    }
}

void Compiler::emitByte(uint8_t byte) {
    currentFunction->chunk.code.push_back(byte);
    if (currentLine != -1) {
        if (currentFunction->chunk.lineInfo.empty() || 
            currentFunction->chunk.lineInfo.back().line != currentLine) {
            currentFunction->chunk.lineInfo.push_back({currentFunction->chunk.code.size() - 1, currentLine});
        }
    }
}

void Compiler::emitBytes(uint8_t b1, uint8_t b2) {
    emitByte(b1);
    emitByte(b2);
}

void Compiler::emitConstant(Value value) {
    currentFunction->chunk.constants.push_back(value);
    emitBytes(static_cast<uint8_t>(OpCode::CONSTANT), (uint8_t)(currentFunction->chunk.constants.size() - 1));
}

int Compiler::emitJump(OpCode instruction) {
    emitByte(static_cast<uint8_t>(instruction));
    emitByte(0xff);
    emitByte(0xff);
    return (int)currentFunction->chunk.code.size() - 2;
}

void Compiler::emitLoop(int loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::LOOP));
    int offset = (int)currentFunction->chunk.code.size() - loopStart + 2;
    if (offset > 0xffff) throw std::runtime_error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

void Compiler::patchJump(int offset) {
    int jump = (int)currentFunction->chunk.code.size() - offset - 2;
    if (jump > 0xffff) throw std::runtime_error("Too much code to jump over.");
    currentFunction->chunk.code[offset] = (jump >> 8) & 0xff;
    currentFunction->chunk.code[offset + 1] = jump & 0xff;
}
void Compiler::compileDestructuring(const std::vector<DestructuringBinding>& bindings, bool isMutable) {
    // Collect keys for rest object destructuring in this level
    std::vector<std::string> excludedKeys;
    for (const auto& binding : bindings) {
        if (!binding.isRest && binding.arrayIndex == -1) {
            excludedKeys.push_back(binding.propertyName);
        }
    }

    for (const auto& binding : bindings) {
        emitByte(static_cast<uint8_t>(OpCode::DUP));
        if (binding.isRest) {
            if (binding.arrayIndex != -1) {
                // Array rest
                emitConstant(Value((double)binding.arrayIndex));
                emitByte(static_cast<uint8_t>(OpCode::SLICE_ARRAY));
            } else {
                // Object rest
                emitByte(static_cast<uint8_t>(OpCode::REST_OBJECT));
                emitByte((uint8_t)excludedKeys.size());
                for (const auto& key : excludedKeys) {
                    auto namePtr = std::make_shared<std::string>(key);
                    Value::registerString(namePtr);
                    currentFunction->chunk.constants.push_back(Value(namePtr.get()));
                    emitByte((uint8_t)(currentFunction->chunk.constants.size() - 1));
                }
            }
        } else if (binding.arrayIndex != -1) {
            emitConstant(Value((double)binding.arrayIndex));
            emitByte(static_cast<uint8_t>(OpCode::GET_ELEMENT));
        } else {
            auto namePtr = std::make_shared<std::string>(binding.propertyName);
            Value::registerString(namePtr);
            currentFunction->chunk.constants.push_back(Value(namePtr.get()));
            emitBytes(static_cast<uint8_t>(OpCode::GET_PROPERTY), (uint8_t)(currentFunction->chunk.constants.size() - 1));
        }

        if (!binding.nested.empty()) {
            compileDestructuring(binding.nested, isMutable);
        } else if (!binding.name.empty()) {
            // Leaf binding
            if (binding.index != -1) {
                emitBytes(static_cast<uint8_t>(OpCode::SET_LOCAL), static_cast<uint8_t>(binding.index));
            } else {
                auto namePtr = std::make_shared<std::string>(binding.name);
                Value::registerString(namePtr);
                currentFunction->chunk.constants.push_back(Value(namePtr.get()));
                emitByte(static_cast<uint8_t>(OpCode::DUP));
                emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), (uint8_t)(currentFunction->chunk.constants.size() - 1));
                emitByte(isMutable ? 1 : 0);
            }
        }
        emitByte(static_cast<uint8_t>(OpCode::POP)); // Pop the property/element/rest result
    }
}
