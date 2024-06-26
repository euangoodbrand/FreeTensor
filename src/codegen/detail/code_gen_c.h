#ifndef DETAIL_CODE_GEN_C_H
#define DETAIL_CODE_GEN_C_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include <analyze/find_stmt.h>
#include <codegen/code_gen_c.h>
#include <config.h>
#include <container_utils.h>
#include <serialize/mangle.h>

#include "code_gen.h"

namespace freetensor {

template <class Stream>
std::function<std::ostream &(std::ostream &)>
CodeGenC<Stream>::genMdPtrType(const VarDef &def, bool isConst) {
    // NOTE: `[=]` implicitly capturing `this` is deprecated in C++20. Using
    // `[=]` will trigger a warning in GCC (because of deprecation), but using
    // `[=, this]` will trigger a warning in Clang<17 (because it will think
    // `this` is duplicated).
#if defined(__clang__) && __clang_major__ < 17
    return [=](std::ostream &os) -> std::ostream & {
#else
    return [=, this](std::ostream &os) -> std::ostream & {
#endif
        auto &&buf = def->buffer_;

        if (buf->tensor()->shape().empty()) {
            // Use reference for scalars
            if (isConst) {
                os << "const ";
            }
            os << gen(buf->tensor()->dtype()) << " &";
            return os;
        }

        bool isRestricted = true;
        if (def->viewOf_.has_value() ||
            !findAllStmt(def, [&](const Stmt &inner) {
                 return inner->nodeType() == ASTNodeType::VarDef &&
                        inner.as<VarDefNode>()->viewOf_ == def->name_;
             }).empty()) {
            isRestricted = false;
        }

        os << (Config::debugRuntimeCheck() ? "mdspan_dbg<"
               : isRestricted              ? "mdspan_r<"
                                           : "mdspan<");
        if (isConst) {
            os << "const ";
        }
        os << gen(buf->tensor()->dtype()) << ", extents<";
        for (auto &&[i, dim] : views::enumerate(buf->tensor()->shape())) {
            os << (i > 0 ? ", " : "");
            if (dim->nodeType() == ASTNodeType::IntConst) {
                os << dim.template as<IntConstNode>()->val_;
            } else {
                os << "dynamic_extent";
            }
        }
        return os << ">>";
    };
}

template <class Stream>
void CodeGenC<Stream>::genMdPtrDef(const VarDef &def,
                                   const std::function<void()> &genRawPtr,
                                   bool isConst) {
    auto &&buf = def->buffer_;

    if (buf->tensor()->shape().empty()) {
        // Use reference for scalars
        // e.g.
        // ((const int32_t &)*((const int32_t *)(...)))
        this->os() << "((" << genMdPtrType(def, isConst) << ")*((";
        if (isConst) {
            this->os() << "const ";
        }
        this->os() << gen(buf->tensor()->dtype()) << " *)(";
        genRawPtr();
        this->os() << ")))";
        return;
    }

    this->os() << genMdPtrType(def, isConst) << "((";
    if (isConst) {
        this->os() << "const ";
    }
    this->os() << gen(buf->tensor()->dtype()) << "*)(";
    genRawPtr();
    this->os() << ")";
    for (auto &&dim : buf->tensor()->shape()) {
        if (dim->nodeType() != ASTNodeType::IntConst) {
            this->os() << ", ";
            (*this)(dim);
        }
    }
    this->os() << ")";
}

template <class Stream>
void CodeGenC<Stream>::genScalar(const VarDef &def,
                                 const std::vector<Expr> &indices) {
    if (def->buffer_->mtype() == MemType::ByValue) {
        // __ByValArray
        this->os() << mangle(def->name_);
        for (auto &&index : indices) {
            this->os() << "[";
            (*this)(index);
            this->os() << "]";
        }
    } else {
        this->os() << mangle(def->name_);
        if (!def->buffer_->tensor()->shape().empty()) {
            // TODO: Switch bracket after C++23
            this->os() << "(";
            for (auto &&[i, index] : views::enumerate(indices)) {
                this->os() << (i > 0 ? ", " : "");
                (*this)(index);
            }
            this->os() << ")";
        }
    }
}

template <class Stream> void CodeGenC<Stream>::visit(const StmtSeq &op) {
    for (auto &&stmt : op->stmts_) {
        if (stmt->nodeType() == ASTNodeType::VarDef) {
            this->makeIndent();
            this->beginBlock();
            (*this)(stmt);
            this->endBlock();
        } else {
            (*this)(stmt);
        }
    }
}

template <class Stream> void CodeGenC<Stream>::visit(const VarDef &op) {
    this->makeIndent();
    auto &&tensor = op->buffer_->tensor();
    auto &&shape = tensor->shape();
    auto name = mangle(op->name_);

    if (op->viewOf_.has_value()) {
        // e.g.
        // auto &&x = mdspan_r<const float, extents<5, 5>>(y.data_handle());
        auto source = op;
        while (source->viewOf_.has_value()) {
            source = this->def(*source->viewOf_);
        }
        this->os() << "auto &&" << name << " = ";
        genMdPtrDef(op, mangle(source->name_) + ".data_handle()",
                    !isWritable(source->buffer_->atype()));
        this->os() << ";" << std::endl;

    } else if (!isInputting(op->buffer_->atype()) &&
               !isOutputting(op->buffer_->atype())) {
        // e.g. 1. float x;
        //      2. float x[5][5][5];
        this->os() << gen(tensor->dtype()) << " " << name;
        for (auto &&dim : shape) {
            this->os() << "[";
            (*this)(dim);
            this->os() << "]";
        }
        this->os() << ";" << std::endl;
    } else {
        auto paramPositions = ranges::to<std::vector>(
            params_ | views::enumerate | views::filter([&](auto &&pair) {
                return pair.second.name_ == op->name_;
            }) |
            views::keys);
        auto returnPositions = ranges::to<std::vector>(
            returns_ | views::enumerate | views::filter([&](auto &&pair) {
                return pair.second.name_ == op->name_;
            }) |
            views::keys);
        bool isParam = !paramPositions.empty();
        bool isReturn = !returnPositions.empty();
        if (!isParam && !isReturn) {
            throw InvalidProgram("I/O variable " + op->name_ +
                                 " used but not defined as a function's "
                                 "parameters or return values");
        }
        std::string rawPtr;
        if (isParam) {
            if (paramPositions.size() > 1) {
                throw InvalidProgram("Parameter '" + op->name_ +
                                     "' is duplicated");
            }
            int nthParam = paramPositions.front();
            rawPtr = "params[" + std::to_string(nthParam) + "]";
        } else {
            if (!isOutputting(op->buffer_->atype())) {
                throw InvalidProgram(
                    "Only outputting variable can be as a return value");
            }
            // If there are multiple position with the same name, we only fill
            // the first position. Driver::collectReturns will only collect the
            // first
            int nthReturn = returnPositions.front();
            rawPtr = "returns[" + std::to_string(nthReturn) + "]";
            std::string shapePtr =
                "retShapes[" + std::to_string(nthReturn) + "]";
            std::string dimPtr = "retDims[" + std::to_string(nthReturn) + "]";
            this->os() << "if (" + rawPtr + " == NULL) ";
            this->beginBlock();
            this->genAlloc(op->buffer_->tensor(), rawPtr, shapePtr, dimPtr);
            this->endBlock();
            this->makeIndent();
        }

        switch (op->buffer_->mtype()) {
        case MemType::ByValue:
            // e.g. (1)
            // float x;
            // x = *((float*)params[0]);

            // e.g. (2)
            // __ByValArray<__ByValArray<float, 2>, 2> x;
            // x[0][0] = *((float*)params[0])[0];
            // x[0][1] = *((float*)params[0])[1];
            // x[1][0] = *((float*)params[0])[2];
            // x[1][1] = *((float*)params[0])[3];
            if (op->buffer_->atype() != AccessType::Input) {
                throw InvalidProgram("ByValue typed var " + op->name_ +
                                     " can only be Input");
            }
            for (auto &&dim : shape) {
                if (dim->nodeType() != ASTNodeType::IntConst) {
                    throw InvalidProgram("ByValue typed var " + op->name_ +
                                         " can only have a constant size");
                }
            }
            if (shape.empty()) {
                this->os() << gen(tensor->dtype()) << " " << name << " = *(("
                           << gen(tensor->dtype()) << "*)" << rawPtr << ");"
                           << std::endl;
            } else {
                for (size_t i = 0, iEnd = shape.size(); i < iEnd; i++) {
                    this->os() << "__ByValArray<";
                }
                this->os() << gen(tensor->dtype());
                for (auto it = shape.rbegin(); it != shape.rend(); it++) {
                    this->os() << ", " << (*it).as<IntConstNode>()->val_ << ">";
                }
                this->os() << " " << name << ";" << std::endl;
                std::vector<int> idx(shape.size(), 0);
                std::function<void(size_t, int)> f = [&](size_t i, int offset) {
                    if (i == shape.size()) {
                        this->makeIndent();
                        this->os() << name;
                        for (int x : idx) {
                            this->os() << "[" << x << "]";
                        }
                        this->os()
                            << " = ((" << gen(tensor->dtype()) << "*)" << rawPtr
                            << ")[" << offset << "];" << std::endl;
                        return;
                    }
                    for (int j = 0, jEnd = shape[i].as<IntConstNode>()->val_;
                         j < jEnd; j++) {
                        idx[i] = j;
                        f(i + 1, offset * jEnd + j);
                    }
                };
                f(0, 0);
            }
            break;

        default:
            // e.g.
            // auto &&x = mdspan_r<const float, extents<5, 5>>(params[0]);
            this->os() << "auto &&" << name << " = ";
            genMdPtrDef(op, rawPtr, !isWritable(op->buffer_->atype()));
            this->os() << ";" << std::endl;
        }
    }

    this->markDef(op);
    (*this)(op->body_);
    this->markUndef(op);
}

template <class Stream> void CodeGenC<Stream>::visit(const Var &op) {
    this->markUseIter(op->name_);
    this->os() << mangle(op->name_);
    BaseClass::visit(op);
}

template <class Stream> void CodeGenC<Stream>::visit(const Store &op) {
    this->markUse(op->var_);

    this->makeIndent();
    this->genScalar(op);
    this->os() << " = ";
    (*this)(op->expr_);
    this->os() << ";" << std::endl;
}

template <class Stream> void CodeGenC<Stream>::visit(const Alloc &op) {
    this->markUse(op->var_);
    this->makeIndent();

    auto &&def = BaseClass::def(op->var_);
    auto &&tensor = def->buffer_->tensor();
    auto &&shape = tensor->shape();
    auto &&dtype = tensor->dtype();

    // e.g.
    // x_opt = mdspan_r<int, extents<5, 5>>(new int[n*m*l]);
    this->os() << mangle(op->var_) << "_opt = ";
    genMdPtrDef(def, [&]() {
        this->os() << "new " << gen(dtype) << "[";
        for (auto i = 0lu; i < shape.size(); ++i) {
            if (i != 0lu)
                this->os() << "*";
            this->os() << "(";
            (*this)(shape[i]);
            this->os() << ")";
        }
        this->os() << "]";
    });
    this->os() << ";" << std::endl;
}

template <class Stream> void CodeGenC<Stream>::visit(const Free &op) {

    // e.g. auto x_ptr = x.data_handle();
    //      x_opt.drop();
    //      x_opt = std::nullopt;
    //      delete[] x_ptr;
    auto &&name = mangle(op->var_);
    this->makeIndent();
    this->os() << "auto " << name << "_ptr = " << name << ".data_handle();"
               << std::endl;
    this->makeIndent();
    this->os() << name << "_opt.drop();" << std::endl;
    this->makeIndent();
    this->os() << name << "_opt = std::nullopt;" << std::endl;
    this->makeIndent();
    this->os() << "delete[] " << name << "_ptr;" << std::endl;
}

template <class Stream> void CodeGenC<Stream>::visit(const Load &op) {
    this->markUse(op->var_);
    this->genScalar(op);
}

template <class Stream> void CodeGenC<Stream>::visit(const ReduceTo &op) {
    this->markUse(op->var_);

    this->makeIndent();

    auto genAddr = [&]() { this->genScalar(op); };
    auto genExpr = [&]() { (*this)(op->expr_); };

    switch (op->op_) {
    case ReduceOp::Add:
        genAddr(), this->os() << " += ", genExpr();
        break;
    case ReduceOp::Mul:
        genAddr(), this->os() << " *= ", genExpr();
        break;
    case ReduceOp::Min:
        genAddr(), this->os()
                       << " = std::min<"
                       << this->gen(this->buffer(op->var_)->tensor()->dtype())
                       << ">(";
        genAddr(), this->os() << ", ", genExpr(), this->os() << ")";
        break;
    case ReduceOp::Max:
        genAddr(), this->os()
                       << " = std::max<"
                       << this->gen(this->buffer(op->var_)->tensor()->dtype())
                       << ">(";
        genAddr(), this->os() << ", ", genExpr(), this->os() << ")";
        break;
    case ReduceOp::LAnd:
        genAddr(), this->os() << " &= (bool)(", genExpr(), this->os() << ")";
        break;
    case ReduceOp::LOr:
        genAddr(), this->os() << " |= (bool)(", genExpr(), this->os() << ")";
        break;
    default:
        ASSERT(false);
    }

    this->os() << ";" << std::endl;
}

template <class Stream> void CodeGenC<Stream>::visit(const IntConst &op) {
    this->os() << std::to_string(op->val_);
}

template <class Stream> void CodeGenC<Stream>::visit(const FloatConst &op) {
    if (std::isnan(op->val_)) {
        throw InvalidProgram("NaN literal in the program");
    } else if (op->val_ == INFINITY) {
        this->os() << "INFINITY";
    } else if (op->val_ == -INFINITY) {
        this->os() << "-INFINITY";
    } else {
        this->os() << std::hexfloat << op->val_
                   << "f"; // FIXME: Determine the actual type
    }
}

template <class Stream> void CodeGenC<Stream>::visit(const BoolConst &op) {
    this->os() << std::to_string(op->val_);
}

template <class Stream> void CodeGenC<Stream>::visit(const Add &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " + ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Sub &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " - ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Mul &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " * ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const RealDiv &op) {
    if (isFloat(op->lhs_->dtype()) || isFloat(op->rhs_->dtype())) {
        this->os() << "(";
        (*this)(op->lhs_);
        this->os() << " / ";
        (*this)(op->rhs_);
        this->os() << ")";
    } else {
        // TODO: Use double?
        this->os() << "(float(";
        (*this)(op->lhs_);
        this->os() << ") / float(";
        (*this)(op->rhs_);
        this->os() << "))";
    }
}

template <class Stream>
void CodeGenC<Stream>::visit(const RoundTowards0Div &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " / ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const FloorDiv &op) {
    this->os() << "floorDiv<" << this->gen(op->dtype()) << ">(";
    (*this)(op->lhs_);
    this->os() << ", ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const CeilDiv &op) {
    this->os() << "ceilDiv<" << this->gen(op->dtype()) << ">(";
    (*this)(op->lhs_);
    this->os() << ", ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Mod &op) {
    this->os() << "runtime_mod(";
    (*this)(op->lhs_);
    this->os() << ", ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Remainder &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " % ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Min &op) {
    this->os() << "std::min<" << this->gen(op->dtype()) << ">(";
    (*this)(op->lhs_);
    this->os() << ", ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Max &op) {
    this->os() << "std::max<" << this->gen(op->dtype()) << ">(";
    (*this)(op->lhs_);
    this->os() << ", ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const LT &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " < ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const LE &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " <= ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const GT &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " > ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const GE &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " >= ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const EQ &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " == ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const NE &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " != ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const LAnd &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " && ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const LOr &op) {
    this->os() << "(";
    (*this)(op->lhs_);
    this->os() << " || ";
    (*this)(op->rhs_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const LNot &op) {
    this->os() << "!";
    (*this)(op->expr_);
}

template <class Stream> void CodeGenC<Stream>::visit(const Sqrt &op) {
    this->os() << "sqrt(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Exp &op) {
    this->os() << "exp(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Ln &op) {
    this->os() << "log(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Square &op) {
    this->os() << "runtime_square(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Sigmoid &op) {
    this->os() << "runtime_sigmoid(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Sin &op) {
    this->os() << "std::sin(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Cos &op) {
    this->os() << "std::cos(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Tan &op) {
    this->os() << "std::tan(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Tanh &op) {
    this->os() << "std::tanh(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Abs &op) {
    this->os() << "std::abs(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Floor &op) {
    this->os() << "std::floor(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Ceil &op) {
    this->os() << "std::ceil(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const IfExpr &op) {
    this->os() << "(";
    (*this)(op->cond_);
    this->os() << " ? ";
    (*this)(op->thenCase_);
    this->os() << " : ";
    (*this)(op->elseCase_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Cast &op) {
    this->os() << gen(op->destType_) << "(";
    (*this)(op->expr_);
    this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const For &op) {
    if (op->step_->nodeType() == ASTNodeType::IntConst &&
        op->step_.as<IntConstNode>()->val_ == 1) {
        this->makeIndent();
        this->os() << "for (int " << mangle(op->iter_) << " = ";
        (*this)(op->begin_);
        this->os() << "; " << mangle(op->iter_) << " < ";
        (*this)(op->end_);
        this->os() << "; " << mangle(op->iter_) << "++) ";
        this->beginBlock();
        this->markDefIter(op);
        (*this)(op->body_);
        this->markUndefIter(op);
        this->endBlock();
    } else {
        auto iterCnt = mangle(op->iter_ + ".cnt");
        this->makeIndent();
        this->os() << "for (int " << iterCnt << " = 0; " << iterCnt << " < ";
        (*this)(op->len_);
        this->os() << "; " << iterCnt << "++) ";
        this->beginBlock();
        this->makeIndent();
        this->os() << "int " << mangle(op->iter_) << " = ";
        (*this)(op->begin_);
        this->os() << " + " << iterCnt << " * ";
        (*this)(op->step_);
        this->os() << ";" << std::endl;
        this->markDefIter(op);
        (*this)(op->body_);
        this->markUndefIter(op);
        this->endBlock();
    }
}

template <class Stream> void CodeGenC<Stream>::visit(const If &op) {
    this->makeIndent();
    this->os() << "if (";
    (*this)(op->cond_);
    this->os() << ") ";
    this->beginBlock();
    (*this)(op->thenCase_);
    this->endBlock();
    if (op->elseCase_.isValid()) {
        this->makeIndent();
        this->os() << "else ";
        this->beginBlock();
        (*this)(op->elseCase_);
        this->endBlock();
    }
}

template <class Stream> void CodeGenC<Stream>::visit(const Assert &op) {
    this->makeIndent();
    this->os() << "assert(";
    (*this)(op->cond_);
    this->os() << ");" << std::endl;
    (*this)(op->body_);
}

template <class Stream> void CodeGenC<Stream>::visit(const Intrinsic &op) {
    bool parentIsEval =
        op->parent().as<ASTNode>()->nodeType() != ASTNodeType::Eval;
    if (parentIsEval)
        this->os() << "(";
    size_t i = 0, j = 0, n = op->format_.length();
    while (j < n) {
        if (op->format_[j] == '%') {
            if (j + 1 < n && op->format_[j + 1] == '%') {
                this->os() << '%';
                j += 2;
            } else {
                (*this)(op->params_.at(i++));
                j++;
            }
        } else {
            this->os() << op->format_[j];
            j++;
        }
    }
    if (parentIsEval)
        this->os() << ")";
}

template <class Stream> void CodeGenC<Stream>::visit(const Eval &op) {
    this->makeIndent();
    (*this)(op->expr_);
    this->os() << ";" << std::endl;
}

template <class Stream>
std::string CodeGenC<Stream>::gen(const DataType &dtype) {
    switch (dtype.base()) {
    case DataType::Float64:
        return "double";
    case DataType::Float32:
        return "float";
    case DataType::Float16:
        WARNING(
            "float16 arithmetics on CPU is supported via emulation and comes "
            "with a performance cost, which is only for compatibility purpose. "
            "If you intend to do float32 computation on float16 variables, "
            "please convert them explicitly. Please ignore this warning if you "
            "are only allocating buffers and not performing arithmetics.");
        return "half_float::half"; // From 3rd-party/half
    case DataType::Int64:
        return "int64_t";
    case DataType::Int32:
        return "int32_t";
    case DataType::Bool:
        return "bool";
    default:
        throw InvalidProgram(
            FT_MSG << dtype << " is not supported by this codegen backend");
    }
}

} // namespace freetensor

#endif // DETAIL_CODE_GEN_C_H
