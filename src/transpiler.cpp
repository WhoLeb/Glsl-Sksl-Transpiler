#include "glsl_sksl/transpiler.hpp"
#include "emitter.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <tuple>
namespace glsl_sksl {
namespace {
std::vector<std::string> sourceLines(const std::string& source) {
    std::vector<std::string> lines(1);
    for(std::size_t i=0;i<source.size();++i) {
        if(source[i]=='\r'){if(i+1<source.size() && source[i+1]=='\n')++i;lines.emplace_back();}
        else if(source[i]=='\n')lines.emplace_back();
        else lines.back()+=source[i];
    }
    return lines;
}

bool identifierChar(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
// The GLSL token at a position: an identifier or number, or a single character.
std::string tokenAt(const std::string& text, unsigned column) {
    if(column==0 || column>text.size())return {};
    std::size_t b=column-1, e=b;
    while(e<text.size() && identifierChar(text[e]))++e;
    return e>b?text.substr(b,e-b):text.substr(b,1);
}
// Finds `token` in `text`; a token that is a whole identifier must not be part of a longer one.
std::size_t findToken(const std::string& text, const std::string& token) {
    const bool word=!token.empty() && std::all_of(token.begin(),token.end(),identifierChar);
    for(std::size_t p=text.find(token);p!=std::string::npos;p=text.find(token,p+1)) {
        if(!word)return p;
        if((p==0 || !identifierChar(text[p-1])) && (p+token.size()==text.size() || !identifierChar(text[p+token.size()])))return p;
    }
    return std::string::npos;
}
// The SkSL position of a diagnostic: the lines generated from the innermost statement or
// declaration whose GLSL source contains its position, and in them the first occurrence of the
// hint (the replacement) or of the GLSL token at the position; otherwise the start of the first
// such line. Without such a statement or declaration there is no SkSL position. `first` is the
// SkSL line of origins[0].
void locate(Diagnostic& d, const std::string& hint, const std::vector<std::string>& glsl, const std::vector<std::string>& sksl,
    const std::vector<SourceSpan>& origins, unsigned first) {
    if(d.line==0)return;
    const unsigned column=d.column?d.column:~0u;
    Loc best;
    for(const SourceSpan& span:origins) {
        const Loc& o=span.start;
        if(!o.line || span.endLine<d.line || o.line>d.line || (o.line==d.line && o.column>column))continue;
        if(o.line>best.line || (o.line==best.line && o.column>best.column))best=o;
    }
    if(!best.line)return;
    std::string token=hint;
    if(token.empty() && d.line<=glsl.size())token=tokenAt(glsl[d.line-1],d.column);
    const std::size_t skip=token.find_first_not_of(' ');
    std::size_t line=0, at=std::string::npos;
    for(std::size_t i=0;i<origins.size() && first+i<=sksl.size();++i) {
        if(origins[i].start.line!=best.line || origins[i].start.column!=best.column)continue;
        const std::string& text=sksl[first+i-1];
        if(!line) {
            line=first+i;
            at=text.find_first_not_of(' ');
            if(at==std::string::npos)at=0;
        }
        const std::size_t p=token.empty()?std::string::npos:findToken(text,token);
        if(p!=std::string::npos){line=first+i;at=p+(skip==std::string::npos?0:skip);break;}
    }
    if(!line)return;
    d.skslLine=static_cast<unsigned>(line);
    d.skslColumn=static_cast<unsigned>(at+1);
}

// GLSL errors (sema.*) by position and code.
std::set<std::tuple<unsigned,unsigned,std::string>> invalidErrors(const DiagnosticSink& sink) {
    std::set<std::tuple<unsigned,unsigned,std::string>> errors;
    for(const auto& d:sink.list)if(d.severity==Severity::Error && d.code.compare(0,5,"sema.")==0)errors.emplace(d.line,d.column,d.code);
    return errors;
}
// The texture coordinate input when it is a vec3 or vec4 (see transpile).
const sema::Symbol* wideTextureCoordinate(const sema::Program& program) {
    for(const sema::Symbol* s:program.inputs)
        if(s->textureCoordinate && s->type.base==sema::Type::Float && s->type.rows>2 && !s->type.isArray())return s;
    return nullptr;
}
}

bool Result::ok() const {
    if(state!=State::Success)return false;
    for(const auto& d:diagnostics)if(d.severity==Severity::Error)return false;
    return true;
}

std::map<unsigned, std::vector<Diagnostic>> Result::diagnosticsByLine() const {
    std::map<unsigned, std::vector<Diagnostic>> byLine;
    for(const auto& d:diagnostics)byLine[d.line].push_back(d);
    return byLine;
}

// Allocation failures inside are caught. Constructing and returning `r` allocates nothing in
// standard libraries without debug containers; with MSVC's iterator debugging (the Debug
// default) the containers' noexcept constructors allocate and terminate when that fails, which
// no code outside the standard library can prevent.
Result transpile(const std::string& source) noexcept {
    Result r;
    try {
        const auto lines=sourceLines(source);
        DiagnosticSink sink;
        std::vector<SourceSpan> origins;
        auto parsed=parseShader(source,sink);
        // SkSL is generated whenever the shader parses, also with errors: the parts that are
        // unsupported or invalid are written as close to the GLSL as possible, and the errors say
        // what needs manual adjustment. Without a syntax tree there is nothing to write.
        if(parsed.unit && !sink.errors) {
            auto program=sema::analyze(*parsed.unit,sink);
            // The texture coordinate input holds UV in x and y. Shaders that declare it as a vec4
            // but use it as a vec2 (invalid GLSL: 'uv_coords - offset' with a vec2 offset) are
            // analysed again with the input typed as a vec2, which is kept when it removes GLSL
            // errors without adding any; shaders that are valid as written are never changed.
            const auto before=invalidErrors(sink);
            if(const sema::Symbol* input=wideTextureCoordinate(*program); input && !before.empty()) {
                const std::string name=input->name, type=sema::toString(input->type);
                const Loc at=input->loc;
                DiagnosticSink retry;
                auto again=parseShader(source,retry);
                if(again.unit && !retry.errors) {
                    auto retyped=sema::analyze(*again.unit,retry,name);
                    const auto after=invalidErrors(retry);
                    if(after.size()<before.size() && std::includes(before.begin(),before.end(),after.begin(),after.end())) {
                        retry.warning(at,"sema.input-vec2","input '"+name+"' ("+type+") used as a vec2; treated as a vec2",
                            "declare it 'in vec2 "+name+"'");
                        sink=std::move(retry);
                        parsed=std::move(again);
                        program=std::move(retyped);
                    }
                }
            }
            r.source=emitSkSL(*parsed.unit,*program,parsed.tokens,sink,origins,r.renamedIdentifiers,r.textures);
        }
        unsigned errors=0;
        std::vector<std::size_t> order(sink.list.size());
        for(std::size_t i=0;i<order.size();++i){order[i]=i;if(sink.list[i].severity==Severity::Error)++errors;}
        std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
            const Diagnostic& x=sink.list[a]; const Diagnostic& y=sink.list[b];
            return x.line!=y.line?x.line<y.line:x.column<y.column;});
        // Some refused constructs would compile once written out (a dropped discard, a fixed
        // gl_FragCoord.z) and render differently. SkSL has no #error, so one such line per error
        // makes Skia reject the source for certain while showing why.
        if(errors && !r.source.empty()) {
            std::string header;
            for(std::size_t i:order) {
                const Diagnostic& d=sink.list[i];
                if(d.severity==Severity::Error)
                    header+="#error GLSL "+std::to_string(d.line)+":"+std::to_string(d.column)+": "+d.message+" ["+d.code+"]\n";
            }
            r.source=header+r.source;
        }
        const auto sksl=sourceLines(r.source);
        for(std::size_t i:order) {
            Diagnostic d=sink.list[i];
            if(!r.source.empty())locate(d,i<sink.skslHints.size()?sink.skslHints[i]:std::string(),lines,sksl,origins,errors+1);
            r.diagnostics.push_back(std::move(d));
        }
        const auto skslLines=sourceLines(r.source);
        for(auto& d:r.diagnostics) {
            if(d.line>0 && d.line<=lines.size())d.sourceLine=lines[d.line-1];
            if(d.skslLine>0 && d.skslLine<=skslLines.size())d.skslSourceLine=skslLines[d.skslLine-1];
        }
        r.state=Result::State::Success;
        for(const auto& d:r.diagnostics)if(d.severity==Severity::Error)r.state=Result::State::Failure;
    } catch(const std::exception& e) {
        r.source.clear();
        r.diagnostics.clear();
        r.renamedIdentifiers.clear();
        r.textures.clear();
        r.state=Result::State::Failure;
        try { r.diagnostics.push_back({0,0,std::string("Internal error: ")+e.what(),Severity::Error,"internal"}); } catch(...) {}
    } catch(...) {
        r.source.clear();
        r.diagnostics.clear();
        r.renamedIdentifiers.clear();
        r.textures.clear();
        r.state=Result::State::Failure;
        try { r.diagnostics.push_back({0,0,"Internal error",Severity::Error,"internal"}); } catch(...) {}
    }
    return r;
}

std::string formatDiagnostic(const Diagnostic& d) {
    std::string s=std::to_string(d.line)+":"+std::to_string(d.column)+": "+(d.severity==Severity::Error?"error":"warning")+": "+d.message;
    if(!d.code.empty())s+=" ["+d.code+"]";
    s+="\n";
    const std::string glsl=d.line?std::to_string(d.line):"-";
    const std::string sksl=d.skslLine?std::to_string(d.skslLine):"-";
    const std::size_t width=std::max(glsl.size(),sksl.size());
    auto label=[&](const char* name,const std::string& number){return "  "+std::string(name)+" "+std::string(width-number.size(),' ')+number+" | ";};
    auto caret=[&](const std::string& text,unsigned column) {
        std::string marker;
        for(std::size_t i=0;i+1<column && i<text.size();++i)marker+=text[i]=='\t'?'\t':' ';
        s+="  "+std::string(5+width,' ')+"| "+marker+"^\n";
    };
    s+=label("GLSL",glsl)+(d.line?d.sourceLine:"(no source position)")+"\n";
    if(d.line && d.column)caret(d.sourceLine,d.column);
    s+=label("SkSL",sksl)+(d.skslLine?d.skslSourceLine:"(no SkSL generated)")+"\n";
    if(d.skslLine && d.skslColumn)caret(d.skslSourceLine,d.skslColumn);
    if(!d.suggestion.empty())s+="  suggestion: "+d.suggestion+"\n";
    return s;
}
}
