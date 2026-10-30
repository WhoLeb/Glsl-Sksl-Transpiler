#include "glsl_sksl/transpiler.hpp"
#include <cstdlib>
#include <new>
#include <clocale>
#include <iostream>
#include <exception>
namespace { long remaining=-1; long allocations=0; }
void* operator new(std::size_t n) {
    ++allocations;
    if(remaining==0)throw std::bad_alloc();
    if(remaining>0)--remaining;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main(){
    std::set_terminate([]{std::_Exit(10);});
    const std::string source="#version 300 es\nprecision highp float;uniform float step;uniform float step_;out vec4 c;void main(){const float x=1.25+0.125;c=vec4(step+step_+x);}";
    (void)glsl_sksl::transpile(source); // The first call also builds static tables; count a warm call.
    allocations=0;
    const auto baseline=glsl_sksl::transpile(source);
    const long total=allocations;
    if(!baseline.ok() || baseline.renamedIdentifiers.empty() || baseline.renamedIdentifiers[0].second=="step_")return 1;
    bool completed=false;
    for(long n=0;n<=total;++n){
        // Fail each allocation of a warm call in turn.
        if(n%100==0)std::cerr<<"allocation checkpoint "<<n<<" / "<<total<<"\n";
        remaining=n;
        auto r=glsl_sksl::transpile(source);
        remaining=-1;
        if(r.ok()){completed=true;break;}
        if(r.state!=glsl_sksl::Result::State::Failure)return 2;
    }
    if(!completed)return 3;
    const std::string old=std::setlocale(LC_NUMERIC,nullptr);
    bool locale=false;
    for(const char* name:{"de-DE","German_Germany.1252","de_DE.UTF-8"}) {
        if(std::setlocale(LC_NUMERIC,name)) {
            locale=true;
            const auto r=glsl_sksl::transpile(source);
            if(!r.ok() || r.source!=baseline.source)return 4;
            break;
        }
    }
    std::setlocale(LC_NUMERIC,old.c_str());
    std::cout<<"allocation sweep passed; decimal-comma locale "<<(locale?"passed":"unavailable (skipped)")<<"\n";
}
