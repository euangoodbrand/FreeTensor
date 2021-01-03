#ifndef AST_H
#define AST_H

#include <ref.h>

namespace ir {

enum class ASTNodeType : int {
    Any,
    StmtSeq,
    VarDef,
    Var,
    Store,
    Load,
    AddTo,
    IntConst,
    FloatConst,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Min,
    Max,
    LT,
    LE,
    GT,
    GE,
    EQ,
    NE,
    Not,
    For,
    If,
    Assert,
};

#define DEFINE_NODE_ACCESS(name)                                               \
  protected:                                                                   \
    name##Node() = default; /* Must be constructed in Ref */                   \
                                                                               \
    friend class Ref<name##Node>;

#define DEFINE_NODE_TRAIT(name)                                                \
    DEFINE_NODE_ACCESS(name)                                                   \
  public:                                                                      \
    virtual ASTNodeType nodeType() const override { return ASTNodeType::name; }

class ASTNode {
  public:
    virtual ~ASTNode() {}
    virtual ASTNodeType nodeType() const = 0;

    DEFINE_NODE_ACCESS(AST);
};
typedef Ref<ASTNode> AST;

} // namespace ir

#endif // AST_H
