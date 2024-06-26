#ifdef FT_WITH_CUDA

#include <analyze/all_uses.h>
#include <analyze/comp_unique_bounds_combination.h>
#include <pass/gpu/normalize_thread_dims.h>

namespace freetensor {

namespace gpu {

bool NormalizeThreadDims::isLegalLen(const Expr &expr) {
    return isLegalLen(allNames(expr));
}

bool NormalizeThreadDims::isLegalLen(
    const std::unordered_set<std::string> &names) {
    for (auto &&name : names) {
        if (hasLoop(name)) {
            // Only iterators from outside of the kernel is OK
            if (openLoopsInKernel_.count(loop(name))) {
                return false;
            }
        } else if (!hasDef(name) || buffer(name)->mtype() != MemType::ByValue) {
            return false;
        }
    }
    return true;
}

Stmt NormalizeThreadDims::visit(const For &_op) {
    if (std::holds_alternative<CUDAScope>(_op->property_->parallel_)) {
        openLoopsInKernel_.insert(_op);

        auto oldInKernel = inKernel_;
        inKernel_ = true;
        auto __op = BaseClass::visit(_op);
        ASSERT(__op->nodeType() == ASTNodeType::For);
        auto op = __op.as<ForNode>();
        inKernel_ = oldInKernel;

        // CompUniqueBounds requires one instance per Stmt
        CompUniqueBoundsCombination bound(*this);

        std::unordered_set<std::string> allLegalNames;
        for (auto &&name : names()) {
            if (isLegalLen({name}))
                allLegalNames.emplace(name);
        }

        if (!isLegalLen(op->begin_)) {
            op->body_ = makeIf(
                makeUnbound(makeGE(makeVar(op->iter_), op->begin_)), op->body_);
            Expr begin = bound.getBound(op->begin_)
                             ->restrictScope(allLegalNames)
                             ->lowerExpr();
            if (!begin.isValid()) {
                throw InvalidProgram(
                    FT_MSG
                    << "Length of " << op->property_->parallel_
                    << " should have a finite bound. Note: if you are making a "
                       "dynamic ranged threadIdx or blockIdx loop, please use "
                       "memory type \"byvalue\" for its range, because it is "
                       "used both for launching the kernel and guarding the "
                       "execution inside the kernel");
            }
            op->begin_ = std::move(begin);
        }
        if (!isLegalLen(op->end_)) {
            op->body_ = makeIf(
                makeUnbound(makeLT(makeVar(op->iter_), op->end_)), op->body_);
            Expr end = bound.getBound(op->end_)
                           ->restrictScope(allLegalNames)
                           ->upperExpr();
            if (!end.isValid()) {
                throw InvalidProgram(
                    FT_MSG
                    << "Length of " << op->property_->parallel_
                    << " should have a finite bound. Note: if you are making a "
                       "dynamic ranged threadIdx or blockIdx loop, please use "
                       "memory type \"byvalue\" for its range, because it is "
                       "used both for launching the kernel and guarding the "
                       "execution inside the kernel");
            }
            op->end_ = std::move(end);
        }
        ASSERT(op->step_->nodeType() == ASTNodeType::IntConst &&
               op->step_.as<IntConstNode>()->val_ == 1);
        op->len_ = makeSub(op->end_, op->begin_);

        openLoopsInKernel_.erase(_op);
        return op;
    } else {
        if (inKernel_) {
            openLoopsInKernel_.insert(_op);
            auto ret = BaseClass::visit(_op);
            openLoopsInKernel_.erase(_op);
            return ret;
        } else {
            return BaseClass::visit(_op);
        }
    }
}

} // namespace gpu

} // namespace freetensor

#endif // FT_WITH_CUDA
