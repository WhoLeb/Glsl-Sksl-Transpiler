#pragma once
#include "sema.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
// SkSL replacements for GLSL ES built-ins that SkSL runtime shaders lack (measured with skslc).
// Each helper computes what the GLSL ES specification defines for the built-in.
namespace glsl_sksl {
std::string skslType(const sema::Type& t);   // float2, int3, bool4, float3x3

class BuiltinHelpers {
public:
    // `naming` gives the SkSL name of the helpers that replace a built-in; it is asked once per built-in.
    explicit BuiltinHelpers(std::function<std::string(const std::string&)> naming):naming(std::move(naming)){}
    static bool needed(const sema::Function& builtin);
    // Name to call instead of the built-in; requests the helper and its dependencies.
    std::string use(const sema::Function& builtin);
    // SkSL definitions of every requested helper, dependencies before their users.
    std::string definitions() const;
private:
    struct Key {
        std::string name; std::vector<sema::Type> params;
        bool operator<(const Key& o) const;
    };
    std::function<std::string(const std::string&)> naming;
    std::map<std::string,std::string> names;
    std::string nameOf(const std::string& builtin);
    std::set<Key> requested;
    std::vector<std::string> sources;
    std::string request(const std::string& name, const std::vector<sema::Type>& params);
    std::string generate(const std::string& name, const std::vector<sema::Type>& params);
};
}
