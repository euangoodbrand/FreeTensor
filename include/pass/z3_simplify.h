#ifndef FREE_TENSOR_Z3_SIMPLIFY
#define FREE_TENSOR_Z3_SIMPLIFY

#include <deque>
#include <optional>
#include <unordered_map>

#include <z3++.h>

#include <analyze/symbol_table.h>
#include <func.h>
#include <hash.h>
#include <mutator.h>
#include <visitor.h>

namespace freetensor {

/**
 * Simplify the AST using Z3
 *
 * Comparing to SimplifyPass, Z3Simplify has the following features:
 *
 * - It can only simplify boolean expressions (e.g. remove redundant branches),
 * but it can NOT transform an expression to a simpler form (e.g. transform x +
 * x - x to x)
 * - It can deal with some more complex expressions, such as Mod
 * - It may take some more time
 *
 * Z3Simplify can work on a root-less sub-AST, but in this case, it will not
 * benefit from context from the missing ancestor nodes.
 *
 * Z3Simplify is conflict with SymbolTable. If you want to use these two classes
 * together, please use `Z3SimplifyWithSymbolTable`.
 */
class Z3Simplify : public Mutator {
    typedef Mutator BaseClass;

  private:
    int varCnt_ = 0;
    ASTHashMap<Expr, int> varId_;

    z3::context ctx_;
    z3::solver solver_;

    /**
     * Z3 expressions related to a Expr node
     *
     * All z3::expr is surrounded by std::optional, because there is no
     * z3::expr::expr()
     */
    struct ExprInfo {
        /// Z3 expression that expresses the node itself
        std::optional<z3::expr> self_;

        /// Conditions related to the node. These conditions together with
        /// conditions in the solver stack make up all conditions
        std::vector<std::optional<z3::expr>> conds_;
    };

    // We use std::optional because there is no z3::expr::expr()
    std::unordered_map<Expr, ExprInfo> z3Exprs_;

  public:
    Z3Simplify() : solver_(ctx_) {}

  protected:
    int getVarId(const Expr &op);

    void put(const Expr &key, const z3::expr &expr,
             const std::vector<std::optional<z3::expr>> &conds = {});
    bool exists(const Expr &key);
    const z3::expr &get(const Expr &key);
    const std::vector<std::optional<z3::expr>> &conds(const Expr &key);

    void push(const Expr &op);
    void pop();

    bool prove(const Expr &op);

    using Mutator::visit;

    Expr visit(const Var &op) override;
    Expr visit(const Load &op) override;
    // TODO: Cast can also be treated as Load
    Expr visit(const IntConst &op) override;
    Expr visit(const BoolConst &op) override;
    Expr visit(const Add &op) override;
    Expr visit(const Sub &op) override;
    Expr visit(const Mul &op) override;
    Expr visit(const FloorDiv &op) override;
    Expr visit(const CeilDiv &op) override;
    Expr visit(const Mod &op) override;
    Expr visit(const Min &op) override;
    Expr visit(const Max &op) override;
    Expr visit(const LT &op) override;
    Expr visit(const LE &op) override;
    Expr visit(const GT &op) override;
    Expr visit(const GE &op) override;
    Expr visit(const EQ &op) override;
    Expr visit(const NE &op) override;
    Expr visit(const LAnd &op) override;
    Expr visit(const LOr &op) override;
    Expr visit(const LNot &op) override;
    Expr visit(const IfExpr &op) override;

    Stmt visit(const If &op) override;
    Stmt visit(const Assert &op) override;
    Stmt visit(const Assume &op) override;
    Stmt visit(const For &op) override;
};

/**
 * Compatible inheritence of both Z3Simplify and SymbolTable
 */
class Z3SimplifyWithSymbolTable : public Z3Simplify,
                                  public SymbolTableInterface {
    SymbolTableData symbols_;

  public:
    const std::unordered_set<std::string> &names() const override {
        return symbols_.names();
    }
    const std::unordered_map<std::string, VarDef> &defs() const override {
        return symbols_.defs();
    }
    const std::unordered_map<std::string, For> &loops() const override {
        return symbols_.loops();
    }

    bool hasDef(const std::string &name) const override {
        return symbols_.hasDef(name);
    }
    const VarDef &def(const std::string &name) const override {
        return symbols_.def(name);
    }
    Ref<Buffer> buffer(const std::string &name) const override {
        return symbols_.buffer(name);
    }

    bool hasLoop(const std::string &name) const override {
        return symbols_.hasLoop(name);
    }
    const For &loop(const std::string &name) const override {
        return symbols_.loop(name);
    }

    void pushDef(const VarDef &op) override { symbols_.pushDef(op); }
    void popDef(const VarDef &op) override { symbols_.popDef(op); }

    void pushFor(const For &op) override { symbols_.pushFor(op); }
    void popFor(const For &op) override { symbols_.popFor(op); }

  protected:
    using Z3Simplify::visit;
    Stmt visit(const VarDef &op) override;
    Stmt visit(const For &op) override;
};

Stmt z3Simplify(const Stmt &op);

DEFINE_PASS_FOR_FUNC(z3Simplify)

} // namespace freetensor

#endif // FREE_TENSOR_Z3_SIMPLIFY
