#include "ast.hpp"
namespace glsl_sksl::ast {
Expr::~Expr() {
    std::vector<ExprPtr> pending;
    for(auto& o:operands)if(o)pending.push_back(std::move(o));
    while(!pending.empty()) {
        ExprPtr e=std::move(pending.back()); pending.pop_back();
        for(auto& o:e->operands)if(o)pending.push_back(std::move(o));
    }
}

bool Qualifiers::has(const std::string& word) const {
    for(const auto& q:list)if(q.word==word)return true;
    return false;
}

namespace {
std::string dump(const Qualifiers& q) {
    std::string s;
    for(const auto& x:q.list) {
        if(!s.empty())s+=' ';
        s+=x.word;
        if(x.word!="layout")continue;
        s+='(';
        for(std::size_t i=0;i<x.layout.size();++i) {
            if(i)s+=',';
            s+=x.layout[i].name;
            if(x.layout[i].value)s+="="+dump(*x.layout[i].value);
        }
        s+=')';
    }
    return s;
}
std::string dump(const std::vector<ArraySize>& arrays) {
    std::string s;
    for(const auto& a:arrays)s+="["+(a.size?dump(*a.size):std::string())+"]";
    return s;
}
std::string dump(const std::vector<FieldGroup>& members);
std::string dump(const TypeSpec& t) {
    std::string s=t.structDef?"struct "+t.name+"{"+dump(t.structDef->members)+"}":t.name;
    return s+dump(t.arrays);
}
std::string dump(const std::vector<FieldGroup>& members) {
    std::string s;
    for(const auto& m:members) {
        if(!s.empty())s+=' ';
        s+="(";
        if(!m.qualifiers.empty())s+=dump(m.qualifiers)+" ";
        s+=dump(m.type);
        for(const auto& f:m.fields)s+=" "+f.name+dump(f.arrays);
        s+=")";
    }
    return s;
}
std::string dump(const Declarator& d) {
    std::string s=d.name+dump(d.arrays);
    if(d.init)s+="="+dump(*d.init);
    return s;
}
std::string optional(const ExprPtr& e){return e?dump(*e):"_";}
std::string optional(const StmtPtr& s){return s?dump(*s):"_";}

int precedence(const Expr& e) {
    switch(e.kind) {
    case ExprKind::Comma: return 0;
    case ExprKind::Assign: return 1;
    case ExprKind::Ternary: return 2;
    case ExprKind::Binary: {
        static const char* const levels[][4]={{"||"},{"^^"},{"&&"},{"|"},{"^"},{"&"},{"==","!="},{"<",">","<=",">="},{"<<",">>"},{"+","-"},{"*","/","%"}};
        for(int l=0;l<11;++l)for(const char* op:levels[l])if(op && e.text==op)return 3+l;
        return 3;
    }
    case ExprKind::Unary: return 14;
    case ExprKind::Postfix: case ExprKind::Index: case ExprKind::Field: case ExprKind::Method: case ExprKind::Call: case ExprKind::Constructor: return 15;
    default: return 16;
    }
}
// Diagnostics only need a readable excerpt, so deeply nested parts print as "...", which also
// bounds the recursion.
constexpr int kMaxPrintDepth=16;
std::string render(const Expr& e, int depth);
std::string glsl(const Expr& e, int minimum, int depth) {
    const std::string s=render(e,depth+1);
    return precedence(e)<minimum?"("+s+")":s;
}
std::string arguments(const Expr& e, std::size_t from, int depth) {
    std::string s="(";
    if(e.voidArgument)s+="void";
    for(std::size_t i=from;i<e.operands.size();++i)s+=(i>from?", ":"")+glsl(*e.operands[i],1,depth);
    return s+")";
}
std::string render(const Expr& e, int depth) {
    if(depth>kMaxPrintDepth && !e.operands.empty())return "...";
    switch(e.kind) {
    case ExprKind::UintLiteral: return e.text+"u";
    case ExprKind::IntLiteral: case ExprKind::FloatLiteral: case ExprKind::BoolLiteral: case ExprKind::Name: return e.text;
    case ExprKind::Unary: return e.text+glsl(*e.operands[0],14,depth);
    case ExprKind::Postfix: return glsl(*e.operands[0],15,depth)+e.text;
    case ExprKind::Binary: {
        const int p=precedence(e);
        return glsl(*e.operands[0],p,depth)+" "+e.text+" "+glsl(*e.operands[1],p+1,depth);
    }
    case ExprKind::Assign: return glsl(*e.operands[0],2,depth)+" "+e.text+" "+glsl(*e.operands[1],1,depth);
    case ExprKind::Ternary: return glsl(*e.operands[0],3,depth)+" ? "+glsl(*e.operands[1],0,depth)+" : "+glsl(*e.operands[2],1,depth);
    case ExprKind::Call: return e.text+arguments(e,0,depth);
    case ExprKind::Constructor: {
        std::string s=e.constructed?e.constructed->name:"";
        if(e.constructed)for(const auto& a:e.constructed->arrays)s+="["+(a.size?render(*a.size,depth+1):std::string())+"]";
        return s+arguments(e,0,depth);
    }
    case ExprKind::Index: return glsl(*e.operands[0],15,depth)+"["+render(*e.operands[1],depth+1)+"]";
    case ExprKind::Field: return glsl(*e.operands[0],15,depth)+"."+e.text;
    case ExprKind::Method: return glsl(*e.operands[0],15,depth)+"."+e.text+arguments(e,1,depth);
    case ExprKind::Comma: return glsl(*e.operands[0],0,depth)+", "+glsl(*e.operands[1],1,depth);
    }
    return "?";
}
}

std::string toGlsl(const Expr& e) {return render(e,0);}

std::string dump(const Expr& e) {
    auto all=[&](std::size_t from) {
        std::string s;
        for(std::size_t i=from;i<e.operands.size();++i)s+=" "+dump(*e.operands[i]);
        return s;
    };
    switch(e.kind) {
    case ExprKind::IntLiteral: case ExprKind::FloatLiteral: case ExprKind::BoolLiteral: case ExprKind::Name: return e.text;
    case ExprKind::UintLiteral: return e.text+"u";
    case ExprKind::Unary: return "("+e.text+all(0)+")";
    case ExprKind::Postfix: return "(post"+e.text+all(0)+")";
    case ExprKind::Binary: case ExprKind::Assign: return "("+e.text+all(0)+")";
    case ExprKind::Ternary: return "(?"+all(0)+")";
    case ExprKind::Call: return "(call "+e.text+(e.voidArgument?" void":"")+all(0)+")";
    case ExprKind::Constructor: return "(ctor "+dump(*e.constructed)+(e.voidArgument?" void":"")+all(0)+")";
    case ExprKind::Index: return "([]"+all(0)+")";
    case ExprKind::Field: return "(. "+dump(*e.operands[0])+" "+e.text+")";
    case ExprKind::Method: return "(method "+e.text+all(0)+")";
    case ExprKind::Comma: return "(,"+all(0)+")";
    }
    return "?";
}

std::string dump(const Stmt& s) {
    auto list=[](const std::vector<StmtPtr>& body) {
        std::string r;
        for(const auto& x:body)r+=" "+dump(*x);
        return r;
    };
    switch(s.kind) {
    case StmtKind::Block: return std::string(s.scoped?"(block":"(body")+list(s.body)+")";
    case StmtKind::Declaration: return dump(*s.decl);
    case StmtKind::Expression: return dump(*s.expr);
    case StmtKind::Empty: return "(empty)";
    case StmtKind::If: return "(if "+dump(*s.expr)+" "+dump(*s.then)+(s.otherwise?" "+dump(*s.otherwise):"")+")";
    case StmtKind::Switch: return "(switch "+dump(*s.expr)+list(s.body)+")";
    case StmtKind::Case: return "(case "+dump(*s.expr)+")";
    case StmtKind::Default: return "(default)";
    case StmtKind::For: return "(for "+optional(s.init)+" "+(s.decl?dump(*s.decl):optional(s.expr))+" "+optional(s.step)+" "+dump(*s.loop)+")";
    case StmtKind::While: return "(while "+(s.decl?dump(*s.decl):dump(*s.expr))+" "+dump(*s.loop)+")";
    case StmtKind::DoWhile: return "(do "+dump(*s.loop)+" "+dump(*s.expr)+")";
    case StmtKind::Return: return s.expr?"(return "+dump(*s.expr)+")":"(return)";
    case StmtKind::Break: return "(break)";
    case StmtKind::Continue: return "(continue)";
    case StmtKind::Discard: return "(discard)";
    }
    return "?";
}

std::string dump(const Decl& d) {
    const std::string q=d.qualifiers.empty()?"":dump(d.qualifiers)+" ";
    switch(d.kind) {
    case DeclKind::Variable: {
        std::string s="(var "+q+dump(d.type);
        for(const auto& v:d.declarators)s+=" "+dump(v);
        return s+")";
    }
    case DeclKind::Function: {
        const Function& f=*d.function;
        std::string s="(function "+(f.returnQualifiers.empty()?"":dump(f.returnQualifiers)+" ")+dump(f.returnType)+" "+f.name+" (";
        if(f.voidParams)s+="void";
        for(std::size_t i=0;i<f.params.size();++i) {
            const Param& p=f.params[i];
            if(i)s+=", ";
            if(!p.qualifiers.empty())s+=dump(p.qualifiers)+" ";
            s+=dump(p.type);
            if(!p.name.empty())s+=" "+p.name+dump(p.arrays);
        }
        return s+")"+(f.body?" "+dump(*f.body):"")+")";
    }
    case DeclKind::Precision: return "(precision "+q+dump(d.type)+")";
    case DeclKind::Block: {
        const InterfaceBlock& b=*d.block;
        std::string s="(interface "+(b.qualifiers.empty()?"":dump(b.qualifiers)+" ")+b.name+" {"+dump(b.members)+"}";
        if(!b.instance.empty())s+=" "+b.instance+dump(b.instanceArrays);
        return s+")";
    }
    case DeclKind::Qualifier: {
        std::string s="(qualifier "+dump(d.qualifiers);
        for(const auto& n:d.names)s+=" "+n.first;
        return s+")";
    }
    }
    return "?";
}

std::string dump(const TranslationUnit& unit) {
    std::string s;
    for(const auto& d:unit.decls)s+=dump(*d)+"\n";
    return s;
}
}
