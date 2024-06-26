#ifndef FREE_TENSOR_CODE_GEN_CUDA_H
#define FREE_TENSOR_CODE_GEN_CUDA_H

#ifdef FT_WITH_CUDA

#include <unordered_map>
#include <unordered_set>

#include <codegen/code_gen_c.h>
#include <codegen/native_code.h>
#include <func.h>

namespace freetensor {

struct CodeGenCUDAStream : public CodeGenStream {
    std::unordered_map<ParallelScope, Expr> threadDim_;
    Expr sharedSize_ = makeIntConst(0);
};

class CodeGenCUDA : public CodeGenC<CodeGenCUDAStream> {
  public:
    typedef CodeGenCUDAStream Stream;

  private:
    Ref<GPUTarget> target_;
    std::string kernelPrefix_;
    int nKernel_ = 0;
    Expr sharedStackTop_ = makeIntConst(0);
    Expr globalStackTop_ = makeIntConst(0);
    Expr globalSize_ = makeIntConst(0);
    std::unordered_set<Stmt> streamScopes_;
    bool inMatmul_ = false;
    std::vector<std::string> neededMicroKernels_;

  public:
    CodeGenCUDA(const std::vector<FuncParam> &params,
                const std::vector<FuncRet> &returns,
                const Ref<GPUTarget> &target, const std::string &kernelPrefix)
        : CodeGenC(params, returns), target_(target),
          kernelPrefix_(kernelPrefix) {}

    using CodeGenC<CodeGenCUDAStream>::genMdPtrType;
    using CodeGenC<CodeGenCUDAStream>::genMdPtrDef;
    std::function<std::ostream &(std::ostream &)>
    genMdPtrType(const VarDef &def, bool isConst = false) override;
    void genMdPtrDef(const VarDef &def, const std::function<void()> &genRawPtr,
                     bool isConst = false) override;

    Expr globalSize() const { return globalSize_; }

    std::string gen(const DataType &dtype) override;

    const auto &neededMicroKernels() const { return neededMicroKernels_; }

  private:
    bool inKernel() const;

    void exprOr1(const std::unordered_map<ParallelScope, Expr> &dict,
                 const ParallelScope &key);

    void enterKernel(const Stmt &body);

    /**
     * Try to even put serial statments into a kernel to reduce kernel launch
     * overhead
     *
     * This is only applied to statements with a minimal semantic set that does
     * not rely on host features
     */
    bool canRunInKernel(const Stmt &stmt);

  protected:
    void genAlloc(const Ref<Tensor> &tensor, const std::string &rawPtr,
                  const std::string &shapePtr,
                  const std::string &dimPtr) override;

    using CodeGenC::genScalar;
    void genScalar(const VarDef &def,
                   const std::vector<Expr> &indices) override;

    using CodeGenC<CodeGenCUDAStream>::visit;
    void visitStmt(const Stmt &stmt) override;
    void visit(const Min &op) override;
    void visit(const Max &op) override;
    void visit(const Sqrt &op) override;
    void visit(const Exp &op) override;
    void visit(const Ln &op) override;
    void visit(const Sin &op) override;
    void visit(const Cos &op) override;
    void visit(const Tan &op) override;
    void visit(const Tanh &op) override;
    void visit(const Abs &op) override;
    void visit(const Floor &op) override;
    void visit(const Ceil &op) override;
    void visit(const Cast &op) override;
    void visit(const ReduceTo &op) override;
    void visit(const Var &op) override;
    void visit(const For &op) override;
    void visit(const VarDef &op) override;
    void visit(const MatMul &op) override;
    void visit(const Load &op) override;
    void visit(const Store &op) override;
    void visit(const Alloc &op) override;
    void visit(const Free &op) override;
};

/**
 * Generate target function code
 *
 * @return : source
 */
NativeCode codeGenCUDA(const Func &func, const Ref<Target> &target);

} // namespace freetensor

#endif // FT_WITH_CUDA

#endif // FREE_TENSOR_CODE_GEN_CUDA_H
