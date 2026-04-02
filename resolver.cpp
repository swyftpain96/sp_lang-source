#include "resolver.h"
#include <stdexcept>

void Resolver::resolve(std::vector<Statement>& statements) {
    for (auto& stmt : statements) {
        resolve(stmt);
    }
}

void Resolver::resolveModule(std::vector<Statement>& statements) {
    // Top-level variables at the module level are handled as main locals 
    // to allow closures to find them in the VM without dedicated upvalues.
    // We DON'T start a scope here so that top-level decls go to globalMutability.
    resolve(statements);
}

void Resolver::resolve(Statement& stmt) {
    if (std::holds_alternative<VariableDeclaration>(stmt)) {
        auto& decl = const_cast<VariableDeclaration&>(std::get<VariableDeclaration>(stmt));
        resolve(decl.value);
        if (scopes.size() > 0) {
            declare(decl.name, decl.isMutable);
            define(decl.name);
            decl.index = scopes.back().variables[decl.name];
        } else {
            declare(decl.name, decl.isMutable);
            define(decl.name);
            decl.index = -1; // Global
        }
    } else if (std::holds_alternative<DestructuringDeclaration>(stmt)) {
        auto& decl = const_cast<DestructuringDeclaration&>(std::get<DestructuringDeclaration>(stmt));
        resolve(decl.initializer);
        resolveDestructuring(decl.bindings, decl.isMutable);
    } else if (std::holds_alternative<FunctionDeclaration>(stmt)) {
        auto& decl = std::get<FunctionDeclaration>(stmt);
        declare(decl.name);
        define(decl.name);
        if (!scopes.empty()) {
            decl.index = scopes.back().variables[decl.name];
        }

        functionStack.push_back({0});
        beginScope();
        scopes.back().nextIndex = 0; // Reset for function
        for (const auto& param : decl.parameters) {
            declare(param);
            define(param);
        }
        resolve(decl.body);
        endScope();
        decl.localCount = functionStack.back().localCount;
        functionStack.pop_back();
    } else if (std::holds_alternative<PrintStatement>(stmt)) {
        auto& print = std::get<PrintStatement>(stmt);
        for (auto& expr : print.exprs) resolve(expr);
    } else if (std::holds_alternative<ReturnStatement>(stmt)) {
        auto& ret = std::get<ReturnStatement>(stmt);
        if (ret.value) resolve(ret.value);
    } else if (std::holds_alternative<std::shared_ptr<Expression>>(stmt)) {
        resolve(std::get<std::shared_ptr<Expression>>(stmt));
    } else if (std::holds_alternative<WhileStatement>(stmt)) {
        auto& whileStmt = std::get<WhileStatement>(stmt);
        resolve(whileStmt.condition);
        resolve(whileStmt.body);
    } else if (std::holds_alternative<ForStatement>(stmt)) {
        auto& forStmt = const_cast<ForStatement&>(std::get<ForStatement>(stmt));
        resolve(forStmt.collection);
        beginScope();
        declare(forStmt.variableName);
        define(forStmt.variableName);
        if (!scopes.empty()) {
            forStmt.index = scopes.back().variables[forStmt.variableName];
        } else {
            forStmt.index = -1;
        }
        int scopeStart = scopes.back().nextIndex;
        resolve(forStmt.body);
        forStmt.localCount = scopes.back().nextIndex - scopeStart;
        endScope();
    } else if (std::holds_alternative<ExportStatement>(stmt)) {
        auto& exp = std::get<ExportStatement>(stmt);
        if (std::holds_alternative<VariableDeclaration>(exp.declaration)) {
            resolve(std::get<VariableDeclaration>(exp.declaration).value);
        } else if (std::holds_alternative<DestructuringDeclaration>(exp.declaration)) {
            auto& decl = const_cast<DestructuringDeclaration&>(std::get<DestructuringDeclaration>(exp.declaration));
            resolve(decl.initializer);
            resolveDestructuring(decl.bindings, decl.isMutable);
        } else if (std::holds_alternative<FunctionDeclaration>(exp.declaration)) {
            auto& decl = std::get<FunctionDeclaration>(exp.declaration);
            functionStack.push_back({0});
            beginScope();
            for (const auto& param : decl.parameters) {
                declare(param);
                define(param);
            }
            resolve(decl.body);
            endScope();
            decl.localCount = functionStack.back().localCount;
            functionStack.pop_back();
        }
    } else if (std::holds_alternative<ClassDeclaration>(stmt)) {
        auto& decl = std::get<ClassDeclaration>(stmt);
        declare(decl.name);
        define(decl.name);
        
        // Push a special class scope? Or just resolve members.
        // For methods, we need to ensure 'this' is available.
        for (auto& prop : decl.properties) {
            if (prop.initializer) {
                resolve(prop.initializer);
                if (auto lit = dynamic_cast<LiteralExpression*>(prop.initializer.get())) {
                    prop.initializer_value = lit->value;
                }
            }
        }
        for (auto& method : decl.methods) {
            functionStack.push_back({0});
            beginScope();
            // Declare 'this' in method scope
            declare("this");
            define("this");
            
            for (const auto& param : method.parameters) {
                declare(param);
                define(param);
            }
            resolve(method.body);
            endScope();
            method.localCount = functionStack.back().localCount;
            functionStack.pop_back();
        }
    }
}
void Resolver::resolve(std::shared_ptr<Expression> expr) {
    if (dynamic_cast<LiteralExpression*>(expr.get())) {
        // Nothing to resolve
    } else if (auto e = dynamic_cast<IdentifierExpression*>(expr.get())) {
        resolveLocal(*e, e->name);
    } else if (auto e = dynamic_cast<MemberExpression*>(expr.get())) {
        resolve(e->object);
    } else if (auto e = dynamic_cast<IndexExpression*>(expr.get())) {
        resolve(e->object);
        resolve(e->index);
    } else if (auto e = dynamic_cast<BinaryExpression*>(expr.get())) {
        resolve(e->left);
        if (e->op == "|>") {
            beginScope();
            declare("_");
            define("_");
            
            // Track if _ is used in the right-side expression
            placeholderUsageStack.push_back(0);
            
            // Find the index assigned to _ in the current scope
            e->placeholderIndex = scopes.back().variables["_"];
            resolve(e->right);
            
            int usageCount = placeholderUsageStack.back();
            placeholderUsageStack.pop_back();
            
            // If _ wasn't used, we'll treat it as a function call with left-side as first arg.
            // But wait! If it's a CallExpression, the Compiler has its own logic for _ anyway.
            // For non-CallExpressions, if _ wasn't used, the Compiler will use CALL 1.
            if (usageCount == 0) {
                e->placeholderIndex = -1;
            }
            
            endScope();
        } else {
            resolve(e->right);
        }
    } else if (auto e = dynamic_cast<SpreadExpression*>(expr.get())) {
        resolve(e->expr);
    } else if (auto e = dynamic_cast<UnaryExpression*>(expr.get())) {
        resolve(e->expr);
    } else if (auto e = dynamic_cast<ArrayExpression*>(expr.get())) {
        for (auto& el : e->elements) resolve(el);
    } else if (auto e = dynamic_cast<ObjectExpression*>(expr.get())) {
        for (auto& p : e->properties) resolve(p.second);
    } else if (auto e = dynamic_cast<IfExpression*>(expr.get())) {
        resolve(e->condition);
        resolve(e->thenBranch);
        if (e->elseBranch) resolve(e->elseBranch);
    } else if (auto e = dynamic_cast<MatchExpression*>(expr.get())) {
        resolve(e->valueToMatch);
        for (auto& c : e->cases) {
            if (c.pattern) resolve(c.pattern);
            resolve(c.body);
        }
    } else if (auto e = dynamic_cast<CallExpression*>(expr.get())) {
        resolve(e->callee);
        for (auto& arg : e->arguments) resolve(arg);
    } else if (auto e = dynamic_cast<AssignmentExpression*>(expr.get())) {
        resolve(e->value);
        resolveAssignment(*e, e->name);
    } else if (auto e = dynamic_cast<MemberAssignmentExpression*>(expr.get())) {
        resolve(e->object);
        resolve(e->value);
    } else if (auto e = dynamic_cast<ThisExpression*>(expr.get())) {
        resolveLocal(*e, "this");
    } else if (auto e = dynamic_cast<BlockExpression*>(expr.get())) {
        beginScope();
        int scopeStart = scopes.back().nextIndex;
        resolve(e->statements);
        e->localCount = scopes.back().nextIndex - scopeStart;
        endScope();
    } else if (auto e = dynamic_cast<ConsoleArgsExpression*>(expr.get())) {
        if (e->schema) resolve(e->schema);
    } else if (auto e = dynamic_cast<ConsoleReadExpression*>(expr.get())) {
        if (e->options) resolve(e->options);
    } else if (auto e = dynamic_cast<ConsoleShowExpression*>(expr.get())) {
        for (auto& arg : e->arguments) resolve(arg);
    } else if (auto e = dynamic_cast<ConsoleWarnExpression*>(expr.get())) {
        for (auto& arg : e->arguments) resolve(arg);
    } else if (auto e = dynamic_cast<ProcessRunExpression*>(expr.get())) {
        resolve(e->command);
        for (auto& arg : e->arguments) resolve(arg);
    } else if (auto e = dynamic_cast<ProcessSpawnExpression*>(expr.get())) {
        resolve(e->command);
        for (auto& arg : e->arguments) resolve(arg);
    } else if (auto e = dynamic_cast<FSReadExpression*>(expr.get())) {
        resolve(e->path);
    } else if (auto e = dynamic_cast<FSReadJsonExpression*>(expr.get())) {
        resolve(e->path);
    } else if (auto e = dynamic_cast<FSWriteJsonExpression*>(expr.get())) {
        resolve(e->path);
        resolve(e->value);
    } else if (auto e = dynamic_cast<FSInfoExpression*>(expr.get())) {
        resolve(e->path);
    } else if (auto e = dynamic_cast<FSCreateExpression*>(expr.get())) {
        resolve(e->path);
        resolve(e->content);
    } else if (auto e = dynamic_cast<FSOverwriteExpression*>(expr.get())) {
        resolve(e->path);
        resolve(e->content);
        if (e->options) resolve(e->options);
    } else if (auto e = dynamic_cast<FSAppendExpression*>(expr.get())) {
        resolve(e->path);
        resolve(e->content);
        if (e->options) resolve(e->options);
    } else if (auto e = dynamic_cast<FSDeleteExpression*>(expr.get())) {
        resolve(e->path);
    } else if (auto e = dynamic_cast<TrimExpression*>(expr.get())) {
        resolve(e->argument);
    } else if (auto e = dynamic_cast<StringSizeExpression*>(expr.get())) {
        resolve(e->argument);
    } else if (auto e = dynamic_cast<LambdaExpression*>(expr.get())) {
        functionStack.push_back({0});
        beginScope();
        for (const auto& param : e->parameters) {
            declare(param);
            define(param);
        }
        resolve(e->body);
        endScope();
        e->localCount = functionStack.back().localCount;
        functionStack.pop_back();
    }
}

void Resolver::beginScope() {
    int next = 0;
    if (!scopes.empty()) next = scopes.back().nextIndex;
    scopes.push_back({{}, {}, next});
}

void Resolver::endScope() {
    scopes.pop_back();
}

void Resolver::declare(const std::string& name, bool isMutable) {
    if (scopes.empty()) {
        if (globalMutability.find(name) != globalMutability.end()) {
            throw std::runtime_error("Cannot re-declare variable '" + name + "' in the global scope.");
        }
        globalMutability[name] = isMutable;
        return;
    }
    
    // Only check the CURRENT (innermost) scope for re-declaration
    if (scopes.back().variables.find(name) != scopes.back().variables.end()) {
        throw std::runtime_error("Cannot re-declare variable '" + name + "' in the same scope.");
    }
    
    if (!functionStack.empty()) {
        int index = functionStack.back().localCount++;
        scopes.back().variables[name] = index;
        scopes.back().mutability[name] = isMutable;
        scopes.back().nextIndex = functionStack.back().localCount;
    } else {
        // Local to the main chunk (module scope blocks)
        int index = mainLocalCount++;
        scopes.back().variables[name] = index;
        scopes.back().mutability[name] = isMutable;
        scopes.back().nextIndex = mainLocalCount;
    }
}

void Resolver::define(const std::string& name) {
    (void)name;
}

void Resolver::resolveLocal(IdentifierExpression& expr, const std::string& name) {
    if (name == "_" && !placeholderUsageStack.empty()) {
        placeholderUsageStack.back()++;
    }
    
    for (int i = (int)scopes.size() - 1; i >= 0; --i) {
        if (scopes[i].variables.find(name) != scopes[i].variables.end()) {
            int index = scopes[i].variables[name];
            if (index != -1) {
                expr.depth = (int)scopes.size() - 1 - i;
                expr.index = index;
            } else {
                // Global variable found in module scope, leave as global (depth = -1)
            }
            return;
        }
    }
}

void Resolver::resolveLocal(ThisExpression& expr, const std::string& name) {
    for (int i = scopes.size() - 1; i >= 0; --i) {
        if (scopes[i].variables.find(name) != scopes[i].variables.end()) {
            expr.depth = scopes.size() - 1 - i;
            expr.index = scopes[i].variables[name];
            return;
        }
    }
}

void Resolver::resolveAssignment(AssignmentExpression& expr, const std::string& name) {
    for (int i = scopes.size() - 1; i >= 0; --i) {
        auto& scope = scopes[i];
        if (scope.variables.find(name) != scope.variables.end()) {
            // Check mutability
            if (scope.mutability.find(name) != scope.mutability.end()) {
                if (!scope.mutability[name]) {
                    throw std::runtime_error("Cannot reassign immutable variable '" + name + "' (declared with 'set'). Use 'var' for mutable variables.");
                }
            }
            expr.depth = scopes.size() - 1 - i;
            expr.index = scope.variables[name];
            return;
        }
    }
    // Not found in local scopes, check globals
    auto it = globalMutability.find(name);
    if (it != globalMutability.end() && !it->second) {
        throw std::runtime_error("Cannot reassign immutable variable '" + name + "' (declared with 'set'). Use 'var' for mutable variables.");
    }
}
void Resolver::resolveDestructuring(std::vector<DestructuringBinding>& bindings, bool isMutable) {
    for (auto& binding : bindings) {
        if (!binding.nested.empty()) {
            resolveDestructuring(binding.nested, isMutable);
        } else if (!binding.name.empty()) {
            declare(binding.name, isMutable);
            define(binding.name);
            if (!scopes.empty() && !functionStack.empty()) {
                for (int i = scopes.size() - 1; i >= 0; --i) {
                    if (scopes[i].variables.find(binding.name) != scopes[i].variables.end()) {
                        binding.index = scopes[i].variables[binding.name];
                        binding.depth = scopes.size() - 1 - i;
                        break;
                    }
                }
            }
        }
    }
}
