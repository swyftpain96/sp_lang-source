#include "lexer.h"
#include <cctype>
#include <iostream>

Lexer::Lexer(const std::string& source) : source(source), pos(0), line(1) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (pos < source.size()) {
        skipWhitespace();
        if (pos >= source.size()) break;
        char c = peek();
        if (isdigit(c)) {
            tokens.push_back(number());
        } else if (c == '"') {
            tokens.push_back(string());
        } else if (isalpha(c) || c == '_') {
            tokens.push_back(identifier());
        } else {
            switch (c) {
                case '=':
                    advance();
                    if (pos < source.size() && peek() == '=') {
                        advance();
                        tokens.push_back({TokenType::EQUALS_EQUALS, "==", line});
                    } else if (pos < source.size() && peek() == '>') {
                        advance();
                        tokens.push_back({TokenType::ARROW, "=>", line});
                    } else {
                        tokens.push_back({TokenType::EQUALS, "=", line});
                    }
                    break;
                case '!':
                    advance();
                    if (pos < source.size() && peek() == '=') {
                        advance();
                        tokens.push_back({TokenType::NOT_EQUALS, "!=", line});
                    } else {
                        tokens.push_back({TokenType::NOT, "!", line});
                    }
                    break;
                case '<':
                    advance();
                    if (pos < source.size() && peek() == '=') {
                        advance();
                        tokens.push_back({TokenType::LESS_EQUALS, "<=", line});
                    } else {
                        tokens.push_back({TokenType::LESS, "<", line});
                    }
                    break;
                case '>':
                    advance();
                    if (pos < source.size() && peek() == '=') {
                        advance();
                        tokens.push_back({TokenType::GREATER_EQUALS, ">=", line});
                    } else {
                        tokens.push_back({TokenType::GREATER, ">", line});
                    }
                    break;
                case '&':
                    advance();
                    if (pos < source.size() && peek() == '&') {
                        advance();
                        tokens.push_back({TokenType::AND, "&&", line});
                    } else {
                        tokens.push_back({TokenType::AMPERSAND, "&", line});
                    }
                    break;
                case '|':
                    advance();
                    if (pos < source.size() && peek() == '>') {
                        advance();
                        tokens.push_back({TokenType::PIPE, "|>", line});
                    } else if (pos < source.size() && peek() == '|') {
                        advance();
                        tokens.push_back({TokenType::OR, "||", line});
                    } else {
                        tokens.push_back({TokenType::BAR, "|", line});
                    }
                    break;
                case '[': tokens.push_back({TokenType::LBRACKET, "[", line}); advance(); break;
                case ']': tokens.push_back({TokenType::RBRACKET, "]", line}); advance(); break;
                case '{': tokens.push_back({TokenType::LBRACE, "{", line}); advance(); break;
                case '}': tokens.push_back({TokenType::RBRACE, "}", line}); advance(); break;
                case ':': tokens.push_back({TokenType::COLON, ":", line}); advance(); break;
                case ',': tokens.push_back({TokenType::COMMA, ",", line}); advance(); break;
                case '.':
                    advance();
                    if (pos < source.size() && peek() == '.' && pos + 1 < source.size() && source[pos + 1] == '.') {
                        advance();
                        advance();
                        tokens.push_back({TokenType::DOT_DOT_DOT, "...", line});
                    } else {
                        tokens.push_back({TokenType::DOT, ".", line});
                    }
                    break;
                case '(': tokens.push_back({TokenType::LPAREN, "(", line}); advance(); break;
                case ')': tokens.push_back({TokenType::RPAREN, ")", line}); advance(); break;
                case '+': tokens.push_back({TokenType::PLUS, "+", line}); advance(); break;
                case '-': tokens.push_back({TokenType::MINUS, "-", line}); advance(); break;
                case '*': tokens.push_back({TokenType::MULTIPLY, "*", line}); advance(); break;
                case '/': tokens.push_back({TokenType::DIVIDE, "/", line}); advance(); break;
                case '%': tokens.push_back({TokenType::MODULO, "%", line}); advance(); break;
                default:
                    std::cerr << "Unexpected character: " << c << " at line " << line << std::endl;
                    advance();
            }
        }
    }
    tokens.push_back({TokenType::END, "", line});
    return tokens;
}

char Lexer::peek() {
    return source[pos];
}

char Lexer::advance() {
    return source[pos++];
}

void Lexer::skipWhitespace() {
    while (pos < source.size()) {
        if (isspace(source[pos])) {
            if (source[pos] == '\n') line++;
            pos++;
        } else if (source[pos] == '/' && pos + 1 < source.size()) {
            if (source[pos + 1] == '/') {
                // Single-line comment
                pos += 2;
                while (pos < source.size() && source[pos] != '\n') {
                    pos++;
                }
            } else if (source[pos + 1] == '*') {
                // Multi-line comment
                pos += 2;
                while (pos < source.size() && !(source[pos] == '*' && pos + 1 < source.size() && source[pos + 1] == '/')) {
                    if (source[pos] == '\n') line++;
                    pos++;
                }
                if (pos < source.size()) pos += 2; // skip */
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

Token Lexer::number() {
    size_t start = pos;
    while (pos < source.size() && (isdigit(source[pos]) || source[pos] == '.')) {
        advance();
    }
    std::string val = source.substr(start, pos - start);
    if (pos < source.size() && source[pos] == 'n' && val.find('.') == std::string::npos) {
        advance(); // consume 'n'
        return {TokenType::BIGINT_LITERAL, val, line};
    }
    return {TokenType::NUMBER, val, line};
}

Token Lexer::string() {
    advance(); // skip opening "
    std::string val = "";
    while (pos < source.size()) {
        if (source[pos] == '\\' && pos + 1 < source.size()) {
            char next = source[pos + 1];
            if (next == '"' || next == '\\' || next == 'n' || next == 'r' || next == 't') {
                advance(); // Skip backslash character
                char esc = advance();
                if (esc == '"') val += '"';
                else if (esc == '\\') val += '\\';
                else if (esc == 'n') val += '\n';
                else if (esc == 'r') val += '\r';
                else if (esc == 't') val += '\t';
            } else {
                // Keep the backslash for the parser (e.g. \{)
                val += advance();
                val += advance();
            }
        } else if (source[pos] == '"') {
            break;
        } else {
            if (source[pos] == '\n') line++;
            val += advance();
        }
    }
    if (pos < source.size()) advance(); // skip closing "
    return {TokenType::STRING, val, line};
}

Token Lexer::identifier() {
    size_t start = pos;
    while (pos < source.size() && (isalnum(source[pos]) || source[pos] == '_')) {
        advance();
    }
    std::string val = source.substr(start, pos - start);
    TokenType type = TokenType::IDENTIFIER;
    if (val == "set") type = TokenType::SET;
    else if (val == "var") type = TokenType::VAR;
    else if (val == "define") type = TokenType::DEFINE;
    else if (val == "if") type = TokenType::IF;
    else if (val == "else") type = TokenType::ELSE;
    else if (val == "return") type = TokenType::RETURN;
    else if (val == "match") type = TokenType::MATCH;
    else if (val == "default") type = TokenType::DEFAULT;
    else if (val == "use") type = TokenType::USE;
    else if (val == "export") type = TokenType::EXPORT;
    else if (val == "console") type = TokenType::CONSOLE;
    else if (val == "show") type = TokenType::SHOW;
    else if (val == "warn") type = TokenType::WARN;
    else if (val == "args") type = TokenType::ARGS;
    else if (val == "read") type = TokenType::READ;
    else if (val == "true" || val == "false") type = TokenType::BOOLEAN;
    else if (val == "null") type = TokenType::NULL_VAL;
    else if (val == "undefined") type = TokenType::UNDEFINED;
    else if (val == "while") type = TokenType::WHILE;
    else if (val == "for") type = TokenType::FOR;
    else if (val == "in") type = TokenType::IN;
    else if (val == "as") type = TokenType::AS;
    else if (val == "from") type = TokenType::FROM;
    else if (val == "class") type = TokenType::CLASS;
    else if (val == "abstract") type = TokenType::ABSTRACT;
    else if (val == "private") type = TokenType::PRIVATE;
    else if (val == "readonly") type = TokenType::READONLY;
    else if (val == "this") type = TokenType::THIS;
    else if (val == "process") type = TokenType::PROCESS;
    else if (val == "run") type = TokenType::RUN;
    else if (val == "spawn") type = TokenType::SPAWN;
    else if (val == "fs") type = TokenType::FS;
    else if (val == "create") type = TokenType::CREATE;
    else if (val == "overwrite") type = TokenType::OVERWRITE;
    else if (val == "append") type = TokenType::APPEND;
    else if (val == "delete") type = TokenType::DELETE;
    else if (val == "readJson") type = TokenType::READ_JSON;
    else if (val == "writeJson") type = TokenType::WRITE_JSON;
    else if (val == "info") type = TokenType::INFO;
    else if (val == "string") type = TokenType::STRING_KEYWORD;
    else if (val == "trim") type = TokenType::TRIM;
    else if (val == "size") type = TokenType::SIZE;
    else if (val == "length") type = TokenType::LENGTH;
    else if (val == "async") type = TokenType::ASYNC;
    else if (val == "after") type = TokenType::AFTER;
    else if (val == "every") type = TokenType::EVERY;
    else if (val == "sleep") type = TokenType::SLEEP;
    else if (val == "layout") type = TokenType::LAYOUT;
    else if (val == "typeof") type = TokenType::TYPEOF;
    return {type, val, line};
}