#include <expr.h>
#include <ffi.h>
#include <frontend/frontend_var.h>

namespace freetensor {

using namespace pybind11::literals;

void init_ffi_ast_expr(py::module_ &m) {
    auto pyExpr = m.attr("Expr").cast<py::class_<ExprNode, Expr>>();

    py::class_<VarNode, Var>(m, "Var", pyExpr)
        .def_readonly("name", &VarNode::name_);
    py::class_<LoadNode, Load>(m, "Load", pyExpr)
        .def_readonly("var", &LoadNode::var_)
        .def_property_readonly(
            "indices",
            [](const Load &op) -> std::vector<Expr> { return op->indices_; });
    py::class_<IntConstNode, IntConst>(m, "IntConst", pyExpr)
        .def_readonly("val", &IntConstNode::val_);
    py::class_<FloatConstNode, FloatConst>(m, "FloatConst", pyExpr)
        .def_readonly("val", &FloatConstNode::val_);
    py::class_<BoolConstNode, BoolConst>(m, "BoolConst", pyExpr)
        .def_readonly("val", &BoolConstNode::val_);
    py::class_<AddNode, Add>(m, "Add", pyExpr)
        .def_property_readonly("lhs",
                               [](const Add &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const Add &op) -> Expr { return op->rhs_; });
    py::class_<SubNode, Sub>(m, "Sub", pyExpr)
        .def_property_readonly("lhs",
                               [](const Sub &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const Sub &op) -> Expr { return op->rhs_; });
    py::class_<MulNode, Mul>(m, "Mul", pyExpr)
        .def_property_readonly("lhs",
                               [](const Mul &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const Mul &op) -> Expr { return op->rhs_; });
    py::class_<RealDivNode, RealDiv>(m, "RealDiv", pyExpr)
        .def_property_readonly(
            "lhs", [](const RealDiv &op) -> Expr { return op->lhs_; })
        .def_property_readonly(
            "rhs", [](const RealDiv &op) -> Expr { return op->rhs_; });
    py::class_<FloorDivNode, FloorDiv>(m, "FloorDiv", pyExpr)
        .def_property_readonly(
            "lhs", [](const FloorDiv &op) -> Expr { return op->lhs_; })
        .def_property_readonly(
            "rhs", [](const FloorDiv &op) -> Expr { return op->rhs_; });
    py::class_<CeilDivNode, CeilDiv>(m, "CeilDiv", pyExpr)
        .def_property_readonly(
            "lhs", [](const CeilDiv &op) -> Expr { return op->lhs_; })
        .def_property_readonly(
            "rhs", [](const CeilDiv &op) -> Expr { return op->rhs_; });
    py::class_<RoundTowards0DivNode, RoundTowards0Div>(m, "RoundTowards0Div",
                                                       pyExpr)
        .def_property_readonly(
            "lhs", [](const RoundTowards0Div &op) -> Expr { return op->lhs_; })
        .def_property_readonly(
            "rhs", [](const RoundTowards0Div &op) -> Expr { return op->rhs_; });
    py::class_<ModNode, Mod>(m, "Mod", pyExpr)
        .def_property_readonly("lhs",
                               [](const Mod &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const Mod &op) -> Expr { return op->rhs_; });
    py::class_<RemainderNode, Remainder>(m, "Remainder", pyExpr)
        .def_property_readonly(
            "lhs", [](const Remainder &op) -> Expr { return op->lhs_; })
        .def_property_readonly(
            "rhs", [](const Remainder &op) -> Expr { return op->rhs_; });
    py::class_<MinNode, Min>(m, "Min", pyExpr)
        .def_property_readonly("lhs",
                               [](const Min &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const Min &op) -> Expr { return op->rhs_; });
    py::class_<MaxNode, Max>(m, "Max", pyExpr)
        .def_property_readonly("lhs",
                               [](const Max &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const Max &op) -> Expr { return op->rhs_; });
    py::class_<LTNode, LT>(m, "LT", pyExpr)
        .def_property_readonly("lhs",
                               [](const LT &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const LT &op) -> Expr { return op->rhs_; });
    py::class_<LENode, LE>(m, "LE", pyExpr)
        .def_property_readonly("lhs",
                               [](const LE &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const LE &op) -> Expr { return op->rhs_; });
    py::class_<GTNode, GT>(m, "GT", pyExpr)
        .def_property_readonly("lhs",
                               [](const GT &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const GT &op) -> Expr { return op->rhs_; });
    py::class_<GENode, GE>(m, "GE", pyExpr)
        .def_property_readonly("lhs",
                               [](const GE &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const GE &op) -> Expr { return op->rhs_; });
    py::class_<EQNode, EQ>(m, "EQ", pyExpr)
        .def_property_readonly("lhs",
                               [](const EQ &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const EQ &op) -> Expr { return op->rhs_; });
    py::class_<NENode, NE>(m, "NE", pyExpr)
        .def_property_readonly("lhs",
                               [](const NE &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const NE &op) -> Expr { return op->rhs_; });
    py::class_<LAndNode, LAnd>(m, "LAnd", pyExpr)
        .def_property_readonly("lhs",
                               [](const LAnd &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const LAnd &op) -> Expr { return op->rhs_; });
    py::class_<LOrNode, LOr>(m, "LOr", pyExpr)
        .def_property_readonly("lhs",
                               [](const LOr &op) -> Expr { return op->lhs_; })
        .def_property_readonly("rhs",
                               [](const LOr &op) -> Expr { return op->rhs_; });
    py::class_<LNotNode, LNot>(m, "LNot", pyExpr)
        .def_property_readonly(
            "expr", [](const LNot &op) -> Expr { return op->expr_; });
    py::class_<SqrtNode, Sqrt>(m, "Sqrt", pyExpr)
        .def_property_readonly(
            "expr", [](const Sqrt &op) -> Expr { return op->expr_; });
    py::class_<ExpNode, Exp>(m, "Exp", pyExpr)
        .def_property_readonly("expr",
                               [](const Exp &op) -> Expr { return op->expr_; });
    py::class_<LnNode, Ln>(m, "Ln", pyExpr)
        .def_property_readonly("expr",
                               [](const Ln &op) -> Expr { return op->expr_; });
    py::class_<SquareNode, Square>(m, "Square", pyExpr)
        .def_property_readonly(
            "expr", [](const Square &op) -> Expr { return op->expr_; });
    py::class_<SigmoidNode, Sigmoid>(m, "Sigmoid", pyExpr)
        .def_property_readonly(
            "expr", [](const Sigmoid &op) -> Expr { return op->expr_; });
    py::class_<SinNode, Sin>(m, "Sin", pyExpr)
        .def_property_readonly("expr",
                               [](const Sin &op) -> Expr { return op->expr_; });
    py::class_<CosNode, Cos>(m, "Cos", pyExpr)
        .def_property_readonly("expr",
                               [](const Cos &op) -> Expr { return op->expr_; });
    py::class_<TanNode, Tan>(m, "Tan", pyExpr)
        .def_property_readonly("expr",
                               [](const Tan &op) -> Expr { return op->expr_; });
    py::class_<TanhNode, Tanh>(m, "Tanh", pyExpr)
        .def_property_readonly(
            "expr", [](const Tanh &op) -> Expr { return op->expr_; });
    py::class_<AbsNode, Abs>(m, "Abs", pyExpr)
        .def_property_readonly("expr",
                               [](const Abs &op) -> Expr { return op->expr_; });
    py::class_<FloorNode, Floor>(m, "Floor", pyExpr)
        .def_property_readonly(
            "expr", [](const Floor &op) -> Expr { return op->expr_; });
    py::class_<CeilNode, Ceil>(m, "Ceil", pyExpr)
        .def_property_readonly(
            "expr", [](const Ceil &op) -> Expr { return op->expr_; });
    py::class_<IfExprNode, IfExpr>(m, "IfExpr", pyExpr)
        .def_property_readonly(
            "cond", [](const IfExpr &op) -> Expr { return op->cond_; })
        .def_property_readonly(
            "then_case", [](const IfExpr &op) -> Expr { return op->thenCase_; })
        .def_property_readonly("else_case", [](const IfExpr &op) -> Expr {
            return op->elseCase_;
        });
    py::class_<CastNode, Cast>(m, "Cast", pyExpr)
        .def_property_readonly("expr",
                               [](const Cast &op) -> Expr { return op->expr_; })
        .def_property_readonly("dest_type", [](const Cast &op) -> DataType {
            return op->destType_;
        });
    py::class_<IntrinsicNode, Intrinsic> pyIntrinsic(m, "Intrinsic", pyExpr);
    py::class_<AnyExprNode, AnyExpr> pyAnyExpr(m, "AnyExpr", pyExpr);
    py::class_<LoadAtVersionNode, LoadAtVersion>(m, "LoadAtVersion", pyExpr)
        .def_readonly("tape_name", &LoadAtVersionNode::tapeName_)
        .def_property_readonly(
            "indices", [](const LoadAtVersion &op) -> std::vector<Expr> {
                return op->indices_;
            });

    // NOTE: ORDER of the constructor matters!
    pyExpr.def(py::init([](const Expr &expr) { return deepCopy(expr); }))
        .def(py::init([](bool val) { return makeBoolConst(val); }))
        .def(py::init([](int64_t val) { return makeIntConst(val); }))
        .def(py::init([](float val) { return makeFloatConst(val); }))
        .def(py::init([](const FrontendVar &var) { return var.asLoad(); }))
        .def_property_readonly(
            "dtype", [](const Expr &op) -> DataType { return op->dtype(); });
    py::implicitly_convertible<int, ExprNode>();
    py::implicitly_convertible<float, ExprNode>();
    py::implicitly_convertible<bool, ExprNode>();
    py::implicitly_convertible<FrontendVar, ExprNode>();

    // makers
    m.def("makeAnyExpr", []() { return makeAnyExpr(); });
    m.def(
        "makeVar", [](const std::string &_1) { return makeVar(_1); }, "name"_a);
    m.def(
        "makeIntConst", [](int64_t _1) { return makeIntConst(_1); }, "val"_a);
    m.def(
        "makeFloatConst", [](double _1) { return makeFloatConst(_1); },
        "val"_a);
    m.def(
        "makeBoolConst", [](bool _1) { return makeBoolConst(_1); }, "val"_a);
    m.def(
        "makeLoad",
        [](const std::string &_1, const std::vector<Expr> &_2,
           const DataType _3) { return makeLoad(_1, _2, _3); },
        "var"_a, "indices"_a, "load_type"_a);
    m.def(
        "makeMod",
        [](const Expr &_1, const Expr &_2) { return makeMod(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeRemainder",
        [](const Expr &_1, const Expr &_2) { return makeRemainder(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeMin",
        [](const Expr &_1, const Expr &_2) { return makeMin(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeMax",
        [](const Expr &_1, const Expr &_2) { return makeMax(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeLAnd",
        [](const Expr &_1, const Expr &_2) { return makeLAnd(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeLOr",
        [](const Expr &_1, const Expr &_2) { return makeLOr(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeLNot", [](const Expr &_1) { return makeLNot(_1); }, "expr"_a);
    m.def(
        "makeSqrt", [](const Expr &_1) { return makeSqrt(_1); }, "expr"_a);
    m.def(
        "makeExp", [](const Expr &_1) { return makeExp(_1); }, "expr"_a);
    m.def(
        "makeLn", [](const Expr &_1) { return makeLn(_1); }, "expr"_a);
    m.def(
        "makeSquare", [](const Expr &_1) { return makeSquare(_1); }, "expr"_a);
    m.def(
        "makeSigmoid", [](const Expr &_1) { return makeSigmoid(_1); },
        "expr"_a);
    m.def(
        "makeSin", [](const Expr &_1) { return makeSin(_1); }, "expr"_a);
    m.def(
        "makeCos", [](const Expr &_1) { return makeCos(_1); }, "expr"_a);
    m.def(
        "makeTan", [](const Expr &_1) { return makeTan(_1); }, "expr"_a);
    m.def(
        "makeTanh", [](const Expr &_1) { return makeTanh(_1); }, "expr"_a);
    m.def(
        "makeAbs", [](const Expr &_1) { return makeAbs(_1); }, "expr"_a);
    m.def(
        "makeFloor", [](const Expr &_1) { return makeFloor(_1); }, "expr"_a);
    m.def(
        "makeCeil", [](const Expr &_1) { return makeCeil(_1); }, "expr"_a);
    m.def(
        "makeUnbound", [](const Expr &_1) { return makeUnbound(_1); },
        "expr"_a);
    m.def(
        "makeIfExpr",
        [](const Expr &_1, const Expr &_2, const Expr &_3) {
            return makeIfExpr(_1, _2, _3);
        },
        "cond"_a, "thenCase"_a, "elseCase"_a);
    m.def(
        "makeCast",
        [](const Expr &_1, const DataType &_2) { return makeCast(_1, _2); },
        "expr"_a, "dtype"_a);
    m.def(
        "makeAdd",
        [](const Expr &_1, const Expr &_2) { return makeAdd(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeSub",
        [](const Expr &_1, const Expr &_2) { return makeSub(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeMul",
        [](const Expr &_1, const Expr &_2) { return makeMul(_1, _2); }, "lhs"_a,
        "rhs"_a);
    m.def(
        "makeRealDiv",
        [](const Expr &_1, const Expr &_2) { return makeRealDiv(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeFloorDiv",
        [](const Expr &_1, const Expr &_2) { return makeFloorDiv(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeCeilDiv",
        [](const Expr &_1, const Expr &_2) { return makeCeilDiv(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeRoundTowards0Div",
        [](const Expr &_1, const Expr &_2) {
            return makeRoundTowards0Div(_1, _2);
        },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeLT", [](const Expr &_1, const Expr &_2) { return makeLT(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeLE", [](const Expr &_1, const Expr &_2) { return makeLE(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeGT", [](const Expr &_1, const Expr &_2) { return makeGT(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeGE", [](const Expr &_1, const Expr &_2) { return makeGE(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeEQ", [](const Expr &_1, const Expr &_2) { return makeEQ(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeNE", [](const Expr &_1, const Expr &_2) { return makeNE(_1, _2); },
        "lhs"_a, "rhs"_a);
    m.def(
        "makeIntrinsic",
        [](const std::string &_1, const std::vector<Expr> &_2,
           const DataType &_3,
           bool _4) { return makeIntrinsic(_1, _2, _3, _4); },
        "fmt"_a, "params"_a, "retType"_a = DataType{DataType::Void},
        "hasSideEffect"_a = false);
    m.def(
        "makeLoadAtVersion",
        [](const std::string &_1, const std::vector<Expr> &_2,
           const DataType &_3) { return makeLoadAtVersion(_1, _2, _3); },
        "tape_name"_a, "indices"_a, "load_type"_a);
}

} // namespace freetensor
