#include "parser.h"
#include <stdexcept>
#include <type_traits>

Statement Parser::addLine(Statement stmt, int line) {
    std::visit([line](auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<Expression>>) {
            if (s) s->line = line;
        } else {
            s.line = line;
        }
    }, stmt);
    return stmt;
}

TypeAnnotation Parser::parseTypeAnnotation() {
    if (check(TokenType::COLON)) {
        advance();
        return parseTypeAnnotationInternal();
    }
    return TypeAnnotation();
}

TypeAnnotation Parser::parseTypeAnnotationInternal() {
    TypeAnnotation type;
    type.isPresent = true;

    if (match(TokenType::LBRACE)) {
        std::string structural = "{ ";
        if (!check(TokenType::RBRACE)) {
            do {
                if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected property name");
                structural += advance().value + ": ";
                TypeAnnotation inner = parseTypeAnnotation();
                structural += inner.toString();
                if (check(TokenType::COMMA)) {
                    advance();
                    structural += ", ";
                }
            } while (!check(TokenType::RBRACE) && !check(TokenType::END));
        }
        if (!match(TokenType::RBRACE)) throw std::runtime_error("Expected } after structural type");
        structural += " }";
        type.typeName = structural;
    } else if (match(TokenType::LPAREN)) {
        std::string funcType = "(";
        if (!check(TokenType::RPAREN)) {
            do {
                if (isIdentifierLike(current().type)) {
                    funcType += advance().value;
                    if (check(TokenType::COLON)) {
                        funcType += ":" + parseTypeAnnotation().toString();
                    }
                } else {
                    funcType += parseTypeAnnotationInternal().toString();
                }
                if (check(TokenType::COMMA)) {
                    advance();
                    funcType += ", ";
                }
            } while (!check(TokenType::RPAREN) && !check(TokenType::END));
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after function parameters");
        funcType += ")";
        if (match(TokenType::ARROW)) {
            funcType += " => " + parseTypeAnnotationInternal().toString();
        }
        type.typeName = funcType;
    } else {
        if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected type name");
        type.typeName = advance().value;
        if (match(TokenType::LESS)) {
            do {
                type.generics.push_back(parseTypeAnnotationInternal());
            } while (match(TokenType::COMMA));
            if (!match(TokenType::GREATER)) throw std::runtime_error("Expected > after generic types");
        }
    }

    if (check(TokenType::BAR)) {
        TypeAnnotation unionType;
        unionType.isPresent = true;
        unionType.unions.push_back(type);
        while (match(TokenType::BAR)) {
            unionType.unions.push_back(parseTypeAnnotationInternal());
        }
        return unionType;
    }

    if (check(TokenType::AMPERSAND)) {
        TypeAnnotation intersectType;
        intersectType.isPresent = true;
        intersectType.intersections.push_back(type);
        while (match(TokenType::AMPERSAND)) {
            intersectType.intersections.push_back(parseTypeAnnotationInternal());
        }
        return intersectType;
    }

    return type;
}


Parser::Parser(const std::vector<Token> &tokens) : tokens(tokens), pos(0) {}

std::vector<Statement> Parser::parse() {
  std::vector<Statement> statements;
  while (!check(TokenType::END)) {
    statements.push_back(statement());
  }
  return statements;
}

Statement Parser::statement() {
  int line = tokens[pos].line;
  if (match(TokenType::SET)) {
    return addLine(variableDeclaration(false), line);
  } else if (match(TokenType::VAR)) {
    return addLine(variableDeclaration(true), line);
  } else if (match(TokenType::DEFINE)) {
    return addLine(functionDeclaration(), line);
  } else if (match(TokenType::IF)) {
    return addLine(ifExpr(), line);
  } else if (match(TokenType::CLASS)) {
    return addLine(classDeclaration(false), line);
  } else if (match(TokenType::ABSTRACT)) {
    if (!match(TokenType::CLASS)) throw std::runtime_error("Expected 'class' after 'abstract'");
    return addLine(classDeclaration(true), line);
  } else if (match(TokenType::RETURN)) {
    return addLine(returnStatement(), line);
  } else if (match(TokenType::USE)) {
    return addLine(useStatement(), line);
  } else if (match(TokenType::WHILE)) {
    return addLine(whileStatement(), line);
  } else if (match(TokenType::FOR)) {
    return addLine(forStatement(), line);
  } else if (match(TokenType::EXPORT)) {
    return addLine(exportStatement(), line);
  } else if (match(TokenType::LAYOUT)) {
    return addLine(layoutDeclaration(), line);
  } else {
    return expression();
  }
}

Statement Parser::variableDeclaration(bool isMutable) {
  int line = tokens[pos].line;
  if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
    std::vector<DestructuringBinding> bindings = parseDestructuringPattern();
    if (!match(TokenType::EQUALS)) throw std::runtime_error("Expected = after destructuring pattern");
    auto value = expression();
    return addLine(DestructuringDeclaration{isMutable, std::move(bindings), std::move(value)}, line);
  }

  if (!isIdentifierLike(current().type)) {
    throw std::runtime_error("Expected identifier after set/var");
  }
  std::string name = advance().value;
  TypeAnnotation type = parseTypeAnnotation();
  if (!match(TokenType::EQUALS)) {
    throw std::runtime_error("Expected = after variable name");
  }
  auto value = expression();
  return addLine(VariableDeclaration{name, std::move(value), std::move(type), -1, isMutable, line}, line);
}

std::vector<DestructuringBinding> Parser::parseDestructuringPattern() {
  std::vector<DestructuringBinding> bindings;
  if (match(TokenType::LBRACKET)) {
    int arrayIndex = 0;
    if (!check(TokenType::RBRACKET)) {
      do {
        bool isRest = false;
        if (match(TokenType::DOT_DOT_DOT)) isRest = true;
        
        DestructuringBinding binding;
        binding.isRest = isRest;
        binding.arrayIndex = arrayIndex++;
        
        if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
          binding.nested = parseDestructuringPattern();
        } else if (isIdentifierLike(current().type)) {
          binding.name = advance().value;
        } else {
          throw std::runtime_error("Expected identifier or nested pattern in array destructuring");
        }
        
        bindings.push_back(std::move(binding));
        if (isRest) break;
      } while (match(TokenType::COMMA));
    }
    if (!match(TokenType::RBRACKET)) throw std::runtime_error("Expected ] after array destructuring");
  } else if (match(TokenType::LBRACE)) {
    if (!check(TokenType::RBRACE)) {
      do {
        bool isRest = false;
        if (match(TokenType::DOT_DOT_DOT)) isRest = true;
        
        if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected identifier in object destructuring");
        std::string propName = advance().value;
        
        DestructuringBinding binding;
        binding.isRest = isRest;
        binding.propertyName = propName;
        binding.name = propName; // default
        
        if (!isRest && match(TokenType::COLON)) {
          if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
            binding.name = ""; // marked as non-leaf
            binding.nested = parseDestructuringPattern();
          } else if (isIdentifierLike(current().type)) {
            binding.name = advance().value;
          } else {
            throw std::runtime_error("Expected identifier or nested pattern after :");
          }
        }
        
        bindings.push_back(std::move(binding));
        if (isRest) break;
      } while (match(TokenType::COMMA));
    }
    if (!match(TokenType::RBRACE)) throw std::runtime_error("Expected } after object destructuring");
  }
  return bindings;
}

Statement Parser::functionDeclaration() {
  int line = tokens[pos].line;
  if (!isIdentifierLike(current().type)) {
    throw std::runtime_error("Expected function name after define");
  }
  std::string name = advance().value;
  match(TokenType::EQUALS); // optional =
  if (!match(TokenType::LPAREN)) {
    throw std::runtime_error("Expected ( before parameters");
  }

  std::vector<std::pair<std::string, TypeAnnotation>> params;
  bool hasRest = false;
  if (!check(TokenType::RPAREN)) {
    do {
      if (match(TokenType::DOT_DOT_DOT)) {
        if (!isIdentifierLike(current().type)) {
          throw std::runtime_error("Expected identifier for rest parameter");
        }
        params.push_back({advance().value, TypeAnnotation()});
        hasRest = true;
        break; // rest parameter must be last
      }
      if (!isIdentifierLike(current().type)) {
        throw std::runtime_error("Expected parameter name");
      }
      std::string paramName = advance().value;
      TypeAnnotation type = parseTypeAnnotation();
      params.push_back({paramName, std::move(type)});
    } while (match(TokenType::COMMA));
  }

  if (!match(TokenType::RPAREN)) {
    throw std::runtime_error("Expected ) after parameters");
  }
  
  TypeAnnotation returnType = parseTypeAnnotation();
  if (!match(TokenType::ARROW)) {
    throw std::runtime_error("Expected => before function body");
  }

  std::shared_ptr<Expression> body;
  if (check(TokenType::LBRACE)) {
    body = block();
  } else {
    body = expression();
  }

  return addLine(FunctionDeclaration{name, std::move(params), std::move(body), std::move(returnType), 0, -1, hasRest}, line);
}

Statement Parser::classDeclaration(bool isAbstract) {
  int line = tokens[pos].line;
  if (!isIdentifierLike(current().type)) {
    throw std::runtime_error("Expected class name after 'class'");
  }
  std::string name = advance().value;
  if (!match(TokenType::LBRACE)) {
    throw std::runtime_error("Expected { after class name");
  }

  std::vector<PropertyDeclaration> properties;
  std::vector<FunctionDeclaration> methods;

  while (!check(TokenType::RBRACE) && !check(TokenType::END)) {
    int memberLine = tokens[pos].line;
    bool isPrivate = match(TokenType::PRIVATE);
    bool isReadonly = match(TokenType::READONLY);

    if (match(TokenType::SET) || match(TokenType::VAR)) {
      // Property
      if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected property name");
      std::string propName = advance().value;
      TypeAnnotation type = parseTypeAnnotation();
      std::shared_ptr<Expression> initializer = nullptr;
      if (match(TokenType::EQUALS)) {
        initializer = expression();
      }
      properties.push_back({propName, std::move(initializer), Value(), std::move(type), isPrivate, isReadonly, memberLine});
    } else if (match(TokenType::DEFINE)) {
      // Method
      Statement decl = functionDeclaration();
      auto& func = std::get<FunctionDeclaration>(decl);
      // Automatically return 'this' if no return is present and it's not a block with returns
      // For simplicity, we'll handle this in the interpreter/sp-function logic or wrap the body here
      methods.push_back(std::move(func));
    } else {
      throw std::runtime_error("Expected property or method in class body");
    }
  }

  if (!match(TokenType::RBRACE)) {
    throw std::runtime_error("Expected } after class body");
  }

  return ClassDeclaration{name, isAbstract, std::move(properties), std::move(methods), line};
}

std::shared_ptr<Expression> Parser::ifExpr() {
  int line = tokens[pos].line;
  auto condition = expression();

  std::shared_ptr<Expression> thenBranch;
  if (check(TokenType::LBRACE)) {
    thenBranch = block();
  } else {
    thenBranch = expression();
  }

  std::shared_ptr<Expression> elseBranch = nullptr;
  if (match(TokenType::ELSE)) {
    if (check(TokenType::LBRACE)) {
      elseBranch = block();
    } else if (match(TokenType::IF)) {
      elseBranch = ifExpr();
    } else {
      elseBranch = expression();
    }
  }

  return addLine(std::make_shared<IfExpression>(std::move(condition), std::move(thenBranch), std::move(elseBranch)), line);
}

Statement Parser::returnStatement() {
  int line = tokens[pos].line;
  // 'return' already matched by caller
  std::shared_ptr<Expression> value = nullptr;
  if (!check(TokenType::RBRACE) && !check(TokenType::END)) {
    value = expression();
  }
  return addLine(ReturnStatement{std::move(value)}, line);
}

Statement Parser::useStatement() {
  int line = tokens[pos].line;
  // 'use' already matched by caller
  UseStatement stmt;
  stmt.line = line;

  // Named import form: use { name, name as alias, ... } from module
  if (check(TokenType::LBRACE)) {
    advance(); // consume {
    stmt.isNamed = true;
    if (!check(TokenType::RBRACE)) {
      do {
        if (!isIdentifierLike(current().type))
          throw std::runtime_error("Expected identifier in named import list");
        std::string exportName = advance().value;
        std::string localAlias = exportName; // default: same name
        if (match(TokenType::AS)) {
          if (!isIdentifierLike(current().type))
            throw std::runtime_error("Expected alias name after 'as'");
          localAlias = advance().value;
        }
        stmt.namedImports.push_back({exportName, localAlias});
      } while (match(TokenType::COMMA));
    }
    if (!match(TokenType::RBRACE))
      throw std::runtime_error("Expected } after named import list");
    if (!match(TokenType::FROM))
      throw std::runtime_error("Expected 'from' after named import list");
    if (!isIdentifierLike(current().type) && !check(TokenType::STRING))
      throw std::runtime_error("Expected module name after 'from'");
    stmt.moduleName = advance().value;
    return addLine(stmt, line);
  }

  // Plain or aliased: use module  /  use module as alias
  if (!isIdentifierLike(current().type) && !check(TokenType::FS) && !check(TokenType::CONSOLE) && !check(TokenType::PROCESS))
    throw std::runtime_error("Expected module name after 'use'");
  stmt.moduleName = advance().value;
  if (match(TokenType::AS)) {
    if (!isIdentifierLike(current().type))
      throw std::runtime_error("Expected alias name after 'as'");
    stmt.alias = advance().value;
  }
  return addLine(stmt, line);
}

Statement Parser::exportStatement() {
  int line = tokens[pos].line;
  // 'export' already matched by caller
  if (match(TokenType::SET)) {
    Statement decl = variableDeclaration(false);
    if (auto* v = std::get_if<VariableDeclaration>(&decl)) return addLine(ExportStatement{*v}, line);
    if (auto* d = std::get_if<DestructuringDeclaration>(&decl)) return addLine(ExportStatement{*d}, line);
  } else if (match(TokenType::VAR)) {
    Statement decl = variableDeclaration(true);
    if (auto* v = std::get_if<VariableDeclaration>(&decl)) return addLine(ExportStatement{*v}, line);
    if (auto* d = std::get_if<DestructuringDeclaration>(&decl)) return addLine(ExportStatement{*d}, line);
  } else if (match(TokenType::DEFINE)) {
    Statement func = functionDeclaration();
    if (auto* f = std::get_if<FunctionDeclaration>(&func)) return addLine(ExportStatement{*f}, line);
    throw std::runtime_error("Expected function declaration after export define");
  } else if (match(TokenType::LAYOUT)) {
    Statement decl = layoutDeclaration();
    if (auto* l = std::get_if<LayoutDeclaration>(&decl)) return addLine(ExportStatement{*l}, line);
    throw std::runtime_error("Expected layout declaration after export");
  }
  throw std::runtime_error("Expected variable, function, or layout declaration after export");
}


Statement Parser::layoutDeclaration() {
  int line = tokens[pos].line;
  if (!isIdentifierLike(current().type)) {
     // If no name, it might be a layout expression used as a statement (useless but valid)
     // or the start of layout { ... } which should be handled by expression()
     // But layout Name { ... } is a declaration.
     throw std::runtime_error("Expected layout name");
  }
  std::string name = advance().value;
  
  if (match(TokenType::EQUALS)) {
      TypeAnnotation type = parseTypeAnnotationInternal();
      return addLine(LayoutDeclaration{name, {}, type, line}, line);
  }

  if (!match(TokenType::LBRACE)) throw std::runtime_error("Expected { or = after layout name");
  
  std::vector<PropertyDeclaration> properties;
  while (!check(TokenType::RBRACE) && !check(TokenType::END)) {
      int propLine = tokens[pos].line;
      if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected property name in layout");
      std::string propName = advance().value;
      TypeAnnotation type = parseTypeAnnotation();
      properties.push_back({propName, nullptr, Value(), std::move(type), false, true, propLine}); // Readonly by default!
      if (check(TokenType::COMMA)) advance();
  }
  if (!match(TokenType::RBRACE)) throw std::runtime_error("Expected } after layout body");
  
  return addLine(LayoutDeclaration{name, std::move(properties), {}, line}, line);
}

Statement Parser::whileStatement() {
    int line = tokens[pos].line;
    // 'while' already matched by caller
    auto condition = expression();
    auto body = block();
    return addLine(WhileStatement{condition, body}, line);
}

Statement Parser::forStatement() {
    int line = tokens[pos].line;
    // 'for' already matched by caller
    if (!isIdentifierLike(current().type)) {
        throw std::runtime_error("Expected identifier after 'for'");
    }
    std::string name = advance().value;
    if (!match(TokenType::IN)) {
        throw std::runtime_error("Expected 'in' after variable name in for loop");
    }
    auto collection = expression();
    auto body = block();
    return addLine(ForStatement{name, collection, body, -1}, line);
}

std::shared_ptr<Expression> Parser::expression() { return assignment(); }

std::shared_ptr<Expression> Parser::assignment() {
  int line = tokens[pos].line;
  auto expr = pipeline();

  if (match(TokenType::EQUALS)) {
    auto value = assignment(); // right-associative
    if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
      return addLine(std::make_shared<MemberAssignmentExpression>(member->object, member->property, std::move(value)), line);
    } else if (auto indexExpr = std::dynamic_pointer_cast<IndexExpression>(expr)) {
      return addLine(std::make_shared<IndexAssignmentExpression>(indexExpr->object, indexExpr->index, std::move(value)), line);
    } else if (auto id = std::dynamic_pointer_cast<IdentifierExpression>(expr)) {
      return addLine(std::make_shared<AssignmentExpression>(id->name, std::move(value)), line);
    }
    throw std::runtime_error("Invalid assignment target");
  }

  return expr;
}

std::shared_ptr<Expression> Parser::pipeline() {
  int line = tokens[pos].line;
  auto left = logicOr();
  while (match(TokenType::PIPE)) {
    std::string op = tokens[pos - 1].value;
    auto right = logicOr();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::logicOr() {
  int line = tokens[pos].line;
  auto left = logicAnd();
  while (match(TokenType::OR)) {
    std::string op = tokens[pos - 1].value;
    auto right = logicAnd();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::logicAnd() {
  int line = tokens[pos].line;
  auto left = equality();
  while (match(TokenType::AND)) {
    std::string op = tokens[pos - 1].value;
    auto right = equality();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::equality() {
  int line = tokens[pos].line;
  auto left = comparison();
  while (match(TokenType::EQUALS_EQUALS) || match(TokenType::NOT_EQUALS)) {
    std::string op = tokens[pos - 1].value;
    auto right = comparison();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::comparison() {
  int line = tokens[pos].line;
  auto left = term();
  while (match(TokenType::LESS) || match(TokenType::LESS_EQUALS) ||
         match(TokenType::GREATER) || match(TokenType::GREATER_EQUALS)) {
    std::string op = tokens[pos - 1].value;
    auto right = term();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::term() {
  int line = tokens[pos].line;
  auto left = factor();
  while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
    std::string op = tokens[pos - 1].value;
    auto right = factor();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::factor() {
  int line = tokens[pos].line;
  auto left = unary();
  while (match(TokenType::MULTIPLY) || match(TokenType::DIVIDE) || match(TokenType::MODULO)) {
    std::string op = tokens[pos - 1].value;
    auto right = unary();
    left = addLine(std::make_shared<BinaryExpression>(std::move(left), std::move(right), op), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::unary() {
  int line = tokens[pos].line;
  if (match(TokenType::NOT)) {
    std::string op = tokens[pos - 1].value;
    auto right = unary();
    return addLine(std::make_shared<UnaryExpression>(std::move(right), op), line);
  }
  if (match(TokenType::TYPEOF)) {
    auto right = unary();
    return addLine(std::make_shared<TypeofExpression>(std::move(right)), line);
  }
  return typeAssertion();
}

std::shared_ptr<Expression> Parser::typeAssertion() {
  int line = tokens[pos].line;
  auto left = primary();
  while (match(TokenType::AS)) {
    TypeAnnotation type = parseTypeAnnotationInternal();
    left = addLine(std::make_shared<AsExpression>(std::move(left), std::move(type)), line);
  }
  return left;
}

std::shared_ptr<Expression> Parser::primary() {
  int line = tokens[pos].line;
  std::shared_ptr<Expression> expr;
  if (match(TokenType::NUMBER)) {
    double nval = std::stod(tokens[pos - 1].value);
    expr = addLine(std::make_shared<LiteralExpression>(Value(nval)), line);
  } else if (match(TokenType::BIGINT_LITERAL)) {
    expr = addLine(std::make_shared<BigIntLiteralExpression>(tokens[pos - 1].value), line);
  } else if (match(TokenType::STRING)) {
    std::string sval = tokens[pos - 1].value;
    if (sval.find('{') != std::string::npos) {
      expr = parseInterpolatedString(sval);
      if (expr) expr->line = line;
    } else {
      expr = addLine(std::make_shared<LiteralExpression>(Value(new std::string(sval))), line);
    }
  } else if (match(TokenType::BOOLEAN)) {
    bool val = tokens[pos - 1].value == "true";
    expr = addLine(std::make_shared<LiteralExpression>(Value(val)), line);
  } else if (match(TokenType::NULL_VAL)) {
    expr = addLine(std::make_shared<LiteralExpression>(Value(Type::NULL_VAL)), line);
  } else if (match(TokenType::UNDEFINED)) {
    expr = addLine(std::make_shared<LiteralExpression>(Value(Type::UNDEFINED)), line);
  } else if (match(TokenType::CONSOLE)) {
    expr = consoleExpr();
  } else if (match(TokenType::PROCESS)) {
    expr = processExpr();
  } else if (match(TokenType::FS)) {
    expr = fsExpr();
  } else if (match(TokenType::STRING_KEYWORD)) {
    expr = stringExpr();
  } else if (match(TokenType::LAYOUT)) {
    int layoutLine = tokens[pos-1].line;
    if (!match(TokenType::LBRACE)) throw std::runtime_error("Expected { after layout keyword");
    std::vector<PropertyDeclaration> properties;
    while (!check(TokenType::RBRACE) && !check(TokenType::END)) {
        int propLine = tokens[pos].line;
        if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected property name in layout");
        std::string propName = advance().value;
        TypeAnnotation type = parseTypeAnnotation();
        properties.push_back({propName, nullptr, Value(), std::move(type), false, true, propLine}); // Readonly by default!
        if (check(TokenType::COMMA)) advance();
    }
    if (!match(TokenType::RBRACE)) throw std::runtime_error("Expected } after layout body");
    expr = addLine(std::make_shared<LayoutExpression>(std::move(properties)), layoutLine);
  } else if (match(TokenType::LBRACKET)) {
    expr = array();
    if (expr) expr->line = line;
  } else if (match(TokenType::THIS)) {
    expr = addLine(std::make_shared<ThisExpression>(), line);
  } else if (match(TokenType::LBRACE)) {
    expr = object();
    if (expr) expr->line = line;
  } else if (match(TokenType::IF)) {
    expr = ifExpr();
    if (expr) expr->line = line;
  } else if (match(TokenType::MATCH)) {
    expr = matchExpr();
    if (expr) expr->line = line;
  } else if (match(TokenType::ASYNC)) {
    std::shared_ptr<Expression> body;
    if (check(TokenType::LBRACE)) {
        body = block();
    } else {
        body = expression();
    }
    expr = addLine(std::make_shared<AsyncExpression>(std::move(body)), line);
  } else if (match(TokenType::AFTER)) {
    auto delay = expression();
    std::shared_ptr<Expression> body;
    if (check(TokenType::LBRACE)) body = block();
    else body = expression();
    expr = addLine(std::make_shared<AfterExpression>(std::move(delay), std::move(body)), line);
  } else if (match(TokenType::EVERY)) {
    auto delay = expression();
    std::shared_ptr<Expression> body;
    if (check(TokenType::LBRACE)) body = block();
    else body = expression();
    expr = addLine(std::make_shared<EveryExpression>(std::move(delay), std::move(body)), line);
  } else if (match(TokenType::LPAREN)) {
    // Look ahead to see if it's a lambda or grouped expression
    bool isLambda = false;
    if (check(TokenType::RPAREN)) { // () =>
        if (pos + 1 < tokens.size() && tokens[pos+1].type == TokenType::ARROW) isLambda = true;
    } else if (check(TokenType::DOT_DOT_DOT)) { // (...args) =>
        isLambda = true;
    } else if (isIdentifierLike(current().type)) {
        if (pos + 1 < tokens.size()) {
            if (tokens[pos+1].type == TokenType::COMMA) isLambda = true; // (a, 
            else if (tokens[pos+1].type == TokenType::COLON) isLambda = true; // (a: Type
            else if (tokens[pos+1].type == TokenType::RPAREN) { // (a) =>
                if (pos + 2 < tokens.size() && tokens[pos+2].type == TokenType::ARROW) isLambda = true;
            }
        }
    }

    if (isLambda) {
        std::vector<std::pair<std::string, TypeAnnotation>> params;
        bool hasRest = false;
        if (!check(TokenType::RPAREN)) {
            do {
                if (match(TokenType::DOT_DOT_DOT)) {
                    if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected identifier for rest parameter");
                    params.push_back({advance().value, TypeAnnotation()});
                    hasRest = true;
                    break;
                }
                if (!isIdentifierLike(current().type)) throw std::runtime_error("Expected parameter name");
                std::string paramName = advance().value;
                TypeAnnotation type = parseTypeAnnotation();
                params.push_back({paramName, std::move(type)});
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after parameters");
        
        TypeAnnotation returnType = parseTypeAnnotation();
        if (!match(TokenType::ARROW)) throw std::runtime_error("Expected => after parameters");

        std::shared_ptr<Expression> body;
        if (check(TokenType::LBRACE)) body = block();
        else body = expression();
        expr = addLine(std::make_shared<LambdaExpression>(std::move(params), std::move(body), hasRest, std::move(returnType)), line);
    }
 else {
        expr = expression();
        if (!match(TokenType::RPAREN)) {
            throw std::runtime_error("Expected ) after expression");
        }
        if (expr) expr->line = line;
    }
  } else if (isIdentifierLike(current().type)) {
    std::string name = advance().value;
    expr = addLine(std::make_shared<IdentifierExpression>(name), line);
  } else {
    throw std::runtime_error("Unexpected token in primary: '" + tokens[pos].value + "' at line " + std::to_string(tokens[pos].line) + " (pos: " + std::to_string(pos) + ")");
  }

  // Support member access, array indexing, and function calls
  while (true) {
    if (match(TokenType::DOT)) {
      if (!isIdentifierLike(current().type)) {
        std::cerr << "Parser error: Expected identifier after '.', found token type " << (int)current().type << " with value '" << current().value << "' at line " << current().line << std::endl;
        throw std::runtime_error("Expected identifier after .");
      }
      std::string prop = advance().value;
      expr = std::make_shared<MemberExpression>(expr, prop);
    } else if (match(TokenType::LBRACKET)) {
      auto indexExpr = expression();
      if (!match(TokenType::RBRACKET)) {
        throw std::runtime_error("Expected ] after index at line " + std::to_string(tokens[pos].line));
      }
      expr = std::make_shared<IndexExpression>(expr, indexExpr);
    } else if (match(TokenType::LPAREN)) {
      std::vector<std::shared_ptr<Expression>> args;
      if (!check(TokenType::RPAREN)) {
        do {
          if (match(TokenType::DOT_DOT_DOT)) {
            args.push_back(std::make_shared<SpreadExpression>(expression()));
          } else {
            args.push_back(expression());
          }
        } while (match(TokenType::COMMA));
      }
      if (!match(TokenType::RPAREN)) {
        throw std::runtime_error("Expected ) after arguments");
      }
      expr = std::make_shared<CallExpression>(expr, std::move(args));
    } else {
      break;
    }
  }

  return expr;
}

std::shared_ptr<Expression> Parser::block() {
  match(TokenType::LBRACE); // Consume '{'
  std::vector<Statement> stmts;

  while (!check(TokenType::RBRACE) && !check(TokenType::END)) {
    stmts.push_back(statement());
  }

  if (!match(TokenType::RBRACE)) {
    throw std::runtime_error("Expected } to close block");
  }
  return std::make_shared<BlockExpression>(std::move(stmts));
}

std::shared_ptr<Expression> Parser::array() {
  std::vector<std::shared_ptr<Expression>> elements;
  if (!check(TokenType::RBRACKET)) {
    do {
      if (match(TokenType::DOT_DOT_DOT)) {
        elements.push_back(std::make_shared<SpreadExpression>(expression()));
      } else {
        elements.push_back(expression());
      }
    } while (match(TokenType::COMMA));
  }
  if (!match(TokenType::RBRACKET)) {
    throw std::runtime_error("Expected ] after array");
  }
  return std::make_shared<ArrayExpression>(std::move(elements));
}

std::shared_ptr<Expression> Parser::object() {
  std::vector<std::pair<std::string, std::shared_ptr<Expression>>> properties;
  if (!check(TokenType::RBRACE)) {
    do {
      if (match(TokenType::DOT_DOT_DOT)) {
        properties.emplace_back("...", std::make_shared<SpreadExpression>(expression()));
      } else {
        if (!isIdentifierLike(current().type)) {
          throw std::runtime_error("Expected identifier in object");
        }
        std::string key = advance().value;
        if (!match(TokenType::COLON)) {
          throw std::runtime_error("Expected : after key");
        }
        auto value = expression();
        properties.emplace_back(key, std::move(value));
      }
    } while (match(TokenType::COMMA));
  }
  if (!match(TokenType::RBRACE)) {
    throw std::runtime_error("Expected } after object");
  }
  return std::make_shared<ObjectExpression>(std::move(properties));
}

std::shared_ptr<Expression> Parser::parseInterpolatedString(const std::string& val) {
  std::shared_ptr<Expression> result = nullptr;
  size_t start = 0;
  while (start < val.size()) {
    size_t braceOpen = std::string::npos;
    for (size_t i = start; i < val.size(); ++i) {
      if (val[i] == '{') {
        if (i == 0 || val[i-1] != '\\') {
          braceOpen = i;
          break;
        }
      }
    }

    if (braceOpen == std::string::npos) {
      std::string tail = "";
      for (size_t i = start; i < val.size(); ++i) {
        if (val[i] == '\\' && i + 1 < val.size() && (val[i+1] == '{' || val[i+1] == '}')) {
          continue; // skip backslash for escaped braces
        }
        tail += val[i];
      }
      auto part = std::make_shared<LiteralExpression>(Value(new std::string(tail)));
      if (!result) result = part;
      else result = std::make_shared<BinaryExpression>(result, part, "+");
      break;
    }

    if (braceOpen > start) {
      std::string head = "";
      for (size_t i = start; i < braceOpen; ++i) {
        if (val[i] == '\\' && i + 1 < val.size() && (val[i+1] == '{' || val[i+1] == '}')) {
          continue;
        }
        head += val[i];
      }
      auto part = std::make_shared<LiteralExpression>(Value(new std::string(head)));
      if (!result) result = part;
      else result = std::make_shared<BinaryExpression>(result, part, "+");
    }

    size_t braceClose = std::string::npos;
    for (size_t i = braceOpen + 1; i < val.size(); ++i) {
        if (val[i] == '}') {
            if (val[i-1] != '\\') {
                braceClose = i;
                break;
            }
        }
    }
    if (braceClose == std::string::npos) {
      throw std::runtime_error("Unterminated interpolation in string: " + val);
    }

    std::string exprStr = val.substr(braceOpen + 1, braceClose - braceOpen - 1);
    Lexer innerLex(exprStr);
    auto innerTokens = innerLex.tokenize();
    // Remove END token from inner tokens
    if (!innerTokens.empty() && innerTokens.back().type == TokenType::END) {
        innerTokens.pop_back();
    }
    
    if (innerTokens.empty()) {
        throw std::runtime_error("Empty interpolation in string");
    }

    Parser innerParser(innerTokens);
    bool failed = false;
    std::shared_ptr<Expression> exprNode = nullptr;
    try {
        exprNode = innerParser.expression();
        if (!innerParser.isAtEnd()) {
            failed = true;
        }
    } catch (const std::exception& e) {
        failed = true;
    } catch (...) {
        failed = true;
    }

    if (failed) {
        // Not a valid/complete expression, treat as literal text
        std::string literal = "{" + exprStr + "}";
        auto part = std::make_shared<LiteralExpression>(Value(new std::string(literal)));
        if (!result) result = part;
        else result = std::make_shared<BinaryExpression>(result, part, "+");
    } else {
        if (!result) result = exprNode;
        else result = std::make_shared<BinaryExpression>(result, exprNode, "+");
    }

    start = braceClose + 1;
  }
  
  if (!result) {
    return std::make_shared<LiteralExpression>(Value(new std::string("")));
  }
  return result;
}

std::shared_ptr<Expression> Parser::matchExpr() {
  // `match` keyword already consumed
  auto valueToMatch = expression();
  if (!match(TokenType::LBRACE)) {
    throw std::runtime_error("Expected { after match expression value");
  }

  std::vector<MatchCase> cases;
  if (!check(TokenType::RBRACE)) {
    do {
      std::shared_ptr<Expression> pattern = nullptr;
      if (match(TokenType::DEFAULT)) {
        // default case
      } else {
        pattern = expression();
      }
      if (!match(TokenType::COLON)) {
        throw std::runtime_error("Expected : after case pattern");
      }
      auto body = expression();
      cases.push_back({std::move(pattern), std::move(body)});
    } while (match(TokenType::COMMA));
  }
  if (!match(TokenType::RBRACE)) {
    throw std::runtime_error("Expected } after match cases");
  }
  return std::make_shared<MatchExpression>(std::move(valueToMatch), std::move(cases));
}

Token Parser::current() { return tokens[pos]; }

Token Parser::advance() { return tokens[pos++]; }

bool Parser::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

std::shared_ptr<Expression> Parser::consoleExpr() {
    int line = tokens[pos].line;
    // 'console' already matched by caller in primary()
    if (!match(TokenType::DOT)) throw std::runtime_error("Expected . after console");
    
    if (match(TokenType::SHOW)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after console.show");
        std::vector<std::shared_ptr<Expression>> exprs;
        if (!check(TokenType::RPAREN)) {
            do {
                exprs.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after console.show");
        return addLine(std::make_shared<ConsoleShowExpression>(std::move(exprs)), line);
    } else if (match(TokenType::WARN)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after console.warn");
        std::vector<std::shared_ptr<Expression>> exprs;
        if (!check(TokenType::RPAREN)) {
            do {
                exprs.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after console.warn");
        return addLine(std::make_shared<ConsoleWarnExpression>(std::move(exprs)), line);
    } else if (match(TokenType::ARGS)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after console.args");
        std::shared_ptr<Expression> schema = nullptr;
        if (!check(TokenType::RPAREN)) {
            schema = expression();
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after console.args");
        return addLine(std::make_shared<ConsoleArgsExpression>(std::move(schema)), line);
    } else if (match(TokenType::READ)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after console.read");
        std::shared_ptr<Expression> options = nullptr;
        if (!check(TokenType::RPAREN)) {
            options = expression();
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after console.read");
        return addLine(std::make_shared<ConsoleReadExpression>(std::move(options)), line);
    }
    throw std::runtime_error("Expected show, warn, args, or read after console.");
}

std::shared_ptr<Expression> Parser::processExpr() {
    int line = tokens[pos].line;
    if (!match(TokenType::DOT)) throw std::runtime_error("Expected . after process");
    
    if (match(TokenType::SLEEP)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after process.sleep");
        auto delay = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after process.sleep");
        return addLine(std::make_shared<ProcessSleepExpression>(std::move(delay)), line);
    }

    bool isSpawn = false;
    if (match(TokenType::SPAWN)) {
        isSpawn = true;
    } else if (match(TokenType::RUN)) {
        isSpawn = false;
    } else {
        throw std::runtime_error("Expected run, spawn, or sleep after process.");
    }

    if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after process method");
    auto cmd = expression();
    std::vector<std::shared_ptr<Expression>> args;
    if (match(TokenType::COMMA)) {
        args.push_back(expression());
    }
    if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after process method");

    if (isSpawn) {
        return addLine(std::make_shared<ProcessSpawnExpression>(std::move(cmd), std::move(args)), line);
    } else {
        return addLine(std::make_shared<ProcessRunExpression>(std::move(cmd), std::move(args)), line);
    }
}

std::shared_ptr<Expression> Parser::fsExpr() {
    int line = tokens[pos].line;
    if (!match(TokenType::DOT)) throw std::runtime_error("Expected . after fs");

    TokenType methodType = tokens[pos].type;
    if (match(TokenType::CREATE)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.create");
        auto path = expression();
        if (!match(TokenType::COMMA)) throw std::runtime_error("Expected , after path in fs.create");
        auto content = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.create");
        return addLine(std::make_shared<FSCreateExpression>(std::move(path), std::move(content)), line);
    } else if (match(TokenType::OVERWRITE) || match(TokenType::APPEND)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs method");
        auto path = expression();
        if (!match(TokenType::COMMA)) throw std::runtime_error("Expected , after path in fs method");
        auto content = expression();
        
        std::shared_ptr<Expression> options = nullptr;
        if (match(TokenType::COMMA)) {
            options = expression();
        }
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs method");

        if (methodType == TokenType::OVERWRITE)
            return addLine(std::make_shared<FSOverwriteExpression>(std::move(path), std::move(content), std::move(options)), line);
        return addLine(std::make_shared<FSAppendExpression>(std::move(path), std::move(content), std::move(options)), line);
    } else if (match(TokenType::DELETE)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.delete");
        auto path = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.delete");
        return addLine(std::make_shared<FSDeleteExpression>(std::move(path)), tokens[pos - 1].line);
        return addLine(std::make_shared<FSDeleteExpression>(std::move(path)), line);
    } else if (match(TokenType::READ)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.read");
        auto path = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.read");
        return addLine(std::make_shared<FSReadExpression>(std::move(path)), line);
    } else if (match(TokenType::READ_JSON)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.readJson");
        auto path = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.readJson");
        return addLine(std::make_shared<FSReadJsonExpression>(std::move(path)), line);
    } else if (match(TokenType::WRITE_JSON)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.writeJson");
        auto path = expression();
        if (!match(TokenType::COMMA)) throw std::runtime_error("Expected , after path in fs.writeJson");
        auto content = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.writeJson");
        return addLine(std::make_shared<FSWriteJsonExpression>(std::move(path), std::move(content)), line);
    } else if (match(TokenType::INFO)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.info");
        auto path = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.info");
        return addLine(std::make_shared<FSInfoExpression>(std::move(path)), line);
    } else if (match(TokenType::IDENTIFIER)) {
        std::string method = tokens[pos - 1].value;
        if (method == "info") {
            if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after fs.info");
            auto path = expression();
            if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after fs.info");
            return addLine(std::make_shared<FSInfoExpression>(std::move(path)), line);
        }
        throw std::runtime_error("Unknown fs method: " + method);
    }

    throw std::runtime_error("Expected create, overwrite, append, read, path, dirname, name, ext, info, or delete after fs.");
}

std::shared_ptr<Expression> Parser::stringExpr() {
    int line = tokens[pos].line;
    if (!match(TokenType::DOT)) throw std::runtime_error("Expected . after string");
    if (match(TokenType::TRIM)) {
        if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after string.trim");
        auto arg = expression();
        if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after string.trim");
        return addLine(std::make_shared<TrimExpression>(std::move(arg)), line);
    } else if (isIdentifierLike(current().type)) {
        std::string method = advance().value;
        if (method == "size" || method == "length") {
            if (!match(TokenType::LPAREN)) throw std::runtime_error("Expected ( after string method");
            auto arg = expression();
            if (!match(TokenType::RPAREN)) throw std::runtime_error("Expected ) after string method");
            return addLine(std::make_shared<StringSizeExpression>(std::move(arg)), line);
        }
        throw std::runtime_error("Unknown string method: " + method);
    }
    throw std::runtime_error("Expected string method after string.");
}


bool Parser::check(TokenType type) {
  return pos < tokens.size() && tokens[pos].type == type;
}

bool Parser::isIdentifierLike(TokenType type) {
  if (type == TokenType::IDENTIFIER) return true;
  // Soft keywords allowed as properties or local variable names:
  switch (type) {
    case TokenType::SET:
    case TokenType::VAR:
    case TokenType::DEFINE:
    case TokenType::IF:
    case TokenType::ELSE:
    case TokenType::RETURN:
    case TokenType::MATCH:
    case TokenType::DEFAULT:
    case TokenType::USE:
    case TokenType::EXPORT:
    case TokenType::WHILE:
    case TokenType::FOR:
    case TokenType::IN:
    case TokenType::AS:
    case TokenType::FROM:
    case TokenType::CLASS:
    case TokenType::ABSTRACT:
    case TokenType::PRIVATE:
    case TokenType::READONLY:
    case TokenType::THIS:
    case TokenType::SHOW:
    case TokenType::WARN:
    case TokenType::ARGS:
    case TokenType::READ:
    case TokenType::CREATE:
    case TokenType::OVERWRITE:
    case TokenType::APPEND:
    case TokenType::DELETE:
    case TokenType::READ_JSON:
    case TokenType::WRITE_JSON:
    case TokenType::INFO:
    case TokenType::TRIM:
    case TokenType::SIZE:
    case TokenType::LENGTH:
    case TokenType::RUN:
    case TokenType::SPAWN:
    case TokenType::FS:
    case TokenType::CONSOLE:
    case TokenType::PROCESS:
    case TokenType::PATH:
    case TokenType::DIRNAME:
    case TokenType::NAME:
    case TokenType::EXT:
    case TokenType::AFTER:
    case TokenType::EVERY:
    case TokenType::ASYNC:
    case TokenType::STRING_KEYWORD:
    case TokenType::SLEEP:
        return true;
    default:
        return false;
  }
}