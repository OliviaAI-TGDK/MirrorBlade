#include "TGDKLoader.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cctype>
#include <stack>
#include <cmath>
#include <unordered_map>

#if __has_include("MBLog.hpp")
#include "MBLog.hpp"
#define MBLOGI(fmt, ...) MB::Log().Log(MB::LogLevel::Info,  fmt, __VA_ARGS__)
#define MBLOGW(fmt, ...) MB::Log().Log(MB::LogLevel::Warn,  fmt, __VA_ARGS__)
#define MBLOGE(fmt, ...) MB::Log().Log(MB::LogLevel::Error, fmt, __VA_ARGS__)
#else
#define MBLOGI(...) (void)0
#define MBLOGW(...) (void)0
#define MBLOGE(...) (void)0
#endif

namespace MB {
    using json = nlohmann::json;

    // -------------------------------------------------
    // LoaderContext helper
    // -------------------------------------------------
    std::optional<double> LoaderContext::GetVar(std::string_view name) const {
        if (!baseEnv || !baseEnv->is_object()) return std::nullopt;
        auto it = baseEnv->find(std::string(name));
        if (it == baseEnv->end()) return std::nullopt;
        if (it->is_number_float())   return it->get<double>();
        if (it->is_number_integer()) return static_cast<double>(it->get<long long>());
        if (it->is_number_unsigned())return static_cast<double>(it->get<unsigned long long>());
        return std::nullopt;
    }

    // -------------------------------------------------
    // Minimal expression evaluator (shunting-yard + RPN)
    // Supports: numbers, identifiers, (), + - * / ^, unary -, functions:
    //  abs(x), min(a,b), max(a,b), clamp(x,lo,hi)
    //
    // Variables resolve from provided env JSON (numbers only).
    // -------------------------------------------------
    namespace {

        enum class TokKind { Num, Id, LParen, RParen, Comma, Op, End };
        struct Token {
            TokKind kind{};
            std::string text; // for Id/Op
            double num{ 0.0 };  // for Num
        };

        struct Lexer {
            std::string_view s;
            size_t i{ 0 };

            static bool isIdStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
            static bool isId(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '.'; }

            Token next() {
                while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
                if (i >= s.size()) return { TokKind::End, "", 0.0 };

                char c = s[i];
                if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
                    size_t start = i;
                    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) ++i;
                    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                        ++i;
                        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                        while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
                    }
                    double v = std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
                    return { TokKind::Num, "", v };
                }
                if (isIdStart(c)) {
                    size_t start = i++;
                    while (i < s.size() && isId(s[i])) ++i;
                    return { TokKind::Id, std::string(s.substr(start, i - start)), 0.0 };
                }
                ++i;
                switch (c) {
                case '(': return { TokKind::LParen, "(", 0.0 };
                case ')': return { TokKind::RParen, ")", 0.0 };
                case ',': return { TokKind::Comma, ",", 0.0 };
                case '+': case '-': case '*': case '/': case '^':
                    return { TokKind::Op, std::string(1, c), 0.0 };
                default:
                    // Unknown char: treat as end for safety
                    return { TokKind::End, "", 0.0 };
                }
            }
        };

        inline int precedence(const std::string& op) {
            if (op == "^") return 4;
            if (op == "*" || op == "/") return 3;
            if (op == "+" || op == "-") return 2;
            if (op == "neg") return 5; // unary minus (as function)
            return 0;
        }
        inline bool rightAssoc(const std::string& op) {
            return op == "^" || op == "neg";
        }

        struct RPN {
            // token: number, variable name, function/op name (with arg count)
            struct Node { enum { Num, Var, Op } kind; double num; std::string sym; int argc; };
            std::vector<Node> code;
        };

        bool emitRPN(std::string_view expr, RPN& out, std::string& err) {
            Lexer lx{ expr };
            std::vector<std::string> opstack;
            std::vector<int> argstack; // for functions: number of args parsed
            Token prev{ TokKind::End,"",0.0 };
            for (;;) {
                Token t = lx.next();
                if (t.kind == TokKind::End) {
                    break;
                }
                else if (t.kind == TokKind::Num) {
                    out.code.push_back({ RPN::Node::Num, t.num, {}, 0 });
                }
                else if (t.kind == TokKind::Id) {
                    // Could be function or variable. Peek next:
                    Token t2 = lx.next();
                    if (t2.kind == TokKind::LParen) {
                        // function - push name
                        opstack.push_back(t.text); // function name
                        argstack.push_back(0);
                        opstack.push_back("(");
                        prev = { TokKind::LParen, "(", 0.0 };
                        continue;
                    }
                    else {
                        // variable
                        out.code.push_back({ RPN::Node::Var, 0.0, t.text, 0 });
                        // No robust "unget"—keeping grammar simple; next token resumes.
                    }
                }
                else if (t.kind == TokKind::LParen) {
                    opstack.push_back("(");
                }
                else if (t.kind == TokKind::Comma) {
                    // Unwind until "("
                    while (!opstack.empty() && opstack.back() != "(") {
                        out.code.push_back({ RPN::Node::Op, 0.0, opstack.back(), 2 });
                        opstack.pop_back();
                    }
                    if (argstack.empty()) { err = "Unexpected ','"; return false; }
                    argstack.back()++;
                }
                else if (t.kind == TokKind::RParen) {
                    // Pop ops until "("
                    while (!opstack.empty() && opstack.back() != "(") {
                        out.code.push_back({ RPN::Node::Op, 0.0, opstack.back(), 2 });
                        opstack.pop_back();
                    }
                    if (opstack.empty()) { err = "Mismatched ')'"; return false; }
                    opstack.pop_back(); // remove "("
                    // If there is a function before '(', emit it
                    if (!opstack.empty()) {
                        const std::string fn = opstack.back();
                        if (!fn.empty() && std::isalpha((unsigned char)fn[0])) {
                            opstack.pop_back();
                            int argc = (argstack.empty() ? 0 : (argstack.back() + 1));
                            if (!argstack.empty()) argstack.pop_back();
                            out.code.push_back({ RPN::Node::Op, 0.0, fn, argc });
                        }
                    }
                }
                else if (t.kind == TokKind::Op) {
                    std::string op = t.text;
                    // Detect unary minus
                    if (op == "-" && (prev.kind == TokKind::End || prev.kind == TokKind::Op || prev.kind == TokKind::LParen || prev.kind == TokKind::Comma)) {
                        op = "neg";
                    }
                    while (!opstack.empty()) {
                        const std::string& top = opstack.back();
                        if (top == "(" || (std::isalpha((unsigned char)top[0]) && top != "neg")) break; // stop at '(' or function name
                        int p1 = precedence(op);
                        int p2 = precedence(top);
                        if ((rightAssoc(op) && p1 < p2) || (!rightAssoc(op) && p1 <= p2)) {
                            out.code.push_back({ RPN::Node::Op, 0.0, top,  (top == "neg") ? 1 : 2 });
                            opstack.pop_back();
                        }
                        else break;
                    }
                    opstack.push_back(op);
                }
                prev = t;
            }
            while (!opstack.empty()) {
                const std::string top = opstack.back();
                if (top == "(") { std::string dummy = ""; (void)dummy; err = "Mismatched '('"; return false; }
                out.code.push_back({ RPN::Node::Op, 0.0, top, (top == "neg") ? 1 : 2 });
                opstack.pop_back();
            }
            return true;
        }

        double pop1(std::vector<double>& st) { double a = st.back(); st.pop_back(); return a; }
        std::pair<double, double> pop2(std::vector<double>& st) { double b = st.back(); st.pop_back(); double a = st.back(); st.pop_back(); return { a,b }; }

        EquationResult evalRPN(const RPN& rpn, const json& env) {
            std::vector<double> st;
            for (const auto& n : rpn.code) {
                if (n.kind == RPN::Node::Num) {
                    st.push_back(n.num);
                }
                else if (n.kind == RPN::Node::Var) {
                    auto it = env.find(n.sym);
                    if (it == env.end()) return { false, 0.0, "Unknown variable: " + n.sym };
                    if (it->is_number_float()) st.push_back(it->get<double>());
                    else if (it->is_number_integer()) st.push_back(static_cast<double>(it->get<long long>()));
                    else if (it->is_number_unsigned()) st.push_back(static_cast<double>(it->get<unsigned long long>()));
                    else return { false, 0.0, "Variable not numeric: " + n.sym };
                }
                else {
                    // operator/function
                    auto sym = n.sym;
                    if (sym == "neg") {
                        if (st.empty()) return { false, 0.0, "neg: stack underflow" };
                        st.back() = -st.back();
                        continue;
                    }
                    if (sym == "+" || sym == "-" || sym == "*" || sym == "/" || sym == "^") {
                        if (st.size() < 2) return { false, 0.0, "operator: stack underflow" };
                        auto [a, b] = pop2(st);
                        if (sym == "+") st.push_back(a + b);
                        else if (sym == "-") st.push_back(a - b);
                        else if (sym == "*") st.push_back(a * b);
                        else if (sym == "/") st.push_back(b == 0.0 ? 0.0 : a / b);
                        else                  st.push_back(std::pow(a, b));
                        continue;
                    }
                    // functions
                    if (sym == "abs") {
                        if (st.empty()) return { false, 0.0, "abs: stack underflow" };
                        st.back() = std::fabs(st.back());
                    }
                    else if (sym == "min" || sym == "max") {
                        if (st.size() < 2) return { false, 0.0, sym + ": argc<2" };
                        auto [a, b] = pop2(st);
                        st.push_back(sym == "min" ? std::min(a, b) : std::max(a, b));
                    }
                    else if (sym == "clamp") {
                        if (st.size() < 3) return { false, 0.0, "clamp: argc<3" };
                        double hi = pop1(st);
                        double lo = pop1(st);
                        double x = pop1(st);
                        st.push_back(std::max(lo, std::min(hi, x)));
                    }
                    else {
                        return { false, 0.0, "Unknown function/op: " + sym };
                    }
                }
            }
            if (st.size() != 1) return { false, 0.0, "Invalid expression" };
            return { true, st.back(), {} };
        }

    } // anon

    EquationResult TGDKLoader::ResolveEquation(std::string_view expr, const json& env) {
        RPN rpn;
        std::string err;
        if (!emitRPN(expr, rpn, err))
            return { false, 0.0, err };
        return evalRPN(rpn, env);
    }

    // -------------------------------------------------
    // TGDKLoader impl
    // -------------------------------------------------

    TGDKLoader::TGDKLoader() {
        // Register built-ins
        Register(std::unique_ptr<ILoaderService>(new CompoundLoader()));
        Register(std::unique_ptr<ILoaderService>(new ImpoundLoader()));
        Register(std::unique_ptr<ILoaderService>(new VolumetricPhiLoader()));
    }

    void TGDKLoader::Register(std::unique_ptr<ILoaderService> svc) {
        if (!svc) return;
        std::lock_guard<std::mutex> lk(_mx);
        _services[svc->Name()] = std::shared_ptr<ILoaderService>(std::move(svc));
    }

    void TGDKLoader::Unregister(std::string_view name) {
        std::lock_guard<std::mutex> lk(_mx);
        _services.erase(std::string(name));
    }

    ILoaderService* TGDKLoader::Get(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _services.find(std::string(name));
        return it == _services.end() ? nullptr : it->second.get();
    }

    void TGDKLoader::Load(const json& config, const json& env) {
        std::lock_guard<std::mutex> lk(_mx);
        _lastConfig = config;
        _lastEnv = env;

        LoaderContext ctx{ &_lastEnv };
        for (auto& kv : _services) {
            kv.second->Configure(_lastConfig, ctx);
        }
        for (auto& kv : _services) {
            kv.second->Apply();
        }
    }

    bool TGDKLoader::LoadFromFile(const std::string& path, const json& env) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        json j;
        try {
            f >> j;
        }
        catch (...) {
            return false;
        }
        Load(j, env);
        return true;
    }

    nlohmann::json TGDKLoader::SnapshotAll() const {
        std::lock_guard<std::mutex> lk(_mx);
        json out = json::object();
        for (const auto& kv : _services) {
            out[kv.first] = kv.second->Snapshot();
        }
        return out;
    }

    // -------------------------------------------------
    // CompoundLoader
    // -------------------------------------------------

    void CompoundLoader::Configure(const json& cfg, const LoaderContext& ctx) {
        std::lock_guard<std::mutex> lk(_mx);
        _staged.clear();

        const json* sec = nullptr;
        auto it = cfg.find("compound");
        if (it != cfg.end() && it->is_object()) sec = &(*it);
        if (!sec) return;

        // Collect pre-known entity values so later equations can reference earlier by name.
        std::unordered_map<std::string, double> envChain;
        if (ctx.baseEnv && ctx.baseEnv->is_object()) {
            for (auto& kv : ctx.baseEnv->items()) {
                if (kv.value().is_number()) envChain[kv.key()] = kv.value().get<double>();
            }
        }

        const json& ent = (*sec)["entities"];
        if (!ent.is_array()) return;

        for (const auto& e : ent) {
            if (!e.is_object()) continue;
            const std::string name = e.value("name", "");
            const std::string eq = e.value("equation", "");
            if (name.empty() || eq.empty()) continue;

            // Build eval env = base + staged + entity.env
            json env = json::object();
            for (auto& kv : envChain) env[kv.first] = kv.second;
            if (e.contains("env") && e["env"].is_object()) {
                for (auto& kv : e["env"].items()) env[kv.key()] = kv.value();
            }

            auto res = TGDKLoader::ResolveEquation(eq, env);
            if (res.ok) {
                _staged[name] = res.value;
                envChain[name] = res.value; // allow chaining
            }
            else {
                MBLOGW("CompoundLoader: '%s' failed: %s", name.c_str(), res.error.c_str());
            }
        }
    }

    void CompoundLoader::Apply() {
        std::lock_guard<std::mutex> lk(_mx);
        _values = _staged;
    }

    nlohmann::json CompoundLoader::Snapshot() const {
        std::lock_guard<std::mutex> lk(_mx);
        json j = json::object();
        for (auto& kv : _values) j[kv.first] = kv.second;
        return j;
    }

    void CompoundLoader::Reset() {
        std::lock_guard<std::mutex> lk(_mx);
        _staged.clear();
        _values.clear();
    }

    std::optional<double> CompoundLoader::Get(std::string_view entity) const {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _values.find(std::string(entity));
        if (it == _values.end()) return std::nullopt;
        return it->second;
    }

    // -------------------------------------------------
    // ImpoundLoader
    // -------------------------------------------------

    static bool charMatch(char t, char p) {
        return p == '?' || t == p;
    }

    bool ImpoundLoader::MatchLike(const std::string& text, const std::string& pattern) {
        // Simple glob with * and ?
        size_t ti = 0, pi = 0, star = std::string::npos, mark = 0;
        while (ti < text.size()) {
            if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) { ++ti; ++pi; }
            else if (pi < pattern.size() && pattern[pi] == '*') { star = pi++; mark = ti; }
            else if (star != std::string::npos) { pi = star + 1; ti = ++mark; }
            else return false;
        }
        while (pi < pattern.size() && pattern[pi] == '*') ++pi;
        return pi == pattern.size();
    }

    void ImpoundLoader::Configure(const json& cfg, const LoaderContext&) {
        std::lock_guard<std::mutex> lk(_mx);
        _stagedItems.clear();
        _stagedRules.clear();

        auto it = cfg.find("impound");
        if (it == cfg.end() || !it->is_object()) return;

        if (it->contains("items") && (*it)["items"].is_array()) {
            for (const auto& s : (*it)["items"]) {
                if (s.is_string()) _stagedItems.push_back(s.get<std::string>());
            }
        }
        if (it->contains("rules") && (*it)["rules"].is_array()) {
            for (const auto& r : (*it)["rules"]) {
                if (!r.is_object()) continue;
                Rule rr;
                rr.tag = r.value("tag", "");
                rr.pattern = r.value("match", "");
                if (!rr.pattern.empty()) _stagedRules.push_back(std::move(rr));
            }
        }
    }

    void ImpoundLoader::Apply() {
        std::lock_guard<std::mutex> lk(_mx);
        _items = _stagedItems;
        _rules = _stagedRules;
    }

    nlohmann::json ImpoundLoader::Snapshot() const {
        std::lock_guard<std::mutex> lk(_mx);
        json j = json::object();
        j["items"] = json::array();
        for (auto& s : _items) j["items"].push_back(s);
        j["rules"] = json::array();
        for (auto& r : _rules) j["rules"].push_back(json{ {"tag", r.tag}, {"match", r.pattern} });
        return j;
    }

    void ImpoundLoader::Reset() {
        std::lock_guard<std::mutex> lk(_mx);
        _stagedItems.clear(); _items.clear();
        _stagedRules.clear(); _rules.clear();
    }

    bool ImpoundLoader::IsImpounded(const std::string& name) const {
        std::lock_guard<std::mutex> lk(_mx);
        for (const auto& s : _items) if (s == name) return true;
        for (const auto& r : _rules) if (MatchLike(name, r.pattern)) return true;
        return false;
    }

    // -------------------------------------------------
    // VolumetricPhiLoader
    // -------------------------------------------------

    static float clamp01f(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    void VolumetricPhiLoader::Configure(const json& cfg, const LoaderContext& ctx) {
        (void)ctx;
        std::lock_guard<std::mutex> lk(_mx);
        _staged = Params{}; // defaults

        auto it = cfg.find("volumetricPhi");
        if (it == cfg.end() || !it->is_object()) return;

        const json& j = *it;
        _staged.enabled = j.value("enabled", true);
        _staged.distanceMul = std::max(0.0f, j.value("distanceMul", 1.0f));
        _staged.densityMul = std::max(0.0f, j.value("densityMul", 1.0f));
        _staged.horizonFade = clamp01f(j.value("horizonFade", 0.25f));
        _staged.jitterStrength = std::max(0.0f, j.value("jitterStrength", 1.0f));
        _staged.temporalBlend = clamp01f(j.value("temporalBlend", 0.90f));
    }

    void VolumetricPhiLoader::Apply() {
        std::lock_guard<std::mutex> lk(_mx);
        _live = _staged;
    }

    nlohmann::json VolumetricPhiLoader::Snapshot() const {
        std::lock_guard<std::mutex> lk(_mx);
        return json{
            {"enabled",        _live.enabled},
            {"distanceMul",    _live.distanceMul},
            {"densityMul",     _live.densityMul},
            {"horizonFade",    _live.horizonFade},
            {"jitterStrength", _live.jitterStrength},
            {"temporalBlend",  _live.temporalBlend}
        };
    }

    void VolumetricPhiLoader::Reset() {
        std::lock_guard<std::mutex> lk(_mx);
        _staged = Params{};
        _live = Params{};
    }

    VolumetricPhiLoader::Params VolumetricPhiLoader::Get() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _live;
    }

} // namespace MB
