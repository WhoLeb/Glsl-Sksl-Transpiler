#include "glsl_sksl/transpiler.hpp"
#include "emitter.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <algorithm>
#include <exception>
#include <map>
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
}

bool Result::ok() const {
    for(const auto& d:diagnostics)if(d.severity==Severity::Error)return false;
    return true;
}

std::map<unsigned, std::vector<Diagnostic>> Result::diagnosticsByLine() const {
    std::map<unsigned, std::vector<Diagnostic>> byLine;
    for(const auto& d:diagnostics)byLine[d.line].push_back(d);
    return byLine;
}

Result transpile(const std::string& source) {
    Result r;
    try {
        DiagnosticSink sink;
        const auto parsed=parseShader(source,sink);
        if(parsed.unit && !sink.errors) {
            const auto program=sema::analyze(*parsed.unit,sink);
            if(!sink.errors)r.source=emitSkSL(*parsed.unit,*program,parsed.tokens,sink);
        }
        r.diagnostics=std::move(sink.list);
        std::stable_sort(r.diagnostics.begin(),r.diagnostics.end(),[](const Diagnostic& a,const Diagnostic& b){
            return a.line!=b.line?a.line<b.line:a.column<b.column;});
        if(!r.ok())r.source.clear();
    } catch(const std::exception& e) {
        r.source.clear();
        r.diagnostics.push_back({0,0,std::string("Internal error: ")+e.what(),Severity::Error,"internal"});
    } catch(...) {
        r.source.clear();
        r.diagnostics.push_back({0,0,"Internal error",Severity::Error,"internal"});
    }
    const auto lines=sourceLines(source);
    for(auto& d:r.diagnostics)
        if(d.line>0 && d.line<=lines.size())d.sourceLine=lines[d.line-1];
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
    s+=label("GLSL",glsl)+(d.line?d.sourceLine:"(no source position)")+"\n";
    if(d.line && d.column) {
        std::string marker;
        for(std::size_t i=0;i+1<d.column && i<d.sourceLine.size();++i)marker+=d.sourceLine[i]=='\t'?'\t':' ';
        s+="  "+std::string(5+width,' ')+"| "+marker+"^\n";
    }
    s+=label("SkSL",sksl)+(d.skslLine?"":"(no SkSL generated)")+"\n";
    if(!d.suggestion.empty())s+="  suggestion: "+d.suggestion+"\n";
    return s;
}
}
