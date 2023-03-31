#include <analyze/symbol_table.h>
#include <autograd/analyze_version.h>
#include <autograd/output_intermediates.h>

namespace freetensor {

namespace {

class AllWritesToDefs : public SymbolTable<Visitor> {
    typedef SymbolTable<Visitor> BaseClass;

    const std::unordered_set<ID> &defs_;
    std::unordered_map<ID, std::unordered_set<ID>> writes_;

  public:
    AllWritesToDefs(const std::unordered_set<ID> &defs) : defs_(defs) {}

    const auto &writes() const { return writes_; }

  protected:
    using BaseClass::visit;

    void visit(const Store &op) override {
        BaseClass::visit(op);
        if (defs_.count(def(op->var_)->id())) {
            writes_[def(op->var_)->id()].emplace(op->id());
        }
    }

    void visit(const ReduceTo &op) override {
        BaseClass::visit(op);
        if (defs_.count(def(op->var_)->id())) {
            writes_[def(op->var_)->id()].emplace(op->id());
        }
    }
};

} // Anonymous namespace

static MemType toGlobalMemType(MemType mtype) {
    switch (mtype) {
    case MemType::CPU:
        return MemType::CPU;
    case MemType::GPULocal:
    case MemType::GPUShared:
    case MemType::GPUGlobal:
        return MemType::GPUGlobal;
    default:
        ASSERT(false);
    }
}

std::string OutputIntermediates::savingName(const std::string &oldName) const {
    return oldName + varSuffix_;
}

Stmt OutputIntermediates::visitStmt(const Stmt &stmt) {
    curStmt_ = stmt->id();
    auto ret = BaseClass::visitStmt(stmt);
    if (toSave_.count(stmt->id())) {
        auto &toSave = toSave_.at(stmt->id());
        toSave.emplace_back(ret);
        return makeStmtSeq(std::move(toSave));
    }
    return ret;
}

Expr OutputIntermediates::visit(const Load &op) {
    auto ret = BaseClass::visit(op);
    auto id = StmtOrExprID(op, curStmt_);
    if (versions_.count(id) && !trivials_.count(def(op->var_)->id())) {
        std::vector<Expr> newIndices(1, versions_.at(id));
        newIndices.insert(newIndices.end(), op->indices_.begin(),
                          op->indices_.end());
        auto store = makeStore(savingName(op->var_), std::move(newIndices),
                               makeLoad(op->var_, op->indices_, op->loadType_));
        toSave_[curStmt_].emplace_back(store);
        insertedStmts_.insert(store->id());
    }
    return ret;
}

Stmt OutputIntermediates::visit(const Store &op) {
    auto oldStore = BaseClass::visit(op);
    if (versions_.count(op->id()) && !trivials_.count(def(op->var_)->id())) {
        std::vector<Expr> newIndices(1, versions_.at(op->id()));
        newIndices.insert(newIndices.end(), op->indices_.begin(),
                          op->indices_.end());
        auto newStore =
            makeStore(savingName(op->var_), std::move(newIndices),
                      makeLoad(op->var_, op->indices_,
                               buffer(op->var_)->tensor()->dtype()));
        insertedStmts_.insert(newStore->id());
        return makeStmtSeq({oldStore, newStore});
    } else {
        return oldStore;
    }
}

Stmt OutputIntermediates::visit(const ReduceTo &op) {
    auto oldReduce = BaseClass::visit(op);
    if (versions_.count(op->id()) && !trivials_.count(def(op->var_)->id())) {
        std::vector<Expr> newIndices(1, versions_.at(op->id()));
        newIndices.insert(newIndices.end(), op->indices_.begin(),
                          op->indices_.end());
        auto newStore =
            makeStore(savingName(op->var_), std::move(newIndices),
                      makeLoad(op->var_, op->indices_,
                               buffer(op->var_)->tensor()->dtype()));
        insertedStmts_.insert(newStore->id());
        return makeStmtSeq({oldReduce, newStore});
    } else {
        return oldReduce;
    }
}

Stmt OutputIntermediates::visit(const VarDef &_op) {
    if (totLens_.count(_op->id())) {
        // FIXME: What if the scopeLen_ is a loop-variant temporary?
        if (trivials_.count(_op->id())) {
            // No need to create a new VarDef
            auto __op = BaseClass::visit(_op);
            ASSERT(__op->nodeType() == ASTNodeType::VarDef);
            auto op = __op.as<VarDefNode>();

            savedNames_[op->id()] = op->name_;
            if (stage_ == OutputIntermediatesStage::Forward) {
                op->buffer_->setAtype(addOutputting(op->buffer_->atype()));
            }
            return op;
        } else {
            auto __op = BaseClass::visit(_op);
            ASSERT(__op->nodeType() == ASTNodeType::VarDef);
            auto op = __op.as<VarDefNode>();

            auto savedName = savedNames_[op->id()] = savingName(op->name_);
            Ref<Tensor> tensor = deepCopy(op->buffer_->tensor());
            tensor->shape().insert(tensor->shape().begin(),
                                   totLens_.at(op->id()));
            if (stage_ == OutputIntermediatesStage::Forward) {
                return makeVarDef(
                    savedName,
                    makeBuffer(std::move(tensor), AccessType::Output,
                               toGlobalMemType(op->buffer_->mtype())),
                    std::nullopt, op, false);
            } else {
                return makeVarDef(savedName,
                                  makeBuffer(std::move(tensor),
                                             op->buffer_->atype(),
                                             op->buffer_->mtype()),
                                  std::nullopt, op, false);
            }
        }
    } else {
        return BaseClass::visit(_op);
    }
}

std::tuple<Stmt, std::unordered_map<ID, std::string>,
           std::unordered_map<StmtOrExprID, Expr>, std::unordered_map<ID, Expr>,
           std::unordered_set<ID>,
           std::unordered_map<std::string, std::pair<std::string, Expr>>>
outputIntermediates(
    const Stmt &op,
    const std::unordered_map<ID, std::unordered_set<ID>> &needVersions,
    const std::unordered_map<StmtOrExprID, Derivative::LazyFullDerivative>
        &derivatives,
    OutputIntermediatesStage stage, const std::string &varSuffix) {
    auto [versions, totLens, trivials, userVersions] =
        analyzeVersion(op, needVersions, derivatives,
                       stage == OutputIntermediatesStage::Backward);
    OutputIntermediates mutator(versions, totLens, trivials, stage, varSuffix);
    auto ret = mutator(op);
    return {ret,     mutator.savedNames(),    versions,
            totLens, mutator.insertedStmts(), userVersions};
}

Stmt outputAllIntermedaites(const Stmt &op,
                            const std::unordered_set<ID> &intermediates,
                            OutputIntermediatesStage stage,
                            const std::string &varSuffix) {
    AllWritesToDefs visitor{intermediates};
    visitor(op);
    return std::get<0>(
        outputIntermediates(op, visitor.writes(), {}, stage, varSuffix));
}

} // namespace freetensor
