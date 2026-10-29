#include "parser.hpp"
#include "language.hpp"
#include <algorithm>
#include <map>
namespace glsl_sksl {
using namespace ast;
namespace {
// Nested statements, parentheses, arguments and unary operators. Recursion costs a few KB of stack
// per level in unoptimised builds; 128 levels stay well inside a 1 MB stack, and SkSL itself
// rejects nesting deeper than 50.
constexpr int kMaxNesting=128;
constexpr unsigned kMaxHeight=256;      // expression tree height, so later recursive passes stay bounded
constexpr unsigned kMaxErrors=25;
#ifdef _MSC_VER
#define GLSL_SKSL_COLD __declspec(noinline)
#else
#define GLSL_SKSL_COLD __attribute__((noinline,cold))
#endif

struct ParseError {};

std::string describe(const Token& t) {
    switch(t.kind) {
    case TokenKind::End: return "end of input";
    case TokenKind::Keyword: return "keyword '"+t.text+"'";
    default: return "'"+t.text+"'";
    }
}
std::string where(Loc l){return std::to_string(l.line)+":"+std::to_string(l.column);}

class Parser {
public:
    Parser(const std::vector<Token>& tokens, int version, DiagnosticSink& sink):t(tokens),version(version),sink(sink){scopes.emplace_back();}

    std::unique_ptr<TranslationUnit> run() {
        auto unit=std::make_unique<TranslationUnit>();
        unit->version=version;
        while(peek().kind!=TokenKind::End) {
            const std::size_t start=i;
            try {
                if(is(";"))fail(peek(),"Invalid syntax: unexpected ';' at global scope","remove the extra ';'","syntax.stray-semicolon");
                unit->decls.push_back(declaration(true));
            } catch(const ParseError&) {
                if(++errors>=kMaxErrors)break;
                recoverGlobal(start);
            }
        }
        return unit;
    }

private:
    const std::vector<Token>& t; int version; DiagnosticSink& sink;
    std::size_t i=0;
    int depth=0, loops=0, switches=0;
    unsigned errors=0;
    std::vector<std::map<std::string,bool>> scopes;   // name -> declared as a type

    struct Nest {
        Parser& p;
        Nest(Parser& p, const Token& at):p(p) {
            if(++p.depth>kMaxNesting){--p.depth;p.tooDeep(at.loc);}
        }
        ~Nest(){--p.depth;}
    };
    struct Scope {
        Parser* p;
        explicit Scope(Parser* p):p(p){if(p)p->scopes.emplace_back();}
        ~Scope(){if(p)p->scopes.pop_back();}
    };
    struct Count {
        int& n;
        explicit Count(int& n):n(n){++n;}
        ~Count(){--n;}
    };

    const Token& peek(std::size_t k=0) const {return t[std::min(i+k,t.size()-1)];}
    bool is(const char* p, std::size_t k=0) const {const Token& x=peek(k);return x.kind==TokenKind::Punct && x.text==p;}
    bool isKeyword(const char* w, std::size_t k=0) const {const Token& x=peek(k);return x.kind==TokenKind::Keyword && x.text==w;}
    const Token& next() {const Token& x=t[i];if(i+1<t.size())++i;return x;}
    bool accept(const char* p){if(!is(p))return false;next();return true;}
    const Token& previous() const {return t[i?i-1:0];}

    [[noreturn]] void fail(Loc at, std::string message, std::string suggestion={}, std::string code="syntax.unexpected-token") {
        sink.error(at,std::move(code),std::move(message),std::move(suggestion));
        throw ParseError{};
    }
    [[noreturn]] void fail(const Token& at, std::string message, std::string suggestion={}, std::string code="syntax.unexpected-token") {
        fail(at.loc,std::move(message),std::move(suggestion),std::move(code));
    }
    void report(const Token& at, std::string message, std::string suggestion, std::string code) {
        sink.error(at.loc,std::move(code),std::move(message),std::move(suggestion));
    }
    // Error paths of the recursive functions live in these out-of-line helpers so that the
    // recursive frames do not reserve stack for message strings.
    [[noreturn]] GLSL_SKSL_COLD void tooDeep(Loc at) {
        fail(at,"Unsupported nesting: more than "+std::to_string(kMaxNesting)+" nested statements or expressions",
            "split the code into helper functions or temporary variables","syntax.nesting-limit");
    }
    [[noreturn]] GLSL_SKSL_COLD void expected(const char* p, const char* context) {
        std::string suggestion;
        if(std::string(p)==";" && i)suggestion="add ';' after "+describe(previous());
        fail(peek(),"Invalid syntax: expected '"+std::string(p)+"' "+context+", found "+describe(peek()),suggestion,"syntax.expected-token");
    }
    [[noreturn]] GLSL_SKSL_COLD void expectedExpression(const Token& x) {
        fail(x,"Invalid syntax: expected an expression, found "+describe(x),{},"syntax.expected-expression");
    }
    [[noreturn]] GLSL_SKSL_COLD void expectedField(const Token& x) {
        fail(x,"Invalid syntax: expected a field name or swizzle after '.', found "+describe(x),{},"syntax.expected-name");
    }
    [[noreturn]] GLSL_SKSL_COLD void expectedConstructorCall(const TypeSpec& type) {
        fail(peek(),"Invalid syntax: expected '(' after type '"+type.name+"', found "+describe(peek()),
            "a type can only appear in an expression as a constructor call","syntax.expected-token");
    }
    [[noreturn]] GLSL_SKSL_COLD void misplacedLabel(const Token& x) {
        fail(x,"Invalid '"+x.text+"' label: it must appear directly in the body of a switch statement",
            "labels cannot be outside a switch or nested inside other blocks or statements within it","syntax.misplaced-label");
    }
    GLSL_SKSL_COLD void invalidAssignment(const Token& op) {
        report(op,"Invalid assignment: the left side of '"+op.text+"' is not a variable, element, field or swizzle",
            "assign to a variable, array element, struct field or swizzle","syntax.invalid-assignment");
    }
    [[noreturn]] GLSL_SKSL_COLD void tooTall(Loc at) {
        fail(at,"Unsupported expression: it nests more than "+std::to_string(kMaxHeight)+" operations deep",
            "split it using temporary variables","syntax.nesting-limit");
    }
    const Token& expect(const char* p, const char* context) {
        if(is(p))return next();
        expected(p,context);
    }
    const Token& expect(const char* p, const std::string& context){return expect(p,context.c_str());}
    const Token& expectName(const std::string& context) {
        const Token& x=peek();
        if(x.kind==TokenKind::Identifier)return next();
        if(x.kind==TokenKind::Keyword)
            fail(x,"Invalid syntax: expected a name "+context+", found "+describe(x),"'"+x.text+"' is a keyword; choose another name","syntax.expected-name");
        fail(x,"Invalid syntax: expected a name "+context+", found "+describe(x),{},"syntax.expected-name");
    }

    bool isTypeName(const Token& x) const {
        if(x.kind!=TokenKind::Identifier)return false;
        for(auto s=scopes.rbegin();s!=scopes.rend();++s) {
            const auto f=s->find(x.text);
            if(f!=s->end())return f->second;
        }
        return false;
    }
    bool startsType(std::size_t k=0) const {
        const Token& x=peek(k);
        return (x.kind==TokenKind::Keyword && (isBuiltinTypeName(x.text)||x.text=="struct")) || isTypeName(x);
    }
    void declare(const std::string& name, Loc loc, bool type) {
        if(name.rfind("gl_",0)==0)
            sink.error(loc,"syntax.reserved-name","Invalid declaration of '"+name+"': names beginning with 'gl_' are reserved","rename it");
        else if(name.find("__")!=std::string::npos) {
            if(version<300)sink.error(loc,"syntax.reserved-name","Invalid declaration of '"+name+"': names containing '__' are reserved in GLSL ES 1.00","rename it");
            else sink.warning(loc,"syntax.reserved-name","'"+name+"' contains '__', which GLSL ES reserves for the implementation","rename it");
        }
        scopes.back()[name]=type;
    }

    // Skips to the end of the current external declaration after an error.
    void recoverGlobal(std::size_t start) {
        int braces=0;
        for(std::size_t j=start;j<i;++j)
            if(t[j].kind==TokenKind::Punct)braces+=t[j].text=="{"?1:t[j].text=="}"?-1:0;
        if(i==start)next();
        while(peek().kind!=TokenKind::End) {
            const Token& x=next();
            if(x.kind!=TokenKind::Punct)continue;
            if(x.text=="{")++braces;
            else if(x.text=="}" && --braces<=0){accept(";");return;}
            else if(x.text==";" && braces<=0)return;
        }
    }
    // Skips to the end of the current statement, stopping before the '}' that closes the block.
    void recoverStatement(std::size_t start) {
        int nesting=0;
        auto adjust=[&](const Token& x) {
            if(x.kind!=TokenKind::Punct)return;
            if(x.text=="("||x.text=="["||x.text=="{")++nesting;
            else if(x.text==")"||x.text=="]"||x.text=="}")nesting=std::max(0,nesting-1);
        };
        for(std::size_t j=start;j<i;++j)adjust(t[j]);
        bool moved=i>start;
        while(peek().kind!=TokenKind::End) {
            if(!nesting && is("}") && moved)return;
            const Token& x=next(); moved=true;
            const bool closesBlock=x.kind==TokenKind::Punct && x.text=="}";
            adjust(x);
            if(!nesting && x.kind==TokenKind::Punct && (x.text==";"||closesBlock))return;
        }
    }

    // ---------------------------------------------------------------- declarations
    Qualifiers qualifiers() {
        Qualifiers q;
        while(peek().kind==TokenKind::Keyword && isQualifierKeyword(peek().text)) {
            const Token& w=next();
            Qualifier x; x.word=w.text; x.loc=w.loc;
            if(w.text=="layout") {
                expect("(","after 'layout'");
                do {
                    const Token& id=peek();
                    if(id.kind!=TokenKind::Identifier && !(id.kind==TokenKind::Keyword && id.text=="shared"))
                        fail(id,"Invalid layout qualifier: expected a layout identifier, found "+describe(id),{},"syntax.layout");
                    next();
                    LayoutId l; l.name=id.text; l.loc=id.loc;
                    if(accept("="))l.value=conditional();
                    x.layout.push_back(std::move(l));
                } while(accept(","));
                expect(")","to close the layout qualifier");
            }
            q.list.push_back(std::move(x));
        }
        return q;
    }

    std::vector<ArraySize> arrays() {
        std::vector<ArraySize> result;
        while(is("[")) {
            ArraySize a; a.loc=next().loc;
            if(!is("]"))a.size=conditional();
            expect("]","to close the array size");
            result.push_back(std::move(a));
        }
        return result;
    }

    TypeSpec typeSpecifier(bool allowArrays) {
        TypeSpec type; type.loc=peek().loc;
        const Token& x=peek();
        if(isKeyword("struct")){type.structDef=structSpecifier();type.name=type.structDef->name;}
        else if((x.kind==TokenKind::Keyword && isBuiltinTypeName(x.text)) || isTypeName(x))type.name=next().text;
        else fail(x,"Invalid syntax: expected a type, found "+describe(x),
            x.kind==TokenKind::Identifier?"'"+x.text+"' is not a declared type":"","syntax.expected-type");
        if(allowArrays)type.arrays=arrays();
        return type;
    }

    FieldGroup fieldGroup(bool block, const std::string& owner) {
        FieldGroup g;
        g.qualifiers=qualifiers();
        if(!block)
            for(const auto& q:g.qualifiers.list)
                if(q.word!="lowp" && q.word!="mediump" && q.word!="highp")
                    fail(q.loc,"Invalid qualifier '"+q.word+"' on a member of "+owner+": struct members only accept precision qualifiers",
                        "remove the qualifier","syntax.member-qualifier");
        if(isKeyword("struct"))
            fail(peek(),"Invalid struct definition inside "+owner+": GLSL ES does not allow nested struct definitions",
                "define the struct at global scope and use its name here","syntax.nested-struct");
        g.type=typeSpecifier(true);
        do {
            const Token& n=expectName("for the member of "+owner);
            Declarator f; f.name=n.text; f.loc=n.loc; f.arrays=arrays();
            if(is("="))fail(peek(),"Invalid initializer on member '"+n.text+"' of "+owner+": members cannot have initializers",
                "initialise the value when constructing it","syntax.member-initializer");
            g.fields.push_back(std::move(f));
        } while(accept(","));
        expect(";","after the member declaration");
        return g;
    }

    std::shared_ptr<StructDecl> structSpecifier() {
        auto s=std::make_shared<StructDecl>();
        s->loc=next().loc;
        if(peek().kind==TokenKind::Identifier){const Token& n=next();s->name=n.text;s->loc=n.loc;}
        const std::string owner=s->name.empty()?"an anonymous struct":"struct '"+s->name+"'";
        expect("{","to begin the body of "+owner);
        if(is("}"))fail(peek(),"Invalid "+owner+": a struct must have at least one member","add a member","syntax.empty-struct");
        while(!is("}") && peek().kind!=TokenKind::End)s->members.push_back(fieldGroup(false,owner));
        expect("}","to close "+owner);
        if(!s->name.empty())declare(s->name,s->loc,true);
        return s;
    }

    DeclPtr interfaceBlock(Qualifiers q) {
        auto b=std::make_unique<InterfaceBlock>();
        const Token& n=next();
        b->qualifiers=std::move(q); b->name=n.text; b->loc=n.loc;
        const std::string owner="block '"+n.text+"'";
        expect("{","to begin "+owner);
        if(is("}"))fail(peek(),"Invalid "+owner+": a block must have at least one member","add a member","syntax.empty-block");
        while(!is("}") && peek().kind!=TokenKind::End)b->members.push_back(fieldGroup(true,owner));
        expect("}","to close "+owner);
        if(peek().kind==TokenKind::Identifier) {
            const Token& instance=next();
            b->instance=instance.text; b->instanceLoc=instance.loc; b->instanceArrays=arrays();
            declare(instance.text,instance.loc,false);
        }
        expect(";","after "+owner);
        auto d=std::make_unique<Decl>(DeclKind::Block,b->loc);
        d->block=std::move(b);
        return d;
    }

    DeclPtr declaration(bool global) {
        const Token& first=peek();
        if(isKeyword("precision")) {
            next();
            auto d=std::make_unique<Decl>(DeclKind::Precision,first.loc);
            const Token& p=peek();
            if(p.kind!=TokenKind::Keyword || (p.text!="lowp" && p.text!="mediump" && p.text!="highp"))
                fail(p,"Invalid precision statement: expected lowp, mediump or highp, found "+describe(p),{},"syntax.precision");
            next();
            d->qualifiers.list.push_back({p.text,{},p.loc});
            d->type=typeSpecifier(false);
            expect(";","after the precision statement");
            return d;
        }
        Qualifiers q=qualifiers();
        if(!q.empty()) {
            if(accept(";")) {
                auto d=std::make_unique<Decl>(DeclKind::Qualifier,first.loc);
                d->qualifiers=std::move(q);
                return d;
            }
            if(peek().kind==TokenKind::Identifier && !isTypeName(peek())) {
                if(is("{",1))return interfaceBlock(std::move(q));
                auto d=std::make_unique<Decl>(DeclKind::Qualifier,first.loc);
                d->qualifiers=std::move(q);
                do {const Token& n=expectName("in the qualifier declaration");d->names.push_back({n.text,n.loc});} while(accept(","));
                expect(";","after the qualifier declaration");
                return d;
            }
        }
        if(!startsType()) {
            if(global)
                fail(peek(),"Invalid syntax: expected a declaration at global scope, found "+describe(peek()),
                    peek().kind==TokenKind::Identifier && !q.empty()?"'"+peek().text+"' is not a declared type":
                    "statements and expressions must be inside a function such as main()","syntax.global-statement");
            fail(peek(),"Invalid syntax: expected a type, found "+describe(peek()),{},"syntax.expected-type");
        }
        TypeSpec type=typeSpecifier(true);
        if(accept(";")) {
            auto d=std::make_unique<Decl>(DeclKind::Variable,first.loc);
            d->qualifiers=std::move(q); d->type=std::move(type);
            return d;
        }
        const Token& name=expectName("in the declaration");
        if(is("("))return function(std::move(q),std::move(type),name,global);
        auto d=std::make_unique<Decl>(DeclKind::Variable,first.loc);
        d->qualifiers=std::move(q); d->type=std::move(type);
        const Token* n=&name;
        for(;;) {
            Declarator v; v.name=n->text; v.loc=n->loc;
            v.arrays=arrays();
            if(accept("="))v.init=assignment();
            declare(v.name,v.loc,false);   // the scope of a variable begins after its initializer
            d->declarators.push_back(std::move(v));
            if(!accept(","))break;
            n=&expectName("in the declaration");
        }
        expect(";","after the declaration");
        return d;
    }

    Param parameter() {
        Param p; p.loc=peek().loc;
        p.qualifiers=qualifiers();
        p.type=typeSpecifier(true);
        if(peek().kind==TokenKind::Identifier) {
            const Token& n=next();
            p.name=n.text; p.loc=n.loc; p.arrays=arrays();
            declare(p.name,p.loc,false);
        }
        return p;
    }

    DeclPtr function(Qualifiers q, TypeSpec returnType, const Token& name, bool global) {
        auto f=std::make_unique<Function>();
        f->returnQualifiers=std::move(q); f->returnType=std::move(returnType);
        f->name=name.text; f->loc=name.loc;
        if(name.text.rfind("gl_",0)==0)
            sink.error(name.loc,"syntax.reserved-name","Invalid declaration of '"+name.text+"': names beginning with 'gl_' are reserved","rename the function");
        Scope parameters(this);
        expect("(","after the function name");
        if(isKeyword("void") && is(")",1)){next();f->voidParams=true;}
        else if(!is(")"))do f->params.push_back(parameter()); while(accept(","));
        expect(")","to close the parameter list of '"+f->name+"'");
        auto d=std::make_unique<Decl>(DeclKind::Function,f->loc);
        if(is("{")) {
            if(!global)fail(peek(),"Invalid definition of '"+f->name+"': functions cannot be defined inside other functions",
                "move the definition to global scope","syntax.nested-function");
            f->body=block(false);
        } else expect(";","after the prototype of '"+f->name+"'");
        d->function=std::move(f);
        return d;
    }

    // ---------------------------------------------------------------- statements
    bool startsDeclaration() const {
        const Token& x=peek();
        if(x.kind==TokenKind::Keyword && (isQualifierKeyword(x.text)||x.text=="precision"||x.text=="struct"))return true;
        if(!startsType())return false;
        std::size_t k=1;
        while(is("[",k)) {
            int nesting=0;
            do {
                if(is("[",k))++nesting;
                else if(is("]",k))--nesting;
                else if(peek(k).kind==TokenKind::End)return true;
                ++k;
            } while(nesting);
        }
        return !is("(",k);   // a type followed by '(' starts a constructor expression
    }

    StmtPtr block(bool scoped) {
        auto s=std::make_unique<Stmt>(StmtKind::Block,expect("{","to begin a block").loc);
        s->scoped=scoped;
        Scope scope(scoped?this:nullptr);
        while(!is("}") && peek().kind!=TokenKind::End) {
            const std::size_t start=i;
            try {s->body.push_back(statement());}
            catch(const ParseError&) {if(++errors>=kMaxErrors)throw;recoverStatement(start);}
        }
        expect("}","to close the block");
        return s;
    }
    StmtPtr scopedStatement() {
        if(is("{"))return block(true);
        Scope scope(this);
        return statement();
    }
    StmtPtr loopBody() {
        Count loop(loops);
        return is("{")?block(false):statement();
    }

    StmtPtr statement() {
        Nest nest(*this,peek());
        const Token& x=peek();
        if(is("{"))return block(true);
        if(is(";")){next();return std::make_unique<Stmt>(StmtKind::Empty,x.loc);}
        if(x.kind==TokenKind::Keyword) {
            const std::string& w=x.text;
            if(w=="if")return ifStatement();
            if(w=="switch")return switchStatement();
            if(w=="for")return forStatement();
            if(w=="while")return whileStatement();
            if(w=="do")return doStatement();
            if(w=="case"||w=="default")misplacedLabel(x);
            if(w=="break"||w=="continue"||w=="discard"||w=="return")return jump();
        }
        if(startsDeclaration()) {
            auto s=std::make_unique<Stmt>(StmtKind::Declaration,x.loc);
            s->decl=declaration(false);
            return s;
        }
        auto s=std::make_unique<Stmt>(StmtKind::Expression,x.loc);
        s->expr=expression();
        expect(";","after the expression");
        return s;
    }

    StmtPtr ifStatement() {
        auto s=std::make_unique<Stmt>(StmtKind::If,next().loc);
        expect("(","after 'if'");
        s->expr=expression();
        expect(")","to close the if condition");
        s->then=scopedStatement();
        if(isKeyword("else")){next();s->otherwise=scopedStatement();}
        return s;
    }

    StmtPtr switchStatement() {
        auto s=std::make_unique<Stmt>(StmtKind::Switch,next().loc);
        expect("(","after 'switch'");
        s->expr=expression();
        expect(")","to close the switch selector");
        Count inSwitch(switches);
        Scope scope(this);
        expect("{","to begin the switch body");
        while(!is("}") && peek().kind!=TokenKind::End) {
            const std::size_t start=i;
            try {
                if(isKeyword("case")) {
                    auto c=std::make_unique<Stmt>(StmtKind::Case,next().loc);
                    c->expr=conditional();
                    expect(":","after the case label");
                    s->body.push_back(std::move(c));
                } else if(isKeyword("default")) {
                    auto c=std::make_unique<Stmt>(StmtKind::Default,next().loc);
                    expect(":","after 'default'");
                    s->body.push_back(std::move(c));
                } else s->body.push_back(statement());
            } catch(const ParseError&) {if(++errors>=kMaxErrors)throw;recoverStatement(start);}
        }
        expect("}","to close the switch body");
        return s;
    }

    void condition(Stmt& s) {
        if(!startsDeclaration()){s.expr=expression();return;}
        auto d=std::make_unique<Decl>(DeclKind::Variable,peek().loc);
        d->qualifiers=qualifiers();
        d->type=typeSpecifier(true);
        const Token& n=expectName("in the condition declaration");
        Declarator v; v.name=n.text; v.loc=n.loc;
        expect("=","in the condition declaration (it needs an initializer)");
        v.init=assignment();
        declare(v.name,v.loc,false);
        d->declarators.push_back(std::move(v));
        s.decl=std::move(d);
    }

    StmtPtr forStatement() {
        auto s=std::make_unique<Stmt>(StmtKind::For,next().loc);
        expect("(","after 'for'");
        Scope scope(this);
        if(is(";"))s->init=std::make_unique<Stmt>(StmtKind::Empty,next().loc);
        else if(startsDeclaration()) {
            s->init=std::make_unique<Stmt>(StmtKind::Declaration,peek().loc);
            s->init->decl=declaration(false);
        } else {
            s->init=std::make_unique<Stmt>(StmtKind::Expression,peek().loc);
            s->init->expr=expression();
            expect(";","after the for initializer");
        }
        if(!is(";"))condition(*s);
        expect(";","after the for condition");
        if(!is(")"))s->step=expression();
        expect(")","to close the for header");
        s->loop=loopBody();
        return s;
    }

    StmtPtr whileStatement() {
        auto s=std::make_unique<Stmt>(StmtKind::While,next().loc);
        expect("(","after 'while'");
        Scope scope(this);
        condition(*s);
        expect(")","to close the while condition");
        s->loop=loopBody();
        return s;
    }

    StmtPtr doStatement() {
        auto s=std::make_unique<Stmt>(StmtKind::DoWhile,next().loc);
        {
            Count loop(loops);
            s->loop=scopedStatement();
        }
        if(!isKeyword("while"))fail(peek(),"Invalid syntax: expected 'while' after the body of a do loop, found "+describe(peek()),{},"syntax.expected-token");
        next();
        expect("(","after 'while'");
        s->expr=expression();
        expect(")","to close the do-while condition");
        expect(";","after the do-while statement");
        return s;
    }

    StmtPtr jump() {
        const Token& x=next();
        const StmtKind kind=x.text=="break"?StmtKind::Break:x.text=="continue"?StmtKind::Continue:x.text=="discard"?StmtKind::Discard:StmtKind::Return;
        auto s=std::make_unique<Stmt>(kind,x.loc);
        if(kind==StmtKind::Break && !loops && !switches)
            report(x,"Invalid 'break': it must be inside a loop or switch statement","remove it or restructure the control flow","syntax.misplaced-jump");
        if(kind==StmtKind::Continue && !loops)
            report(x,"Invalid 'continue': it must be inside a loop","remove it or restructure the control flow","syntax.misplaced-jump");
        if(kind==StmtKind::Return && !is(";"))s->expr=expression();
        expect(";","after '"+x.text+"'");
        return s;
    }

    // ---------------------------------------------------------------- expressions
    static const std::string& noText(){static const std::string empty;return empty;}
    ExprPtr finish(ExprPtr e) {
        unsigned h=0;
        for(const auto& o:e->operands)if(o)h=std::max(h,o->height);
        if(e->constructed)for(const auto& a:e->constructed->arrays)if(a.size)h=std::max(h,a.size->height);
        e->height=h+1;
        if(e->height>kMaxHeight)tooTall(e->loc);
        return e;
    }
    ExprPtr node(ExprKind kind, Loc loc, const std::string& text, ExprPtr a, ExprPtr b=nullptr, ExprPtr c=nullptr) {
        auto e=std::make_unique<Expr>(kind,loc);
        e->text=text;
        for(auto* o:{&a,&b,&c})if(*o)e->operands.push_back(std::move(*o));
        return finish(std::move(e));
    }

    ExprPtr expression() {
        auto e=assignment();
        while(is(",")) {
            const Token& comma=next();
            e=node(ExprKind::Comma,comma.loc,comma.text,std::move(e),assignment());
        }
        return e;
    }

    static bool assignable(const Expr& e) {
        if(e.parenthesized)return true;
        switch(e.kind) {
        case ExprKind::Binary: case ExprKind::Assign: case ExprKind::Ternary: case ExprKind::Comma: return false;
        default: return true;
        }
    }
    ExprPtr assignment() {
        Nest nest(*this,peek());
        auto target=conditional();
        static const char* const operators[]={"=","*=","/=","%=","+=","-=","<<=",">>=","&=","^=","|="};
        const Token& op=peek();
        if(op.kind!=TokenKind::Punct || std::find_if(std::begin(operators),std::end(operators),[&](const char* o){return op.text==o;})==std::end(operators))
            return target;
        next();
        if(!assignable(*target))invalidAssignment(op);
        auto value=assignment();
        return node(ExprKind::Assign,op.loc,op.text,std::move(target),std::move(value));
    }

    ExprPtr conditional() {
        auto c=binary(1);
        if(!is("?"))return c;
        const Token& q=next();
        auto a=expression();
        expect(":","in the conditional expression");
        auto b=assignment();
        return node(ExprKind::Ternary,q.loc,q.text,std::move(c),std::move(a),std::move(b));
    }

    static int precedence(const Token& x) {
        if(x.kind!=TokenKind::Punct)return 0;
        static const std::map<std::string,int> table={{"||",1},{"^^",2},{"&&",3},{"|",4},{"^",5},{"&",6},{"==",7},{"!=",7},
            {"<",8},{">",8},{"<=",8},{">=",8},{"<<",9},{">>",9},{"+",10},{"-",10},{"*",11},{"/",11},{"%",11}};
        const auto it=table.find(x.text);
        return it==table.end()?0:it->second;
    }
    ExprPtr binary(int minimum) {
        auto l=unary();
        for(;;) {
            const int p=precedence(peek());
            if(!p || p<minimum)return l;
            const Token& op=next();
            auto r=binary(p+1);
            l=node(ExprKind::Binary,op.loc,op.text,std::move(l),std::move(r));
        }
    }

    ExprPtr unary() {
        const Token& x=peek();
        if(x.kind==TokenKind::Punct && (x.text=="++"||x.text=="--"||x.text=="+"||x.text=="-"||x.text=="!"||x.text=="~")) {
            Nest nest(*this,x);
            next();
            return node(ExprKind::Unary,x.loc,x.text,unary());
        }
        return postfix();
    }

    void arguments(Expr& e) {
        expect("(","to begin the argument list");
        if(isKeyword("void") && is(")",1)){next();next();e.voidArgument=true;return;}
        if(accept(")"))return;
        do e.operands.push_back(assignment()); while(accept(","));
        expect(")","to close the argument list");
    }

    ExprPtr postfix() {
        auto e=primary();
        for(;;) {
            if(is("[")) {
                const Token& b=next();
                auto index=expression();
                expect("]","to close the index");
                e=node(ExprKind::Index,b.loc,noText(),std::move(e),std::move(index));
            } else if(is(".")) {
                next();
                const Token& f=peek();
                if(f.kind!=TokenKind::Identifier)expectedField(f);
                next();
                if(is("(")) {
                    auto m=std::make_unique<Expr>(ExprKind::Method,f.loc);
                    m->text=f.text;
                    m->operands.push_back(std::move(e));
                    arguments(*m);
                    e=finish(std::move(m));
                } else e=node(ExprKind::Field,f.loc,f.text,std::move(e));
            } else if(is("++")||is("--")) {
                const Token& op=next();
                e=node(ExprKind::Postfix,op.loc,op.text,std::move(e));
            } else return e;
        }
    }

    ExprPtr primary() {
        const Token& x=peek();
        switch(x.kind) {
        case TokenKind::IntLiteral: case TokenKind::UintLiteral: case TokenKind::FloatLiteral: {
            next();
            auto e=std::make_unique<Expr>(x.kind==TokenKind::IntLiteral?ExprKind::IntLiteral:x.kind==TokenKind::UintLiteral?ExprKind::UintLiteral:ExprKind::FloatLiteral,x.loc);
            e->text=x.text; e->intValue=x.intValue; e->floatValue=x.floatValue;
            return e;
        }
        case TokenKind::Keyword:
            if(x.text=="true"||x.text=="false") {
                next();
                auto e=std::make_unique<Expr>(ExprKind::BoolLiteral,x.loc);
                e->text=x.text; e->intValue=x.text=="true";
                return e;
            }
            if(isBuiltinTypeName(x.text))return constructor();
            expectedExpression(x);
        case TokenKind::Identifier: {
            if(isTypeName(x))return constructor();
            next();
            if(!is("(")) {
                auto e=std::make_unique<Expr>(ExprKind::Name,x.loc);
                e->text=x.text;
                return e;
            }
            auto e=std::make_unique<Expr>(ExprKind::Call,x.loc);
            e->text=x.text;
            arguments(*e);
            return finish(std::move(e));
        }
        default:
            if(is("(")) {
                next();
                auto e=expression();
                expect(")","to close the parenthesised expression");
                e->parenthesized=true;
                return e;
            }
            expectedExpression(x);
        }
    }

    ExprPtr constructor() {
        const Loc loc=peek().loc;
        auto type=std::make_unique<TypeSpec>(typeSpecifier(true));
        if(!is("("))expectedConstructorCall(*type);
        auto e=std::make_unique<Expr>(ExprKind::Constructor,loc);
        e->constructed=std::move(type);
        arguments(*e);
        return finish(std::move(e));
    }
};
}

bool checkDelimiters(const std::vector<Token>& tokens, DiagnosticSink& sink) {
    std::vector<const Token*> open;
    auto closer=[](const std::string& o)->std::string{return o=="("?")":o=="["?"]":"}";};
    for(const auto& x:tokens) {
        if(x.kind!=TokenKind::Punct)continue;
        if(x.text=="("||x.text=="["||x.text=="{"){open.push_back(&x);continue;}
        if(x.text!=")" && x.text!="]" && x.text!="}")continue;
        if(open.empty()) {
            sink.error(x.loc,"syntax.unbalanced-delimiter","Unbalanced delimiter: '"+x.text+"' has no matching opening delimiter",
                "remove it or add the missing opening delimiter");
            return false;
        }
        if(closer(open.back()->text)!=x.text) {
            sink.error(x.loc,"syntax.unbalanced-delimiter","Unbalanced delimiter: found '"+x.text+"' while '"+open.back()->text+
                "' opened at "+where(open.back()->loc)+" is still open","close it with '"+closer(open.back()->text)+"' first");
            return false;
        }
        open.pop_back();
    }
    if(open.empty())return true;
    sink.error(open.back()->loc,"syntax.unclosed-delimiter","Unclosed delimiter '"+open.back()->text+"': no matching '"+closer(open.back()->text)+"' before the end of the shader",
        "add the matching '"+closer(open.back()->text)+"'");
    return false;
}

std::unique_ptr<TranslationUnit> parse(const std::vector<Token>& tokens, int version, DiagnosticSink& sink) {
    return Parser(tokens,version,sink).run();
}

ParsedShader parseShader(const std::string& source, DiagnosticSink& sink) {
    ParsedShader out;
    const auto lexed=lex(source,sink);
    auto pp=preprocess(lexed,sink);
    out.version=pp.version; out.versionDeclared=pp.versionDeclared;
    out.extensions=pp.extensions; out.pragmas=pp.pragmas;
    out.tokens=classify(pp,lexed,sink);
    if(sink.errors || !checkDelimiters(out.tokens,sink))return out;
    out.unit=parse(out.tokens,out.version,sink);
    if(out.unit && out.version==100)
        for(const auto& x:out.extensions)
            if(x.name=="GL_OES_standard_derivatives"||x.name=="all")out.unit->standardDerivatives=x.behavior!="disable";
    return out;
}
}
