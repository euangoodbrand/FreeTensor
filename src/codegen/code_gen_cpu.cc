#include <codegen/code_gen_cpu.h>
#include <pass/simplify.h>

#include "detail/code_gen_c.h"

namespace ir {

#ifdef WITH_MKL

static char genMKLTypeMark(DataType dtype) {
    switch (dtype) {
    case DataType::Float32:
        return 's';
    default:
        ASSERT(false);
    }
}

#endif

void CodeGenCPU::genAlloc(const Tensor &tensor, const std::string &rawPtr,
                          const std::string &sizePtr) {
    makeIndent();
    os() << rawPtr << " = malloc(" << sizePtr << " = ";
    for (auto &&dim : tensor.shape()) {
        (*this)(dim);
        os() << " * ";
    }
    os() << "sizeof(" << gen(tensor.dtype()) << "));" << std::endl;
}

void CodeGenCPU::visit(const VarDef &op) {
    if (op->buffer_->atype() == AccessType::Cache) {
        auto &&tensor = op->buffer_->tensor();
        auto &&shape = tensor.shape();
        int64_t size = sizeOf(tensor.dtype());
        for (auto &&dim : shape) {
            if (dim->nodeType() == ASTNodeType::IntConst) {
                size *= dim.as<IntConstNode>()->val_;
            } else {
                WARNING(
                    "Cannot calculate size for a dynamic-sized local "
                    "(MemType::Cache) array in order to set a stack limit. If "
                    "this array is large, it may result in a stack overflow");
            }
        }
        stackSize_ = std::max(stackSize_, stackTop_ + size);
        stackTop_ += size;
        CodeGenC::visit(op);
        stackTop_ -= size;
    } else {
        CodeGenC::visit(op);
    }
}

void CodeGenCPU::visit(const ReduceTo &op) {
    if (op->atomic_) {
        os() << "#pragma omp atomic" << std::endl;
        // FIXME: OpenMP supports atomic min and max only for FORTRAN
    }
    CodeGenC::visit(op);
}

void CodeGenCPU::visit(const For &op) {
    if (op->property_.parallel_ == "openmp") {
        os() << "#pragma omp parallel for";
        if (!op->property_.reductions_.empty()) {
            for (size_t i = 1, n = op->property_.reductions_.size(); i < n;
                 i++) {
                if (op->property_.reductions_[i].op_ !=
                    op->property_.reductions_.front().op_) {
                    throw InvalidProgram(
                        "Reduction operators of each parallel reduction "
                        "variables should be the same in a single OpenMP loop");
                }
            }
            os() << " reduction(";
            switch (op->property_.reductions_.front().op_) {
            case ReduceOp::Add:
                os() << "+: ";
                break;
            case ReduceOp::Mul:
                os() << "*: ";
                break;
            case ReduceOp::Min:
                os() << "min: ";
                break;
            case ReduceOp::Max:
                os() << "max: ";
                break;
            default:
                ASSERT(false);
            }
            bool first = true;
            for (auto &&[redOp, var, indices] : op->property_.reductions_) {
                if (!first) {
                    os() << ", ";
                }
                first = false;
                os() << var;
                if (!indices.empty()) {
                    os() << "[";
                    bool first_2 = true;
                    for (auto &&idx : indices) {
                        if (!first_2) {
                            os() << ", ";
                        }
                        first_2 = false;
                        if (idx.isValid()) {
                            (*this)(idx);
                        } else {
                            os() << ":";
                        }
                    }
                    os() << "]";
                }
            }
            os() << ")";
        }
        os() << std::endl;
        bool oldInParallel = inParallel_;
        inParallel_ = true;
        CodeGenC::visit(op);
        inParallel_ = oldInParallel;
        return;
    } else if (op->property_.vectorize_) {
        os() << "#pragma omp simd" << std::endl;
    } else if (op->property_.unroll_) {
        os() << "#pragma GCC unroll " << op->len_ << std::endl;
    }
    CodeGenC::visit(op);
}

void CodeGenCPU::visit(const MatMul &op) {
#ifdef WITH_MKL
    makeIndent();
    if (inParallel_) {
        os() << "mkl_set_num_threads_local(1);" << std::endl;
        // TODO: set it to max(1, cpu_count / outer_threads_count)
    } else {
        os() << "mkl_set_num_threads_local(0); // 0 == reset" << std::endl;
    }

    auto d = dtype(op->c_);
    if (dtype(op->a_) != d || dtype(op->b_) != d) {
        throw InvalidProgram(
            "MKL requires all matrices have the same data type");
    }

    bool transA = !op->aIsRowMajor_, transB = !op->bIsRowMajor_;
    Expr a = op->a_, b = op->b_, c = op->c_;
    Expr m = op->m_, k = op->k_, n = op->n_;
    Expr lda = op->lda_, ldb = op->ldb_, ldc = op->ldc_;
    Expr stridea = op->stridea_, strideb = op->strideb_, stridec = op->stridec_;
    if (!op->cIsRowMajor_) {
        transA = !transA;
        transB = !transB;
        std::swap(transA, transB);
        std::swap(a, b);
        std::swap(lda, ldb);
        std::swap(stridea, strideb);
        std::swap(n, m);
    }

    makeIndent();
    os() << "cblas_" << genMKLTypeMark(d)
         << "gemm_batch_strided(CblasRowMajor, "
         << (transA ? "CblasTrans" : "CblasNoTrans") << ", "
         << (transB ? "CblasTrans" : "CblasNoTrans") << ", ";
    (*this)(m);
    os() << ", ";
    (*this)(n);
    os() << ", ";
    (*this)(k);
    os() << ", ";
    (*this)(op->alpha_);
    os() << ", &";
    (*this)(a);
    os() << ", ";
    (*this)(lda);
    os() << ", ";
    (*this)(stridea);
    os() << ", &";
    (*this)(b);
    os() << ", ";
    (*this)(ldb);
    os() << ", ";
    (*this)(strideb);
    os() << ", ";
    (*this)(op->beta_);
    os() << ", &";
    (*this)(c);
    os() << ", ";
    (*this)(ldc);
    os() << ", ";
    (*this)(stridec);
    os() << ", ";
    (*this)(op->batchSize_);
    os() << ");" << std::endl;
#else
    ERROR("Configuring with MKL is needed");
#endif
}

std::string codeGenCPU(const Func &func) {
    CodeGenCPU visitor(func->params_, func->returns_);
    auto &&op = func->body_;
    visitor.beginBlock();
    visitor(op);
    visitor.endBlock();

    // TODO: Pure C?
    const char *header = R"~~~(
#include <cpu_runtime.h>

extern "C" {
)~~~";
    const char *tailer = R"~~~(
}
)~~~";

    auto body = visitor.toString([&](const CodeGenStream &stream) {
        std::string s = "void __attribute__ ((noinline)) _run(void **_params, "
                        "void **_returns, size_t *_retSizes) " +
                        stream.os_.str();
        s += "\n";
        s += "void run(void **_params, void **_returns, size_t *_retSizes, "
             "CPUContext_t _ctx) {\n";
        s += "  _ctx->setStackLim(" + std::to_string(visitor.stackSize()) +
             ");\n";
        s += "  _run(_params, _returns, _retSizes);\n";
        s += "}";
        return s;
    });
    return header + body + tailer;
}

} // namespace ir
