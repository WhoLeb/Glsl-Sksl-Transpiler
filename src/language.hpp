#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace glsl_sksl {
// GLSL ES versions as written in #version: 100, 300, 310, 320.
enum class WordClass { Identifier, Keyword, Reserved };
WordClass classifyWord(std::string_view word, int version);
bool isBuiltinTypeName(std::string_view word);
bool isQualifierKeyword(std::string_view word);
bool isKnownExtension(std::string_view name);
std::string versionName(int version);

struct NumberLiteral {
    enum Kind { Int, Uint, Float, Invalid } kind=Invalid;
    std::uint64_t intValue=0;   // raw 32-bit pattern for Int/Uint
    double floatValue=0;
    bool overflow=false;        // Float only: the value is not finite as a double
    std::string spelling;       // literal without suffix
    std::string message;        // Invalid only
};
NumberLiteral parseNumber(std::string_view text, int version);
}
