#ifndef FREE_TENSOR_SCALAR_PROP_CONST_H
#define FREE_TENSOR_SCALAR_PROP_CONST_H

#include <analyze/symbol_table.h>
#include <func.h>
#include <hash.h>
#include <mutator.h>
#include <pass/const_fold.h>

#include <map>

namespace freetensor {

/**
 * Mutator for propagating scalar values that are const or depend on iteration
 * variables only.
 *
 * Scalars are values in tensors indexed with constants, i.e.
 * this pass requires both indices and assigned value to be constants.
 */
class ScalarPropConst : public SymbolTable<ConstFold> {
  protected:
    typedef SymbolTable<ConstFold> BaseClass;

    struct ScalarIndex : public std::variant<int64_t, std::string> {
        template <class... Args>
        ScalarIndex(Args &&...args)
            : std::variant<int64_t, std::string>(std::forward<Args>(args)...) {}

        std::strong_ordering operator<=>(const ScalarIndex &other) const {
            switch (index() * 2 + other.index()) {
            case 0: // int int
                return std::get<int64_t>(*this) <=> std::get<int64_t>(other);
            case 1: // int str
                return std::strong_ordering::less;
            case 2: // str int
                return std::strong_ordering::greater;
            case 3: // str str
                return std::get<std::string>(*this) <=>
                       std::get<std::string>(other);
            default:
                ASSERT(false);
            }
        }
    };

    /**
     * @brief Indices to a scalar, includes a sequence of constant offsets.
     */
    struct ScalarIndices {
        std::vector<ScalarIndex> offset;

        /// Support comparison to use `std::map`.
        std::strong_ordering operator<=>(const ScalarIndices &other) const {
            ASSERT(offset.size() == other.offset.size() &&
                   "Index count should be identical for same tensor");
            return offset <=> other.offset;
        }

        bool operator==(const ScalarIndices &) const = default;
    };

    /**
     * @brief Try converting indices' AST nodes to constant indices.
     *
     * @param exprs AST nodes for indices
     * @return std::optional<ScalarIndices> Indices to the scalar, if all
     * indices are constant
     */
    std::optional<ScalarIndices> tryToScalar(const std::vector<Expr> &exprs);

    /// Scalar constants records, with first level map indexing var names and
    /// second indexing indices
    std::unordered_map<std::string, std::map<ScalarIndices, Expr>> constants_;

    /// Constant entries dependent on each iteration variable, mapping one
    /// iteration variable to multiple (var name, indices) pairs dependent on it
    std::unordered_multimap<std::string, std::pair<std::string, ScalarIndices>>
        iter_dep_constants_;

    void gen_constant(const std::string &name,
                      const std::optional<ScalarIndices> &indices,
                      const Expr &value);
    void kill_iter_dep_entry(const std::string &name,
                             const ScalarIndices &indices);
    void kill_constant(const std::string &name,
                       const std::optional<ScalarIndices> &indices);
    void kill_iter(const std::string &it_var);

    /**
     * @brief Intersect currently recorded scalar constants with provided map.
     *
     * This operation removes any record not found in `other` from the current
     * map.
     *
     * @param other The constants map to intersect
     * @return true The current constants are changed by this intersection
     * @return false The current constants remain unchanged in this intersection
     */
    bool intersect_constants_with(
        std::unordered_map<std::string, std::map<ScalarIndices, Expr>> other);

  protected:
    using BaseClass::visit;
    Stmt visit(const Store &store_orig) override;
    Stmt visit(const ReduceTo &op) override;
    Expr visit(const Load &load_orig) override;
    Stmt visit(const If &op) override;
    Stmt visit(const VarDef &vd) override;
    Stmt visit(const For &op) override;
};

/**
 * Propagate scalars of constant value or only depending on iteration variables.
 * Scalars are values in tensors indexed with constants.
 *
 * E.g. transform
 *
 * ```
 * x[0] = 1
 * y[0] = x[0]
 * ```
 *
 * into
 *
 * ```
 * x[0] = 1
 * y[0] = 1
 * ```
 *
 * This version of const propagation is designed for only scalars and meant to
 * be fast. It uses traditional dataflow techniques
 */
Stmt scalarPropConst(const Stmt &op);

DEFINE_PASS_FOR_FUNC(scalarPropConst)

} // namespace freetensor

#endif // FREE_TENSOR_SCALAR_PROP_CONST_H
