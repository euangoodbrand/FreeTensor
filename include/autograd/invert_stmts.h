#ifndef FREE_TENSOR_INVERT_STMTS_H
#define FREE_TENSOR_INVERT_STMTS_H

#include <unordered_map>
#include <unordered_set>

#include <analyze/symbol_table.h>
#include <autograd/derivative.h>
#include <visitor.h>

namespace freetensor {

struct InversionInfo {
    Stmt inv_;
    Expr cond_; // null for always invert
};

/**
 * Find out which statements can be inverted
 */
class FindInvertibles : public SymbolTable<Visitor> {
    typedef SymbolTable<Visitor> BaseClass;

    // Original ID -> Inverse Statement
    std::unordered_map<ID, Stmt> invertibles_;

  public:
    const auto &invertibles() const { return invertibles_; }

  protected:
    using BaseClass::visit;
    // TODO 1: Support invertibles in Store.
    // TODO 2: We now always invert first, and then compute gradient. Things
    // will go wrong after we support Store, where we may use y for gradient.
    void visit(const ReduceTo &op) override;
};

/**
 * Given a set of statements we want to get their value in the backward pass,
 * try to recover as more as possbile of them by inverting statements that
 * overwrites them. The rest of the statements have to be recomputed in the
 * traditional way.
 *
 * @param op : The AST
 * @param idsNeeded : {ID of VarDef -> IDs of statements to tape or recompute}.
 * This parameter is updated in place
 * @param derivatives : Map generated by `autograd/derivative.h`, updated in
 * place here
 * @return : (A modified AST for version analysis, {ID -> inversion})
 */
std::tuple<Stmt, std::unordered_map<ID, InversionInfo>>
invertStmts(const Stmt &op,
            std::unordered_map<ID, std::unordered_set<ID>> *idsNeeded,
            std::unordered_map<StmtOrExprID, Derivative::LazyFullDerivative>
                *derivatives);

} // namespace freetensor

#endif // FREE_TENSOR_INVERT_STMTS_H
