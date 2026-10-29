#include "preprocessor.hpp"
#include "language.hpp"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <map>
#include <set>
namespace glsl_sksl {
namespace {
constexpr std::size_t kMaxExpandedTokens=1000000;
constexpr int kMaxExpansionDepth=64;
constexpr int kMaxIfDepth=128;

struct Macro { bool function=false, predefined=false; std::vector<std::string> params; std::vector<Token> body; Loc loc; };
struct Line { std::vector<Token> tokens; Loc end; };
struct Group { bool active, taken, sawElse, parentActive; Loc loc; };

std::string spell(const std::vector<Token>& tokens, std::size_t from) {
    std::string s;
    for(std::size_t i=from;i<tokens.size();++i){if(i>from && tokens[i].spaceBefore)s+=' ';s+=tokens[i].text;}
    return s;
}
std::string where(const Loc& l){return std::to_string(l.line)+":"+std::to_string(l.column);}
bool punct(const Token* t, const char* text){return t && t->kind==TokenKind::Punct && t->text==text;}
Token number(const Token& at, long long value) {
    Token t=at; t.kind=TokenKind::Number; t.text=std::to_string(value); return t;
}

// #if expressions: 32-bit two's-complement integers, C operators without ?: and comma.
class IfEvaluator {
public:
    IfEvaluator(const std::vector<Token>& t, int version, Loc at, DiagnosticSink& sink):t(t),version(version),at(at),sink(sink){}
    bool run() {
        if(t.empty()){fail(at,"Invalid #if: missing expression");return false;}
        const std::int32_t v=binary(1,true);
        if(!failed && i<t.size())fail(t[i].loc,"Invalid #if expression: unexpected '"+t[i].text+"'");
        return !failed && v!=0;
    }
private:
    const std::vector<Token>& t; int version; Loc at; DiagnosticSink& sink;
    std::size_t i=0; int depth=0; bool failed=false;
    static std::int32_t wrap(std::int64_t v){return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)));}
    void fail(Loc l, const std::string& message, const std::string& suggestion={}) {
        if(!failed)sink.error(l,"pp.if-expression",message,suggestion);
        failed=true;
    }
    const Token* peek() const {return i<t.size()?&t[i]:nullptr;}
    static int precedence(const Token* tok) {
        if(!tok || tok->kind!=TokenKind::Punct)return 0;
        static const std::map<std::string,int> table={{"||",1},{"&&",2},{"|",3},{"^",4},{"&",5},{"==",6},{"!=",6},
            {"<",7},{">",7},{"<=",7},{">=",7},{"<<",8},{">>",8},{"+",9},{"-",9},{"*",10},{"/",10},{"%",10}};
        const auto it=table.find(tok->text);
        return it==table.end()?0:it->second;
    }
    std::int32_t primary(bool eval) {
        const Token* tok=peek();
        if(!tok){fail(t.back().loc,"Invalid #if expression: unexpected end of line");return 0;}
        ++i;
        if(punct(tok,"(")) {
            if(++depth>kMaxIfDepth){fail(tok->loc,"Unsupported #if expression: nesting is deeper than "+std::to_string(kMaxIfDepth));return 0;}
            const std::int32_t v=binary(1,eval);
            --depth;
            if(!punct(peek(),")"))fail(peek()?peek()->loc:tok->loc,"Invalid #if expression: expected ')'");else ++i;
            return v;
        }
        if(tok->kind==TokenKind::Number) {
            const auto n=parseNumber(tok->text,version);
            if(n.kind==NumberLiteral::Int||n.kind==NumberLiteral::Uint)return wrap(static_cast<std::int64_t>(n.intValue));
            fail(tok->loc,"Invalid #if expression: '"+tok->text+"' is not an integer constant");return 0;
        }
        if(tok->kind==TokenKind::Identifier) {
            if(tok->text=="defined")fail(tok->loc,"Invalid #if expression: 'defined' produced by macro expansion");
            else fail(tok->loc,"Invalid #if expression: '"+tok->text+"' is not a defined macro",
                "GLSL ES does not treat undefined identifiers as 0 in #if; test them with defined("+tok->text+")");
            return 0;
        }
        fail(tok->loc,"Invalid #if expression: unexpected '"+tok->text+"'");
        return 0;
    }
    std::int32_t unary(bool eval) {
        const Token* tok=peek();
        if(tok && tok->kind==TokenKind::Punct && (tok->text=="+"||tok->text=="-"||tok->text=="~"||tok->text=="!")) {
            ++i;
            if(++depth>kMaxIfDepth){fail(tok->loc,"Unsupported #if expression: nesting is deeper than "+std::to_string(kMaxIfDepth));return 0;}
            const std::int32_t v=unary(eval);
            --depth;
            if(tok->text=="-")return wrap(-static_cast<std::int64_t>(v));
            if(tok->text=="~")return ~v;
            if(tok->text=="!")return !v;
            return v;
        }
        return primary(eval);
    }
    std::int32_t binary(int minimum, bool eval) {
        std::int32_t l=unary(eval);
        for(;;) {
            const Token* tok=peek();
            const int p=precedence(tok);
            if(!p || p<minimum || failed)return l;
            const std::string op=tok->text; const Loc opLoc=tok->loc;
            ++i;
            const bool evalRight=eval && !(op=="&&" && !l) && !(op=="||" && l);
            const std::int32_t r=binary(p+1,evalRight);
            l=apply(op,l,r,evalRight,opLoc);
        }
    }
    std::int32_t apply(const std::string& op, std::int32_t l, std::int32_t r, bool eval, Loc opLoc) {
        const std::int64_t a=l, b=r;
        if(op=="*")return wrap(a*b);
        if(op=="/"||op=="%") {
            if(r==0){if(eval)fail(opLoc,"Invalid #if expression: division by zero");return 0;}
            if(l==INT32_MIN && r==-1)return op=="/"?INT32_MIN:0;
            return op=="/"?l/r:l%r;
        }
        if(op=="+")return wrap(a+b);
        if(op=="-")return wrap(a-b);
        if(op=="<<"||op==">>") {
            if(r<0||r>31){if(eval)fail(opLoc,"Invalid #if expression: shift count "+std::to_string(r)+" is out of range");return 0;}
            return op=="<<"?wrap(static_cast<std::int64_t>(static_cast<std::uint32_t>(l)<<r)):(l>>r);
        }
        if(op=="<")return l<r;
        if(op==">")return l>r;
        if(op=="<=")return l<=r;
        if(op==">=")return l>=r;
        if(op=="==")return l==r;
        if(op=="!=")return l!=r;
        if(op=="&")return l&r;
        if(op=="^")return l^r;
        if(op=="|")return l|r;
        if(op=="&&")return l&&r;
        return l||r;
    }
};

class Preprocessor {
public:
    Preprocessor(const LexOutput& lexed, DiagnosticSink& sink):lexed(lexed),sink(sink){}
    PreprocessOutput run() {
        std::vector<Line> lines;
        const auto& in=lexed.tokens;
        for(std::size_t pos=0;pos<in.size();) {
            Line l;
            while(in[pos].kind!=TokenKind::Newline)l.tokens.push_back(in[pos++]);
            l.end=in[pos++].loc;
            if(!l.tokens.empty())lines.push_back(std::move(l));
        }
        std::size_t first=0;
        if(!lines.empty() && isDirective(lines[0],"version")){version(lines[0]);first=1;}
        definePredefined();
        for(std::size_t i=first;i<lines.size();++i) {
            if(punct(&lines[i].tokens[0],"#")){flush();directive(lines[i]);}
            else if(active())pending.insert(pending.end(),lines[i].tokens.begin(),lines[i].tokens.end());
        }
        flush();
        if(!groups.empty())sink.error(groups.back().loc,"pp.unterminated-conditional","Invalid preprocessor conditional: #if without #endif","add #endif");
        return std::move(out);
    }
private:
    const LexOutput& lexed; DiagnosticSink& sink;
    PreprocessOutput out;
    std::map<std::string,Macro> macros;
    std::vector<Group> groups;
    std::vector<Token> pending;
    long long lineDelta=0; long long fileNumber=0;
    std::size_t produced=0; bool exhausted=false;

    bool active() const {return groups.empty()||groups.back().active;}
    static bool isDirective(const Line& l, const char* name) {
        return l.tokens.size()>1 && punct(&l.tokens[0],"#") && l.tokens[1].kind==TokenKind::Identifier && l.tokens[1].text==name;
    }
    void unexpected(const Line& l, std::size_t from, const std::string& directive) {
        if(l.tokens.size()>from)
            sink.error(l.tokens[from].loc,"pp.extra-tokens","Invalid #"+directive+": unexpected '"+l.tokens[from].text+"' after the directive","remove the extra text or turn it into a comment");
    }

    void version(const Line& l) {
        const auto& t=l.tokens; const Loc loc=t[0].loc;
        out.versionDeclared=true;
        if(t.size()<3 || t[2].kind!=TokenKind::Number) {
            sink.error(loc,"version.invalid","Invalid #version directive: expected a version number","use #version 300 es");
            return;
        }
        const std::string& number=t[2].text;
        const std::string profile=t.size()>3?t[3].text:"";
        static const std::set<std::string> desktop={"110","120","130","140","150","330","400","410","420","430","440","450","460"};
        if(number=="100") {
            out.version=100;
            unexpected(l,3,"version 100");
        } else if(number=="300"||number=="310"||number=="320") {
            out.version=std::stoi(number);
            if(profile=="es")unexpected(l,4,"version");
            else if(profile.empty())
                sink.error(loc,"version.missing-profile","Invalid #version "+number+": GLSL ES "+number.substr(0,1)+"."+number.substr(1)+" requires the 'es' profile","write #version "+number+" es");
            else sink.error(t[3].loc,"version.invalid-profile","Invalid #version profile '"+profile+"': expected 'es'","write #version "+number+" es");
        } else if(desktop.count(number)) {
            out.version=320;
            sink.error(loc,"version.desktop","Unsupported #version "+spell(t,2)+": desktop GLSL is not supported; only GLSL ES 1.00, 3.00, 3.10 and 3.20 are",
                "port the shader to GLSL ES and declare #version 300 es");
        } else sink.error(t[2].loc,"version.invalid","Invalid #version "+number+": not a GLSL ES version","use 100, 300 es, 310 es or 320 es");
    }

    void definePredefined() {
        auto define=[&](const char* name, long long value) {
            Macro m; m.predefined=true;
            if(value>=0){Token t; t.kind=TokenKind::Number; t.text=std::to_string(value); m.body.push_back(t);}
            macros[name]=m;
        };
        define("__LINE__",-1); define("__FILE__",-1);
        define("__VERSION__",out.version); define("GL_ES",1); define("GL_FRAGMENT_PRECISION_HIGH",1);
        // Derivatives are emulated (see emitter.cpp), so the GLSL ES 1.00 extension that provides them is available.
        if(out.version==100)define("GL_OES_standard_derivatives",1);
    }

    bool checkName(const Token& name, const std::string& directive) {
        const std::string& n=name.text;
        if(n=="defined"){sink.error(name.loc,"pp.reserved-macro","Invalid #"+directive+": 'defined' cannot be a macro name");return false;}
        const auto it=macros.find(n);
        if(it!=macros.end() && it->second.predefined) {
            sink.error(name.loc,"pp.reserved-macro","Invalid #"+directive+": '"+n+"' is a predefined macro");return false;
        }
        if(n.rfind("GL_",0)==0) {
            sink.error(name.loc,"pp.reserved-macro","Invalid #"+directive+": macro names beginning with 'GL_' are reserved","rename the macro");return false;
        }
        if(n.find("__")!=std::string::npos) {
            if(out.version<300){sink.error(name.loc,"pp.reserved-macro","Invalid #"+directive+": macro names containing '__' are reserved in GLSL ES 1.00","rename the macro");return false;}
            sink.warning(name.loc,"pp.reserved-macro","macro name '"+n+"' contains '__', which GLSL ES reserves for the implementation","rename the macro");
        }
        return true;
    }

    void directive(const Line& l) {
        const auto& t=l.tokens;
        if(t.size()==1)return;
        const std::string d=t[1].kind==TokenKind::Identifier?t[1].text:"";
        if(!active()) {
            if(d=="if"||d=="ifdef"||d=="ifndef")groups.push_back({false,true,false,false,t[0].loc});
            else if(d=="elif"||d=="else"||d=="endif")conditional(d,l);
            return;
        }
        if(d=="define")define(l);
        else if(d=="undef")undef(l);
        else if(d=="if")groups.push_back(open(evaluate(l),t[0].loc));
        else if(d=="ifdef"||d=="ifndef") {
            if(t.size()<3 || t[2].kind!=TokenKind::Identifier){sink.error(t[1].loc,"pp.syntax","Invalid #"+d+": expected a macro name");groups.push_back(open(false,t[0].loc));return;}
            unexpected(l,3,d);
            const bool defined=macros.count(t[2].text)!=0;
            groups.push_back(open(d=="ifdef"?defined:!defined,t[0].loc));
        }
        else if(d=="elif"||d=="else"||d=="endif")conditional(d,l);
        else if(d=="error")sink.error(t[0].loc,"pp.error","#error "+spell(t,2),"the shader requested this compilation failure");
        else if(d=="pragma")out.pragmas.push_back(spell(t,2));
        else if(d=="extension")extension(l);
        else if(d=="version")
            sink.error(t[0].loc,"version.misplaced","Invalid #version: it must be the first line of the shader and appear only once",
                "move #version to the very first line; only comments and whitespace may precede it");
        else if(d=="line")line(l);
        else sink.error(t[1].loc,"pp.unknown-directive","Invalid preprocessor directive '#"+t[1].text+"'");
    }

    static Group open(bool value, Loc loc){return {value,value,false,true,loc};}

    void conditional(const std::string& d, const Line& l) {
        if(groups.empty()){sink.error(l.tokens[0].loc,"pp.unbalanced-conditional","Invalid #"+d+" without a matching #if");return;}
        Group& g=groups.back();
        if(d=="endif"){if(g.parentActive)unexpected(l,2,d);groups.pop_back();return;}
        if(g.sawElse){sink.error(l.tokens[0].loc,"pp.unbalanced-conditional","Invalid #"+d+" after #else in the same conditional");return;}
        if(d=="else"){if(g.parentActive)unexpected(l,2,d);g.sawElse=true;g.active=g.parentActive && !g.taken;g.taken=true;return;}
        if(g.taken||!g.parentActive){g.active=false;return;}
        g.active=g.taken=evaluate(l);
    }

    bool evaluate(const Line& l) {
        const auto& t=l.tokens;
        std::vector<Token> expression;
        for(std::size_t i=2;i<t.size();++i) {
            if(t[i].kind!=TokenKind::Identifier || t[i].text!="defined"){expression.push_back(t[i]);continue;}
            const bool parens=i+1<t.size() && punct(&t[i+1],"(");
            const std::size_t name=i+(parens?2:1);
            if(name>=t.size() || t[name].kind!=TokenKind::Identifier || (parens && (name+1>=t.size() || !punct(&t[name+1],")")))) {
                sink.error(t[i].loc,"pp.if-expression","Invalid #if expression: 'defined' must be followed by a macro name","write defined(NAME) or defined NAME");
                return false;
            }
            expression.push_back(number(t[i],macros.count(t[name].text)?1:0));
            i=name+(parens?1:0);
        }
        const auto expanded=expand(std::move(expression),0);
        if(exhausted)return false;
        return IfEvaluator(expanded,out.version,t[1].loc,sink).run();
    }

    static bool same(const Macro& a, const Macro& b) {
        if(a.function!=b.function || a.params!=b.params || a.body.size()!=b.body.size())return false;
        for(std::size_t i=0;i<a.body.size();++i)
            if(a.body[i].text!=b.body[i].text || (i && a.body[i].spaceBefore!=b.body[i].spaceBefore))return false;
        return true;
    }

    void define(const Line& l) {
        const auto& t=l.tokens;
        if(t.size()<3 || t[2].kind!=TokenKind::Identifier){sink.error(t[1].loc,"pp.syntax","Invalid #define: expected a macro name");return;}
        if(!checkName(t[2],"define"))return;
        Macro m; m.loc=t[2].loc;
        std::size_t i=3;
        if(i<t.size() && punct(&t[i],"(") && !t[i].spaceBefore) {
            m.function=true; ++i;
            if(i<t.size() && punct(&t[i],")"))++i;
            else for(;;) {
                if(i>=t.size() || t[i].kind!=TokenKind::Identifier){sink.error(i<t.size()?t[i].loc:t[2].loc,"pp.syntax","Invalid #define: expected a parameter name in macro '"+t[2].text+"'");return;}
                if(std::find(m.params.begin(),m.params.end(),t[i].text)!=m.params.end()){sink.error(t[i].loc,"pp.syntax","Invalid #define: duplicate parameter '"+t[i].text+"'");return;}
                m.params.push_back(t[i++].text);
                if(i<t.size() && punct(&t[i],",")){++i;continue;}
                if(i<t.size() && punct(&t[i],")")){++i;break;}
                sink.error(i<t.size()?t[i].loc:t[2].loc,"pp.syntax","Invalid #define: expected ',' or ')' in the parameters of '"+t[2].text+"'");
                return;
            }
        }
        m.body.assign(t.begin()+static_cast<std::ptrdiff_t>(i),t.end());
        if(!m.body.empty())m.body.front().spaceBefore=false;
        for(const auto& b:m.body) {
            if(punct(&b,"##")){sink.error(b.loc,"pp.token-pasting","Unsupported #define: token pasting (##)","write the pasted identifiers out in full");return;}
            if(punct(&b,"#")){sink.error(b.loc,"pp.stringify","Invalid #define: '#' is not an operator in GLSL macros");return;}
        }
        const auto it=macros.find(t[2].text);
        if(it!=macros.end() && !same(it->second,m)) {
            sink.error(t[2].loc,"pp.macro-redefined","Invalid #define: macro '"+t[2].text+"' is redefined differently (previous definition at "+where(it->second.loc)+")",
                "#undef the macro before redefining it");
            return;
        }
        macros[t[2].text]=std::move(m);
    }

    void undef(const Line& l) {
        const auto& t=l.tokens;
        if(t.size()<3 || t[2].kind!=TokenKind::Identifier){sink.error(t[1].loc,"pp.syntax","Invalid #undef: expected a macro name");return;}
        if(!checkName(t[2],"undef"))return;
        unexpected(l,3,"undef");
        macros.erase(t[2].text);
    }

    void line(const Line& l) {
        const auto& t=l.tokens;
        const auto args=expand(std::vector<Token>(t.begin()+2,t.end()),0);
        auto integer=[&](std::size_t i,long long& value) {
            if(i>=args.size() || args[i].kind!=TokenKind::Number)return false;
            const auto n=parseNumber(args[i].text,out.version);
            value=static_cast<long long>(n.intValue);
            return n.kind==NumberLiteral::Int;
        };
        long long number=0, file=fileNumber;
        if(!integer(0,number) || (args.size()>1 && !integer(1,file)) || args.size()>2) {
            sink.error(t[1].loc,"pp.syntax","Invalid #line: expected a line number and an optional source string number","write #line 100 or #line 100 1");
            return;
        }
        // GLSL ES 1.00 numbers the following line line+1; GLSL ES 3.x numbers it line.
        const long long next=out.version>=300?number:number+1;
        lineDelta=next-static_cast<long long>(l.end.line+1);
        fileNumber=file;
    }

    void extension(const Line& l) {
        const auto& t=l.tokens;
        if(t.size()!=5 || t[2].kind!=TokenKind::Identifier || !punct(&t[3],":") || t[4].kind!=TokenKind::Identifier) {
            sink.error(t[1].loc,"pp.syntax","Invalid #extension: expected 'name : behavior'","write #extension NAME : enable");
            return;
        }
        const std::string& name=t[2].text; const std::string& behavior=t[4].text;
        if(behavior!="require" && behavior!="enable" && behavior!="warn" && behavior!="disable") {
            sink.error(t[4].loc,"pp.extension","Invalid #extension behavior '"+behavior+"'","use require, enable, warn or disable");
            return;
        }
        if(out.version>=300 && !out.tokens.empty())
            sink.warning(t[0].loc,"pp.extension-position","#extension should appear before any non-preprocessor tokens","move it directly after #version");
        out.extensions.push_back({name,behavior,t[0].loc});
        if(name=="all") {
            if(behavior=="require"||behavior=="enable")
                sink.error(t[4].loc,"pp.extension","Invalid #extension all : "+behavior+": only 'warn' and 'disable' apply to all extensions");
            return;
        }
        if(behavior=="disable")return;
        if(!isKnownExtension(name)) {
            if(behavior=="require")sink.error(t[2].loc,"pp.extension","Invalid #extension: unknown extension '"+name+"' is required","check the extension name");
            else sink.warning(t[2].loc,"pp.extension","unknown extension '"+name+"'; the directive has no effect","check the extension name");
            return;
        }
        if(name=="GL_OES_standard_derivatives" && out.version==100)return;
        if(behavior=="require")
            sink.error(t[2].loc,"pp.extension","Unsupported extension '"+name+"': it is not available in SkSL runtime shaders","remove the dependency on the extension");
        else sink.warning(t[2].loc,"pp.extension","extension '"+name+"' is not available in SkSL runtime shaders; it stays disabled",
            "features from this extension are rejected");
    }

    void flush() {
        if(pending.empty())return;
        auto expanded=expand(std::move(pending),0);
        pending.clear();
        for(auto& t:expanded)out.tokens.push_back(std::move(t));
    }

    static bool hidden(const Token& t){return std::find(t.hideSet.begin(),t.hideSet.end(),t.text)!=t.hideSet.end();}
    static void addHidden(std::vector<std::string>& set, const std::string& name) {
        if(std::find(set.begin(),set.end(),name)==set.end())set.push_back(name);
    }

    // Prosser's algorithm: every token carries the set of macros it was produced by.
    std::vector<Token> expand(std::vector<Token> input, int depth) {
        std::vector<Token> work(std::make_move_iterator(input.rbegin()),std::make_move_iterator(input.rend()));
        std::vector<Token> result;
        if(depth>kMaxExpansionDepth) {
            if(!exhausted && !work.empty())sink.error(work.back().loc,"pp.expansion-limit","Unsupported macro use: arguments nest more than "+std::to_string(kMaxExpansionDepth)+" invocations deep");
            exhausted=true;
            return result;
        }
        while(!work.empty() && !exhausted) {
            Token t=std::move(work.back()); work.pop_back();
            const auto it=t.kind==TokenKind::Identifier?macros.find(t.text):macros.end();
            if(it==macros.end() || hidden(t)){result.push_back(std::move(t));continue;}
            const Macro& m=it->second;
            if(t.text=="__LINE__"){result.push_back(number(t,static_cast<long long>(t.loc.line)+lineDelta));result.back().macro=t.text;continue;}
            if(t.text=="__FILE__"){result.push_back(number(t,fileNumber));result.back().macro=t.text;continue;}
            std::vector<std::string> hide=t.hideSet;
            std::vector<Token> body;
            const std::string origin=t.macro.empty()?t.text:t.macro;
            if(m.function) {
                if(work.empty() || !punct(&work.back(),"(")){result.push_back(std::move(t));continue;}
                work.pop_back();
                std::vector<std::vector<Token>> args(1);
                int nesting=0; bool closed=false; Token close;
                while(!work.empty()) {
                    Token a=std::move(work.back()); work.pop_back();
                    if(a.kind==TokenKind::Punct) {
                        if(a.text=="(")++nesting;
                        else if(a.text==")"){if(!nesting){closed=true;close=std::move(a);break;}--nesting;}
                        else if(a.text=="," && !nesting){args.emplace_back();continue;}
                    }
                    args.back().push_back(std::move(a));
                }
                if(!closed) {
                    sink.error(t.loc,"pp.macro-arguments","Invalid use of macro '"+t.text+"': unterminated argument list",
                        "close the argument list; preprocessor directives cannot appear inside macro arguments");
                    return result;
                }
                const std::size_t given=m.params.empty() && args.size()==1 && args[0].empty()?0:args.size();
                if(given!=m.params.size()) {
                    sink.error(t.loc,"pp.macro-arguments","Invalid use of macro '"+t.text+"': expected "+std::to_string(m.params.size())+
                        " argument(s) but got "+std::to_string(given),"defined at "+where(m.loc));
                    continue;
                }
                hide.erase(std::remove_if(hide.begin(),hide.end(),[&](const std::string& h){
                    return std::find(close.hideSet.begin(),close.hideSet.end(),h)==close.hideSet.end();}),hide.end());
                std::vector<std::vector<Token>> expandedArgs;
                for(auto& a:args)expandedArgs.push_back(expand(std::move(a),depth+1));
                if(exhausted)return result;
                for(const auto& b:m.body) {
                    const auto p=b.kind==TokenKind::Identifier?std::find(m.params.begin(),m.params.end(),b.text):m.params.end();
                    if(p==m.params.end()){body.push_back(b);body.back().loc=t.loc;continue;}
                    const auto& arg=expandedArgs[static_cast<std::size_t>(p-m.params.begin())];
                    for(std::size_t k=0;k<arg.size();++k){body.push_back(arg[k]);if(!k)body.back().spaceBefore=b.spaceBefore;}
                }
            } else {
                body=m.body;
                for(auto& b:body)b.loc=t.loc;
            }
            addHidden(hide,t.text);
            for(auto& b:body){for(const auto& h:hide)addHidden(b.hideSet,h);if(b.macro.empty())b.macro=origin;}
            if(!body.empty())body.front().spaceBefore=t.spaceBefore;
            produced+=body.size();
            if(produced>kMaxExpandedTokens) {
                sink.error(t.loc,"pp.expansion-limit","Unsupported macro use: expansion produces more than "+std::to_string(kMaxExpandedTokens)+" tokens");
                exhausted=true;
                return result;
            }
            for(auto b=body.rbegin();b!=body.rend();++b)work.push_back(std::move(*b));
        }
        return result;
    }
};
}

PreprocessOutput preprocess(const LexOutput& lexed, DiagnosticSink& sink) {
    return Preprocessor(lexed,sink).run();
}

std::vector<Token> classify(PreprocessOutput& pp, const LexOutput& lexed, DiagnosticSink& sink) {
    const int version=pp.version;
    if(version<300 && !lexed.continuations.empty())
        sink.error(lexed.continuations.front(),"lex.line-continuation","Invalid line continuation: '\\' at the end of a line requires GLSL ES 3.00",
            "join the lines or declare #version 300 es");
    static const std::set<std::string> reservedOperators={"%","<<",">>","&","|","^","~","%=","<<=",">>=","&=","|=","^="};
    std::vector<Token> tokens=std::move(pp.tokens);
    for(auto& t:tokens) {
        const std::string context=t.macro.empty()?"":" (in expansion of macro '"+t.macro+"')";
        t.hideSet.clear();
        switch(t.kind) {
        case TokenKind::Invalid:
            sink.error(t.loc,"lex.invalid-character",t.invalidReason+context,"remove the character");
            break;
        case TokenKind::Identifier: {
            const auto word=classifyWord(t.text,version);
            if(word==WordClass::Keyword)t.kind=TokenKind::Keyword;
            else if(word==WordClass::Reserved)
                sink.error(t.loc,"lex.reserved-word","Invalid use of reserved word '"+t.text+"' in "+versionName(version)+context,"rename the identifier");
            break;
        }
        case TokenKind::Number: {
            const auto n=parseNumber(t.text,version);
            if(n.kind==NumberLiteral::Invalid){sink.error(t.loc,"lex.invalid-number",n.message+context);break;}
            t.kind=n.kind==NumberLiteral::Float?TokenKind::FloatLiteral:n.kind==NumberLiteral::Uint?TokenKind::UintLiteral:TokenKind::IntLiteral;
            t.text=n.spelling; t.intValue=n.intValue; t.floatValue=n.floatValue;
            if(n.overflow)
                sink.error(t.loc,"lex.float-range","Unsupported floating-point literal '"+t.text+"': it is outside the range of a 32-bit float"+context,
                    "use a value within +/-3.4e38");
            break;
        }
        case TokenKind::Punct:
            if(t.text=="#"||t.text=="##")
                sink.error(t.loc,"lex.misplaced-hash","Invalid '"+t.text+"': preprocessor directives must start at the beginning of a line"+context,
                    "move the directive onto its own line");
            else if(version<300 && reservedOperators.count(t.text))
                sink.error(t.loc,"lex.reserved-operator","Invalid use of operator '"+t.text+"': it is reserved in GLSL ES 1.00"+context,
                    t.text[0]=='%'?"use mod() or declare #version 300 es":"declare #version 300 es");
            break;
        default: break;
        }
    }
    Token end; end.kind=TokenKind::End;
    end.loc=tokens.empty()?Loc{1,1}:tokens.back().loc;
    tokens.push_back(std::move(end));
    return tokens;
}
}
