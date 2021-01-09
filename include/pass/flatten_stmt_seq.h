#ifndef FLATTEN_STMT_SEQ_H
#define FLATTEN_STMT_SEQ_H

#include <mutator.h>

namespace ir {

class FlattenStmtSeq : public Mutator {
    bool popVarDef_; // { A; VarDef { B; }} -> VarDef { A; B; }

  public:
    FlattenStmtSeq(bool popVarDef) : popVarDef_(popVarDef) {}

  protected:
    Stmt visit(const StmtSeq &op) override;
};

inline Stmt flattenStmtSeq(const Stmt &op, bool popVarDef = false) {
    return FlattenStmtSeq(popVarDef)(op);
}

} // namespace ir

#endif // FLATTEN_STMT_SEQ_H
