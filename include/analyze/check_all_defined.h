#ifndef FREE_TENSOR_CHECK_ALL_DEFINED_H
#define FREE_TENSOR_CHECK_ALL_DEFINED_H

#include <unordered_set>

#include <analyze/all_uses.h>
#include <ast.h>

namespace freetensor {

inline bool checkAllDefined(const std::unordered_set<std::string> &defs,
                            const std::unordered_set<std::string> &namesInOp) {
    return isSubSetOf(namesInOp, defs);
}

inline bool checkAllDefined(const std::unordered_set<std::string> &defs,
                            const AST &op) {
    return checkAllDefined(defs, allNames(op));
}

} // namespace freetensor

#endif // FREE_TENSOR_CHECK_ALL_DEFINED_H
