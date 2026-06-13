#include "vm.h"
#include "interpreter.h"

// Undefine macros that conflict with SP OpCodes (from system headers included by httplib)
#ifdef ADD
#undef ADD
#endif
#ifdef DELETE
#undef DELETE
#endif
#ifdef OPTIONAL
#undef OPTIONAL
#endif

#include "httplib.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

// Utility to parse simple URLs
struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path;
    int port;
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    parsed.path = "/";
    parsed.scheme = "http";
    
    size_t scheme_end = url.find("://");
    size_t host_start = 0;
    if (scheme_end != std::string::npos) {
        parsed.scheme = url.substr(0, scheme_end);
        host_start = scheme_end + 3;
    }
    
    std::string host_port_path = url.substr(host_start);
    size_t path_start = host_port_path.find('/');
    std::string host_port;
    if (path_start != std::string::npos) {
        host_port = host_port_path.substr(0, path_start);
        parsed.path = host_port_path.substr(path_start);
    } else {
        host_port = host_port_path;
        parsed.path = "/";
    }
    
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        parsed.host = host_port.substr(0, colon_pos);
        try {
            parsed.port = std::stoi(host_port.substr(colon_pos + 1));
        } catch (...) {
            parsed.port = (parsed.scheme == "https") ? 443 : 80;
        }
    } else {
        parsed.host = host_port;
        parsed.port = (parsed.scheme == "https") ? 443 : 80;
    }
    
    return parsed;
}

Value wrapResponse(Interpreter& interp, const httplib::Result& res) {
    if (!res) {
        auto err = std::make_shared<ErrorData>("Connection failed: " + std::to_string((int)res.error()));
        Value::registerError(err);
        return Value(err.get());
    }

    auto* obj = interp.makeObject();
    obj->push_back({"status", Value((double)res->status)});
    obj->push_back({"body", Value(interp.makeString(res->body))});

    auto* headers = interp.makeObject();
    for (const auto& header : res->headers) {
        headers->push_back({header.first, Value(interp.makeString(header.second))});
    }
    obj->push_back({"headers", Value(headers)});

    // Add .json() helper
    auto jsonFunc = std::make_shared<NativeFunction>([body = res->body](Interpreter& innerInterp, const std::vector<Value>&) {
        size_t pos = 0;
        return parseJSONValue(body, pos, innerInterp);
    });
    Value::registerFunction(jsonFunc);
    obj->push_back({"json", Value(jsonFunc.get(), true)});

    return Value(obj);
}

void registerNetModule(VM& vm, Interpreter& interp) {
    auto netGet = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("net.get requires url.");
        std::string url = *args[0].asString();
        ParsedUrl p = parseUrl(url);

        std::string origin = p.scheme + "://" + p.host + ":" + std::to_string(p.port);
        httplib::Client cli(origin);
        cli.set_follow_location(true);

        auto res = cli.Get(p.path);
        return wrapResponse(interp, res);
    });
    Value::registerFunction(netGet);

    auto netPost = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
        if (args.empty() || !args[0].isString()) throw std::runtime_error("net.post requires url.");
        std::string url = *args[0].asString();
        std::string body = "";
        std::string contentType = "text/plain";

        if (args.size() > 1) {
            if (args[1].isString()) {
                body = *args[1].asString();
            } else if (args[1].isObject()) {
                body = stringifyJSON(args[1], 0);
                contentType = "application/json";
            }
        }

        ParsedUrl p = parseUrl(url);
        std::string origin = p.scheme + "://" + p.host + ":" + std::to_string(p.port);
        httplib::Client cli(origin);
        cli.set_follow_location(true);

        auto res = cli.Post(p.path.c_str(), body, contentType.c_str());
        return wrapResponse(interp, res);
    });
    Value::registerFunction(netPost);

    auto netServe = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
        if (args.size() < 2 || !args[0].isNumber() || !args[1].isFunction()) {
            throw std::runtime_error("net.serve(port, handler) expects port and callback.");
        }
        int port = (int)args[0].asNumber();
        auto handler = args[1];

        httplib::Server svr;
        static std::mutex interpreterMutex;

        auto genericHandler = [&](const httplib::Request& req, httplib::Response& res) {
            Value::CurrentContext = &interp;
            std::cout << "[Server] Handling " << req.method << " " << req.path << "..." << std::endl;
            std::lock_guard<std::mutex> lock(interpreterMutex);
            std::cout << "[Server] Lock acquired for " << req.path << std::endl;
            auto* reqObj = interp.makeObject();
            reqObj->push_back({"method", Value(interp.makeString(req.method))});
            reqObj->push_back({"path", Value(interp.makeString(req.path))});
            reqObj->push_back({"body", Value(interp.makeString(req.body))});
            
            auto* headers = interp.makeObject();
            for (const auto& h : req.headers) {
                headers->push_back({h.first, Value(interp.makeString(h.second))});
            }
            reqObj->push_back({"headers", Value(headers)});

            auto* query = interp.makeObject();
            for (const auto& q : req.params) {
                query->push_back({q.first, Value(interp.makeString(q.second))});
            }
            reqObj->push_back({"query", Value(query)});

            Value handlerRes;
            if (interp.callHandler) {
                handlerRes = interp.callHandler(handler.asFunction(), {Value(reqObj)});
            } else {
                handlerRes = handler.asFunction()->call(interp, {Value(reqObj)});
            }

            if (handlerRes.isString()) {
                res.status = 200;
                res.set_content(*handlerRes.asString(), "text/plain");
            } else if (handlerRes.isObject() && !handlerRes.isInstance()) {
                auto* resObj = handlerRes.asObject();
                int status = 200;
                std::string body = "";
                std::string type = "text/plain";
                bool bodySet = false;

                for (auto& p : *resObj) {
                    if (p.first == "status") status = (int)p.second.asNumber();
                    else if (p.first == "body") {
                        bodySet = true;
                        if (p.second.isString()) body = *p.second.asString();
                        else {
                            body = stringifyJSON(p.second, 0);
                            type = "application/json";
                        }
                    }
                    else if (p.first == "headers") {
                        if (p.second.isObject()) {
                            for (auto& h : *p.second.asObject()) {
                                res.set_header(h.first, h.second.toPureString());
                            }
                        }
                    }
                }
                
                if (!bodySet) {
                    body = stringifyJSON(handlerRes, 0);
                    type = "application/json";
                }

                res.status = status;
                res.set_content(body, type.c_str());
            } else {
                res.status = 200;
                res.set_content(handlerRes.toPureString(), "text/plain");
            }
            std::cout << "[Server] Finished " << req.path << " (Status: " << res.status << ")" << std::endl;
        };

        svr.Get(".*", genericHandler);
        svr.Post(".*", genericHandler);
        svr.Put(".*", genericHandler);
        svr.Delete(".*", genericHandler);
        svr.Patch(".*", genericHandler);

        std::cout << "SP Net Server listening on port " << port << "..." << std::endl;
        svr.listen("0.0.0.0", port);
        return Value(Type::NULL_VAL);
    });
    Value::registerFunction(netServe);

    auto* netObj = interp.makeObject();
    netObj->push_back({"get", Value(netGet.get(), true)});
    netObj->push_back({"post", Value(netPost.get(), true)});
    netObj->push_back({"serve", Value(netServe.get(), true)});

    Value netVal(netObj);
    vm.defineGlobal("net", netVal);
    vm.defineGlobal("http", netVal); // Alias for convenience
    interp.environment->define("net", netVal);
    interp.environment->define("http", netVal);
}
