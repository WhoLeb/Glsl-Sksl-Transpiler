#include "lexer.hpp"
#include <cstdio>
namespace glsl_sksl {
namespace {
bool letter(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
bool digit(char c){return c>='0'&&c<='9';}
std::string describe(const std::string& text, std::size_t b, std::size_t e) {
    const auto byte=[&](std::size_t i){return static_cast<unsigned char>(text[i]);};
    char hex[16];
    if(e-b==1) {
        const unsigned char c=byte(b);
        if(c==0)return "Invalid NUL character";
        if(c=='"'||c=='\'')return std::string("Invalid character '")+text[b]+"': GLSL has no string or character literals";
        if(c=='\\')return "Invalid character '\\': a backslash is only allowed immediately before a line break";
        if(c<0x20||c==0x7F){std::snprintf(hex,sizeof hex,"0x%02X",c);return std::string("Invalid control character ")+hex;}
        if(c<0x80)return std::string("Invalid character '")+text[b]+"'";
    }
    unsigned code=0; const unsigned char lead=byte(b);
    const std::size_t length=lead>=0xF0?4:lead>=0xE0?3:lead>=0xC0?2:1;
    if(length==e-b && length>1) {
        code=lead&(0x7Fu>>length);
        for(std::size_t i=b+1;i<e;++i)code=(code<<6)|(byte(i)&0x3Fu);
        std::snprintf(hex,sizeof hex,"U+%04X",code);
        if(code==0xFEFF)return "Invalid character U+FEFF (UTF-8 byte order mark): GLSL source must be plain ASCII outside comments";
        return std::string("Invalid character ")+hex+" '"+text.substr(b,e-b)+"': non-ASCII characters are only allowed in comments";
    }
    std::snprintf(hex,sizeof hex,"0x%02X",lead);
    return std::string("Invalid byte ")+hex+": the source is not valid UTF-8";
}
}

LexOutput lex(const std::string& source, DiagnosticSink& sink) {
    LexOutput out;
    std::string text; std::vector<Loc> at;
    text.reserve(source.size()); at.reserve(source.size()+1);
    unsigned line=1,col=1;
    const std::size_t n=source.size();
    for(std::size_t i=0;i<n;) {
        const char c=source[i];
        if(c=='\\' && i+1<n && (source[i+1]=='\n'||source[i+1]=='\r')) {
            out.continuations.push_back({line,col});
            i+=(source[i+1]=='\r' && i+2<n && source[i+2]=='\n')?3:2;
            ++line;col=1;continue;
        }
        if(c=='\r') {text+='\n';at.push_back({line,col});i+=(i+1<n && source[i+1]=='\n')?2:1;++line;col=1;continue;}
        text+=c;at.push_back({line,col});++i;
        if(c=='\n'){++line;col=1;}else ++col;
    }
    at.push_back({line,col});
    auto& tokens=out.tokens;
    bool space=false, lineStart=true;
    auto push=[&](TokenKind kind,std::size_t b,std::size_t e) {
        Token t; t.kind=kind; t.text=text.substr(b,e-b); t.loc=at[b]; t.spaceBefore=space;
        space=false; lineStart=kind==TokenKind::Newline; tokens.push_back(std::move(t));
    };
    auto comment=[&](std::size_t b,std::size_t e,std::size_t textBegin,std::size_t textEnd) {
        out.comments.push_back({at[b],at[e>b?e-1:b].line,text.substr(textBegin,textEnd-textBegin),lineStart});
    };
    static const char* const longPuncts[]={"<<=",">>=","++","--","<<",">>","<=",">=","==","!=","&&","||","^^",
        "+=","-=","*=","/=","%=","&=","^=","|=","##"};
    static const std::string singlePuncts="()[]{}.,:;?=+-*/%<>!~&|^#";
    const std::size_t m=text.size();
    for(std::size_t p=0;p<m;) {
        const char c=text[p];
        if(c=='\n'){push(TokenKind::Newline,p,p+1);++p;continue;}
        if(c==' '||c=='\t'||c=='\v'||c=='\f'){space=true;++p;continue;}
        if(c=='/' && p+1<m && text[p+1]=='/') {
            const std::size_t b=p;
            while(p<m && text[p]!='\n')++p;
            comment(b,p,b+2,p);
            space=true;continue;
        }
        if(c=='/' && p+1<m && text[p+1]=='*') {
            const std::size_t b=p; bool closed=false;
            for(p+=2;p+1<m;++p)if(text[p]=='*' && text[p+1]=='/'){p+=2;closed=true;break;}
            if(!closed) {
                p=m;
                sink.error(at[b],"lex.unterminated-comment","Unterminated comment","close the comment with '*/'");
            } else comment(b,p,b+2,p-2);
            space=true;continue;
        }
        if(letter(c)) {
            const std::size_t b=p;
            while(p<m && (letter(text[p])||digit(text[p])))++p;
            push(TokenKind::Identifier,b,p);continue;
        }
        if(digit(c) || (c=='.' && p+1<m && digit(text[p+1]))) {
            // Preprocessing number: absorbs every character that could continue a literal so that
            // malformed spellings such as 1x or 1.0.x are diagnosed as one token.
            const std::size_t b=p;
            const bool hex=c=='0' && p+1<m && (text[p+1]=='x'||text[p+1]=='X');
            while(p<m) {
                const char d=text[p];
                if(letter(d)||digit(d)||d=='.'){++p;continue;}
                if((d=='+'||d=='-') && !hex && (text[p-1]=='e'||text[p-1]=='E')){++p;continue;}
                break;
            }
            push(TokenKind::Number,b,p);continue;
        }
        bool matched=false;
        for(const char* punct:longPuncts) {
            const std::string_view s(punct);
            if(text.compare(p,s.size(),s)==0){push(TokenKind::Punct,p,p+s.size());p+=s.size();matched=true;break;}
        }
        if(matched)continue;
        if(singlePuncts.find(c)!=std::string::npos){push(TokenKind::Punct,p,p+1);++p;continue;}
        const std::size_t b=p++;
        if(static_cast<unsigned char>(c)>=0xC0)
            while(p<m && (static_cast<unsigned char>(text[p])&0xC0)==0x80 && p-b<4)++p;
        push(TokenKind::Invalid,b,p);
        tokens.back().invalidReason=describe(text,b,p);
    }
    if(tokens.empty()||tokens.back().kind!=TokenKind::Newline) {
        Token t; t.kind=TokenKind::Newline; t.loc=at.back(); tokens.push_back(std::move(t));
    }
    return out;
}
}
