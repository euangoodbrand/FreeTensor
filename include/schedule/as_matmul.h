#ifndef FREE_TENSOR_MAKE_MATMUL_H
#define FREE_TENSOR_MAKE_MATMUL_H

#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <analyze/analyze_linear.h>
#include <analyze/check_all_defined.h>
#include <analyze/symbol_table.h>
#include <container_utils.h>
#include <hash.h>
#include <mutator.h>

namespace freetensor {

enum class AsMatMulMode : int {
    KeepMemLayout,
    TryVarReorder,
    TryTranspose,
};
inline std::ostream &operator<<(std::ostream &os, AsMatMulMode mode) {
    switch (mode) {
    case AsMatMulMode::KeepMemLayout:
        return os << "keep_mem_layout";
    case AsMatMulMode::TryVarReorder:
        return os << "try_var_reorder";
    case AsMatMulMode::TryTranspose:
        return os << "try_transpose";
    default:
        ASSERT(false);
    }
}

struct NeedVarReorder : Error {
    ID vardef_;
    std::vector<int> order_;

    NeedVarReorder(const ID &vardef, const std::vector<int> &order,
                   const std::string &msg)
        : Error(genErrMsg(vardef, order, msg)), vardef_(vardef), order_(order) {
    }

  private:
    static std::string genErrMsg(const ID &vardef,
                                 const std::vector<int> &order,
                                 const std::string &msg) {
        std::ostringstream os;
        os << msg << ". Consider retrying after `var_reorder`ing " << vardef
           << " to order [" << order
           << "], or retrying with a different `mode` of `as_matmul`";
        return os.str();
    }
};

class AsMatMul : public SymbolTable<Mutator> {
    typedef SymbolTable<Mutator> BaseClass;

    ID loop_;
    MatMulBackend backend_;

    int nestCnt_ = 0;
    std::unordered_map<std::string, int> iterMap_; // iter var -> nest cnt
    std::unordered_set<std::string> outerDefs_;
    std::vector<VarDef> innerDefs_;
    std::vector<int> orderInit_;

    bool foundInit_ = false, foundLeaf_ = false, inside_ = false;
    Expr a_, b_, c_, initC_, m_, k_, n_, lda_, stridea_, ldb_, strideb_, ldc_,
        stridec_, batchSize_;
    bool aIsRowMajor_, bIsRowMajor_, cIsRowMajor_;

    AnalyzeLinear analyzeLinear_;

    ID resultId_;

    // Public matching details
    std::vector<bool> dimsABatch_, dimsBBatch_, dimsCBatch_, dimsAM_, dimsAK_,
        dimsBK_, dimsBN_, dimsCM_, dimsCN_;
    ID defIdA_, defIdB_, defIdC_;

  public:
    AsMatMul(const ID &loop, MatMulBackend backend)
        : loop_(loop), backend_(backend) {}

    bool done() const { return resultId_.isValid(); }
    const ID &resultId() const { return resultId_; }

    const auto &dimsABatch() const { return dimsABatch_; }
    const auto &dimsBBatch() const { return dimsBBatch_; }
    const auto &dimsCBatch() const { return dimsCBatch_; }
    const auto &dimsAM() const { return dimsAM_; }
    const auto &dimsAK() const { return dimsAK_; }
    const auto &dimsBK() const { return dimsBK_; }
    const auto &dimsBN() const { return dimsBN_; }
    const auto &dimsCM() const { return dimsCM_; }
    const auto &dimsCN() const { return dimsCN_; }
    const auto &defIdA() const { return defIdA_; }
    const auto &defIdB() const { return defIdB_; }
    const auto &defIdC() const { return defIdC_; }

  private:
    const LinearExpr<int64_t> &analyzeLinear(const Expr &expr);

    template <class T>
    std::tuple<std::vector<bool>, std::vector<int>, Expr>
    findIterUsedAndBaseAddr(const T &acc) {
        std::vector<bool> usedBy(nestCnt_, false);
        std::vector<int> order;
        Expr baseAddr = makeLoad(acc->var_, acc->indices_,
                                 buffer(acc->var_)->tensor()->dtype());
        for (auto &&[idx, dimLen, baseIdx] :
             views::zip(acc->indices_, buffer(acc->var_)->tensor()->shape(),
                        baseAddr.as<LoadNode>()->indices_)) {
            auto &&lin = analyzeLinear(idx);
            if (lin.coeff_.size() != 1 ||
                std::abs(lin.coeff_.front().k_) != 1 ||
                lin.coeff_.front().a_->nodeType() != ASTNodeType::Var) {
                if (!checkAllDefined(outerDefs_, idx)) {
                    throw InvalidSchedule("Indices of " + acc->var_ +
                                          " should be plain loop iterators");
                }
                continue; // not a dim in matmul
            }
            Var var = lin.coeff_.front().a_.template as<VarNode>();
            if (!iterMap_.count(var->name_)) {
                continue; // not a dim in matmul
            } else {
                baseIdx = makeIntConst(0);
            }
            int loopLevel = iterMap_.at(var->name_);
            if (!HashComparator()(loop(var->name_)->len_, dimLen)) {
                throw InvalidSchedule(
                    FT_MSG << "Iterator " << var->name_ << " of " << acc->var_
                           << " should loop over the entire range (" << dimLen
                           << "), instead of " << loop(var->name_)->len_);
            }
            usedBy[loopLevel] = true;
            order.emplace_back(loopLevel);
        }
        return std::make_tuple(usedBy, order, baseAddr);
    }

    template <class T>
    std::vector<bool> findDimsUsed(const T &acc,
                                   const std::vector<bool> &loopsUsed) {
        std::vector<bool> dimsUsed(acc->indices_.size(), false);
        for (auto &&[dimUsed, idx, dimLen] :
             views::zip(dimsUsed, acc->indices_,
                        buffer(acc->var_)->tensor()->shape())) {
            auto &&lin = analyzeLinear(idx);
            dimUsed = true;
            if (lin.coeff_.size() != 1 ||
                std::abs(lin.coeff_.front().k_) != 1 ||
                lin.coeff_.front().a_->nodeType() != ASTNodeType::Var) {
                dimUsed = false;
            } else {
                Var var = lin.coeff_.front().a_.template as<VarNode>();
                if (!iterMap_.count(var->name_) ||
                    !loopsUsed[iterMap_.at(var->name_)]) {
                    dimUsed = false;
                }
            }
        }
        return dimsUsed;
    }

    template <class T>
    std::pair<Expr, Expr> findLenAndStride(const T &acc,
                                           const std::vector<bool> &dimsIn) {
        Expr len, stride;
        bool lastDimIn = false;
        Expr lastInDim;
        for (auto &&[thisDimIn, idx, dimLen] : views::zip(
                 dimsIn, acc->indices_, buffer(acc->var_)->tensor()->shape())) {
            if (thisDimIn) {
                if (lastInDim.isValid()) {
                    if (!lastDimIn) {
                        throw InvalidSchedule(
                            FT_MSG << "Dimensions " << lastInDim << " and "
                                   << idx << " should be contiguous");
                    }
                }
                len = len.isValid() ? makeMul(len, dimLen) : (Expr)dimLen;
                lastInDim = idx;
            } else {
                if (len.isValid()) {
                    stride = stride.isValid() ? makeMul(stride, dimLen)
                                              : (Expr)dimLen;
                }
            }
            lastDimIn = thisDimIn;
        }
        len = len.isValid() ? len : makeIntConst(1);
        stride = stride.isValid() ? stride : makeIntConst(1);
        return std::make_pair(len, stride);
    }

    void checkSameOrderOrRetry(const ID &idA, const std::vector<int> &orderA,
                               const std::vector<bool> &filterA, const ID &idB,
                               const std::vector<int> &orderB,
                               const std::vector<bool> &filterB,
                               const std::string &message);
    void checkSameOrderNoRetry(const ID &idA, const std::vector<int> &orderA,
                               const std::vector<bool> &filterA, const ID &idB,
                               const std::vector<int> &orderB,
                               const std::vector<bool> &filterB,
                               const std::string &message);

    void retryReorderingBack(const ID &id, const std::vector<bool> &filter,
                             const std::string &message);
    void retryReorderingFront(const ID &id, const std::vector<bool> &filter,
                              const std::string &message);

  protected:
    Stmt visitStmt(const Stmt &op) override;
    Stmt visit(const For &op) override;
    Stmt visit(const ReduceTo &op) override;
    Stmt visit(const Store &op) override;
    Stmt visit(const VarDef &op) override;
};

Stmt asMatMul(const Stmt &ast, const ID &loop, MatMulBackend backend);

} // namespace freetensor

#endif // FREE_TENSOR_MAKE_MATMUL_H
