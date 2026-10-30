#include "glsl_sksl/transpiler.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc,char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);
#endif
    if(argc==2 && std::string(argv[1])=="--help") {std::cout<<"Usage: glsl-to-sksl [input|-] [output]\n";return 0;}
    if(argc>3){std::cerr<<"Too many arguments\n";return 2;}
    std::ifstream file;
    std::istream* in=&std::cin;
    if(argc>1 && std::string(argv[1])!="-"){file.open(argv[1],std::ios::binary);if(!file){std::cerr<<"Cannot open input\n";return 2;}in=&file;}
    std::string source((std::istreambuf_iterator<char>(*in)),{});
    if(in->bad()){std::cerr<<"Cannot read input\n";return 2;}
    auto result=glsl_sksl::transpile(source);
    for(const auto& d:result.diagnostics)std::cerr<<glsl_sksl::formatDiagnostic(d);
    // Unsupported constructs still produce SkSL (which Skia rejects); invalid GLSL produces none.
    if(!result.source.empty()) {
        if(argc==3){std::ofstream out(argv[2]);out<<result.source;if(!out){std::cerr<<"Cannot write output\n";return 2;}}
        else {std::cout<<result.source;if(!std::cout)return 2;}
    }
    return result.ok()?0:1;
}
