#include <ffi.h>
#include <lower.h>
#include <pass/cpu/lower_parallel_reduction.h>
#include <pass/flatten_stmt_seq.h>
#include <pass/float_simplify.h>
#include <pass/gpu/lower_parallel_reduction.h>
#include <pass/gpu/lower_vector.h>
#include <pass/gpu/make_sync.h>
#include <pass/gpu/multiplex_buffers.h>
#include <pass/gpu/normalize_threads.h>
#include <pass/gpu/simplex_buffers.h>
#include <pass/hoist_var_over_stmt_seq.h>
#include <pass/make_heap_alloc.h>
#include <pass/make_parallel_reduction.h>
#include <pass/make_reduction.h>
#include <pass/merge_and_hoist_if.h>
#include <pass/move_out_first_or_last_iter.h>
#include <pass/prop_one_time_use.h>
#include <pass/remove_cyclic_assign.h>
#include <pass/remove_dead_var.h>
#include <pass/remove_writes.h>
#include <pass/scalar_prop_const.h>
#include <pass/shrink_for.h>
#include <pass/shrink_var.h>
#include <pass/simplify.h>
#include <pass/sink_var.h>
#include <pass/tensor_prop_const.h>
#include <pass/use_builtin_div.h>
#include <pass/z3_simplify.h>

namespace freetensor {

using namespace pybind11::literals;

void init_ffi_pass(py::module_ &m) {
    m.def("simplify", static_cast<Func (*)(const Func &)>(&simplify), "func"_a);
    m.def("simplify", static_cast<Stmt (*)(const Stmt &)>(&simplify), "stmt"_a);

    m.def("z3_simplify", static_cast<Func (*)(const Func &)>(&z3Simplify),
          "func"_a);
    m.def("z3_simplify", static_cast<Stmt (*)(const Stmt &)>(&z3Simplify),
          "stmt"_a);

    m.def("pb_simplify", static_cast<Func (*)(const Func &)>(&pbSimplify),
          "func"_a);
    m.def("pb_simplify", static_cast<Stmt (*)(const Stmt &)>(&pbSimplify),
          "stmt"_a);

    m.def("float_simplify", static_cast<Func (*)(const Func &)>(&floatSimplify),
          "func"_a);
    m.def("float_simplify", static_cast<Stmt (*)(const Stmt &)>(&floatSimplify),
          "stmt"_a);

    m.def("flatten_stmt_seq",
          static_cast<Func (*)(const Func &)>(&flattenStmtSeq), "func"_a);
    m.def("flatten_stmt_seq",
          static_cast<Stmt (*)(const Stmt &)>(&flattenStmtSeq), "stmt"_a);

    m.def("move_out_first_or_last_iter",
          static_cast<Func (*)(const Func &)>(&moveOutFirstOrLastIter),
          "func"_a);
    m.def("move_out_first_or_last_iter",
          static_cast<Stmt (*)(const Stmt &)>(&moveOutFirstOrLastIter),
          "stmt"_a);

    m.def("scalar_prop_const",
          static_cast<Func (*)(const Func &)>(&scalarPropConst), "func"_a);
    m.def("scalar_prop_const",
          static_cast<Stmt (*)(const Stmt &)>(&scalarPropConst), "stmt"_a);

    m.def("sink_var",
          static_cast<Func (*)(
              const Func &, const std::optional<std::unordered_set<ID>> &,
              const std::function<bool(const Stmt &)> &)>(&sinkVar),
          "func"_a, "to_sink"_a = std::nullopt, "scope_filter"_a = nullptr);
    m.def("sink_var",
          static_cast<Stmt (*)(
              const Stmt &, const std::optional<std::unordered_set<ID>> &,
              const std::function<bool(const Stmt &)> &)>(&sinkVar),
          "stmt"_a, "to_sink"_a = std::nullopt, "scope_filter"_a = nullptr);

    m.def("shrink_var", static_cast<Func (*)(const Func &)>(&shrinkVar),
          "func"_a);
    m.def("shrink_var", static_cast<Stmt (*)(const Stmt &)>(&shrinkVar),
          "stmt"_a);

    m.def("shrink_for",
          static_cast<Func (*)(const Func &, const ID &, const bool &,
                               const bool &)>(&shrinkFor),
          "func"_a, py::arg_v("sub_ast", ID(), "ID()"), "do_simplify"_a = true,
          "unordered"_a = false);
    m.def(
        "shrink_for",
        static_cast<Stmt (*)(const Stmt &, const ID &, bool, bool)>(&shrinkFor),
        "stmt"_a, py::arg_v("sub_ast", ID(), "ID()"), "do_simplify"_a = true,
        "unordered"_a = false);

    m.def("merge_and_hoist_if",
          static_cast<Func (*)(const Func &)>(&mergeAndHoistIf), "func"_a);
    m.def("merge_and_hoist_if",
          static_cast<Stmt (*)(const Stmt &)>(&mergeAndHoistIf), "stmt"_a);

    m.def("make_reduction", static_cast<Func (*)(const Func &)>(&makeReduction),
          "func"_a);
    m.def("make_reduction", static_cast<Stmt (*)(const Stmt &)>(&makeReduction),
          "stmt"_a);

    m.def("make_parallel_reduction",
          static_cast<Func (*)(const Func &, const Ref<Target> &)>(
              &makeParallelReduction),
          "func"_a, "target"_a);
    m.def("make_parallel_reduction",
          static_cast<Stmt (*)(const Stmt &, const Ref<Target> &)>(
              &makeParallelReduction),
          "stmt"_a, "target"_a);

    m.def("tensor_prop_const",
          static_cast<Func (*)(const Func &, const ID &, const ID &)>(
              &tensorPropConst),
          "func"_a, py::arg_v("both_in_sub_ast", ID(), "ID()"),
          py::arg_v("either_in_sub_ast", ID(), "ID()"));
    m.def("tensor_prop_const",
          static_cast<Stmt (*)(const Stmt &, const ID &, const ID &)>(
              &tensorPropConst),
          "stmt"_a, py::arg_v("both_in_sub_ast", ID(), "ID()"),
          py::arg_v("either_in_sub_ast", ID(), "ID()"));

    m.def("prop_one_time_use",
          static_cast<Func (*)(const Func &, const ID &)>(&propOneTimeUse),
          "func"_a, py::arg_v("sub_ast", ID(), "ID()"));
    m.def("prop_one_time_use",
          static_cast<Stmt (*)(const Stmt &, const ID &)>(&propOneTimeUse),
          "stmt"_a, py::arg_v("sub_ast", ID(), "ID()"));

    m.def("remove_writes",
          static_cast<Func (*)(const Func &, const ID &)>(&removeWrites),
          "func"_a, py::arg_v("single_def_id", ID(), "ID()"));
    m.def("remove_writes",
          static_cast<Stmt (*)(const Stmt &, const ID &)>(&removeWrites),
          "stmt"_a, py::arg_v("single_def_id", ID(), "ID()"));

    m.def("remove_cyclic_assign",
          static_cast<Func (*)(const Func &)>(&removeCyclicAssign), "func"_a);
    m.def("remove_cyclic_assign",
          static_cast<Stmt (*)(const Stmt &)>(&removeCyclicAssign), "stmt"_a);

    m.def("remove_dead_var",
          static_cast<Func (*)(const Func &)>(&removeDeadVar), "func"_a);
    m.def("remove_dead_var",
          static_cast<Stmt (*)(const Stmt &)>(&removeDeadVar), "stmt"_a);

    m.def("make_heap_alloc",
          static_cast<Func (*)(const Func &)>(&makeHeapAlloc), "func"_a);
    m.def("make_heap_alloc",
          static_cast<Stmt (*)(const Stmt &)>(&makeHeapAlloc), "stmt"_a);

    m.def("use_builtin_div",
          static_cast<Func (*)(const Func &)>(&useBuiltinDiv), "func"_a);
    m.def("use_builtin_div",
          static_cast<Stmt (*)(const Stmt &)>(&useBuiltinDiv), "stmt"_a);

    m.def("hoist_var_over_stmt_seq",
          static_cast<Func (*)(const Func &,
                               const std::optional<std::vector<ID>> &)>(
              &hoistVarOverStmtSeq),
          "func"_a, "together_ids"_a = std::nullopt);
    m.def("hoist_var_over_stmt_seq",
          static_cast<Stmt (*)(const Stmt &,
                               const std::optional<std::vector<ID>> &)>(
              &hoistVarOverStmtSeq),
          "stmt"_a, "together_ids"_a = std::nullopt);

    // CPU
    m.def("cpu_lower_parallel_reduction",
          static_cast<Func (*)(const Func &)>(&cpu::lowerParallelReduction));
    m.def("cpu_lower_parallel_reduction",
          static_cast<Stmt (*)(const Stmt &)>(&cpu::lowerParallelReduction));

    // GPU
#ifdef FT_WITH_CUDA
#define GPU_ONLY(name, ...) name, __VA_ARGS__
#else
#define GPU_ONLY(name, ...)                                                    \
    name, [](const py::args &, const py::kwargs &) {                           \
        ERROR(FT_MSG << name                                                   \
                     << " is unavailable because FT_WITH_CUDA is disabled "    \
                        "when building FreeTensor");                           \
    }
#endif

    m.def(GPU_ONLY(
        "gpu_lower_parallel_reduction",
        static_cast<Func (*)(const Func &)>(&gpu::lowerParallelReduction)));
    m.def(GPU_ONLY(
        "gpu_lower_parallel_reduction",
        static_cast<Stmt (*)(const Stmt &)>(&gpu::lowerParallelReduction)));

    m.def(GPU_ONLY("gpu_normalize_threads",
                   static_cast<Func (*)(const Func &)>(&gpu::normalizeThreads),
                   "func"_a));
    m.def(GPU_ONLY("gpu_normalize_threads",
                   static_cast<Stmt (*)(const Stmt &)>(&gpu::normalizeThreads),
                   "stmt"_a));

    m.def(GPU_ONLY(
        "gpu_normalize_var_in_kernel",
        static_cast<Func (*)(const Func &)>(&gpu::normalizeVarInKernel),
        "func"_a));
    m.def(GPU_ONLY(
        "gpu_normalize_var_in_kernel",
        static_cast<Stmt (*)(const Stmt &)>(&gpu::normalizeVarInKernel),
        "stmt"_a));

    m.def(GPU_ONLY("gpu_make_sync",
                   static_cast<Func (*)(const Func &, const Ref<GPUTarget> &)>(
                       &gpu::makeSync),
                   "func"_a, "target"_a));
    m.def(GPU_ONLY("gpu_make_sync",
                   static_cast<Stmt (*)(const Stmt &, const Ref<GPUTarget> &)>(
                       &gpu::makeSync),
                   "stmt"_a, "target"_a));

    m.def(GPU_ONLY(
        "gpu_multiplex_buffers",
        static_cast<Func (*)(const Func &, const Ref<GPUTarget> &, const ID &)>(
            &gpu::multiplexBuffers),
        "func"_a, "target"_a, "def_id"_a = std::nullopt));
    m.def(GPU_ONLY(
        "gpu_multiplex_buffers",
        static_cast<Stmt (*)(const Stmt &, const Ref<GPUTarget> &, const ID &)>(
            &gpu::multiplexBuffers),
        "stmt"_a, "target"_a, "def_id"_a = std::nullopt));

    m.def(GPU_ONLY(
        "gpu_simplex_buffers",
        static_cast<Func (*)(const Func &, const ID &)>(&gpu::simplexBuffers),
        "func"_a, "def_id"_a = std::nullopt));
    m.def(GPU_ONLY(
        "gpu_simplex_buffers",
        static_cast<Stmt (*)(const Stmt &, const ID &)>(&gpu::simplexBuffers),
        "stmt"_a, "def_id"_a = std::nullopt));

    m.def(GPU_ONLY("gpu_lower_vector",
                   static_cast<Func (*)(const Func &)>(&gpu::lowerVector),
                   "func"_a));
    m.def(GPU_ONLY("gpu_lower_vector",
                   static_cast<Stmt (*)(const Stmt &)>(&gpu::lowerVector),
                   "stmt"_a));

#undef GPU_ONLY

    m.def("lower",
          static_cast<Func (*)(const Func &, const Ref<Target> &,
                               const std::unordered_set<std::string> &, int)>(
              &lower),
          "func"_a, "target"_a = nullptr,
          "skip_passes"_a = std::unordered_set<std::string>{}, "verbose"_a = 0);
    m.def("lower",
          static_cast<Stmt (*)(const Stmt &, const Ref<Target> &,
                               const std::unordered_set<std::string> &, int)>(
              &lower),
          "stmt"_a, "target"_a = nullptr,
          "skip_passes"_a = std::unordered_set<std::string>{}, "verbose"_a = 0);
}

} // namespace freetensor
