#ifndef RESOLVER_H
#define RESOLVER_H

#include "ast.h"
#include <vector>
#include <map>
#include <string>

class Resolver {
public:
    void resolve(std::vector<Statement>& statements);
    void resolveModule(std::vector<Statement>& statements);
    int mainLocalCount = 0;

private:
    void resolve(Statement& stmt);
    void resolve(std::shared_ptr<Expression> expr);
    void resolveDestructuring(std::vector<DestructuringBinding>& bindings, bool isMutable);
    
    void beginScope();
    void endScope();
    void declare(const std::string& name, bool isMutable = false);
    void define(const std::string& name);
    void resolveLocal(IdentifierExpression& expr, const std::string& name);
    void resolveLocal(ThisExpression& expr, const std::string& name);
    void resolveAssignment(AssignmentExpression& expr, const std::string& name);

    struct Scope {
        std::map<std::string, int> variables;
        std::map<std::string, bool> mutability; // true = mutable (var)
        int nextIndex = 0;
    };
    std::vector<Scope> scopes;
    std::map<std::string, bool> globalMutability; // Track mutability for globals
    
    struct FunctionInfo {
        int localCount = 0;
    };
    std::vector<FunctionInfo> functionStack;
    std::vector<int> placeholderUsageStack;
};

#endif
