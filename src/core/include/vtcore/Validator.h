#pragma once

#include "vtcore/ObjectPool.h"

#include <string>
#include <vector>

namespace vt {

enum class Severity {
    Error,   // the pool violates ISO 11783-6 or cannot be encoded
    Warning, // the pool is valid but probably not what the designer intended
};

struct Diagnostic {
    Severity severity;
    ObjectId object; // kNullObjectId for pool-wide findings
    std::string message;
};

std::vector<Diagnostic> validatePool(const ObjectPool& pool);

bool hasErrors(const std::vector<Diagnostic>& diagnostics);

} // namespace vt
