#ifndef FREE_TENSOR_MERGE_AND_HOIST_IF_H
#define FREE_TENSOR_MERGE_AND_HOIST_IF_H

#include <unordered_set>

#include <analyze/symbol_table.h>
#include <func.h>
#include <mutator.h>

namespace freetensor {

/**
 * Merge and hoist If nodes
 *
 * 1. Merge consecutive If nodes with the same condition
 *
 * E.g. Transform
 *
 * ```
 * if (x) { f(); }
 * if (x) { g(); }
 * ```
 *
 * into
 *
 * ```
 * if (x) {
 *   f();
 *   g();
 * }
 * ```
 *
 * 2. Hoist loop-independent If nodes
 *
 * E.g. Transform
 *
 * ```
 * for i = 0 to n {
 *   if (n < 100) { ... }
 * }
 * ```
 *
 * into
 *
 * ```
 * if (n < 100) {
 *   for i = 0 to n { ... }
 * }
 * ```
 *
 * We do not handle else-cases in this pass. Otherwise, the resulting code will
 * be too long. This is different from schedule/separate_tail
 */
class MergeAndHoistIf : public SymbolTable<Mutator> {
    typedef SymbolTable<Mutator> BaseClass;

    bool isFixPoint_ = true;

  public:
    bool isFixPoint() const { return isFixPoint_; }

  protected:
    Stmt visit(const StmtSeq &op) override;
    Stmt visit(const VarDef &op) override;
    Stmt visit(const For &op) override;
};

Stmt mergeAndHoistIf(const Stmt &op);

DEFINE_PASS_FOR_FUNC(mergeAndHoistIf)

} // namespace freetensor

#endif // FREE_TENSOR_MERGE_AND_HOIST_IF_H
