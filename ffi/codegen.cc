#include <codegen/code_gen.h>
#include <codegen/code_gen_cpu.h>
#include <codegen/code_gen_cuda.h>
#include <codegen/native_code.h>
#include <ffi.h>

namespace freetensor {

using namespace pybind11::literals;

void init_ffi_codegen(py::module_ &m) {
    py::class_<NativeCodeParam>(m, "NativeCodeParam")
        .def(py::init([](const std::string &name, const DataType &dtype,
                         const AccessType &atype, const MemType &mtype) {
            return NativeCodeParam{name, dtype, atype, mtype, nullptr, false};
        }))
        .def_readonly("name", &NativeCodeParam::name_)
        .def_readonly("dtype", &NativeCodeParam::dtype_)
        .def_readonly("atype", &NativeCodeParam::atype_)
        .def_readonly("mtype", &NativeCodeParam::mtype_)
        .def_readonly("update_closure", &NativeCodeParam::updateClosure_)
        .def_property_readonly("is_in_closure", &NativeCodeParam::isInClosure)
        .def("__str__",
             static_cast<std::string (*)(const NativeCodeParam &)>(&toString));

    py::class_<NativeCodeRet>(m, "NativCodeRet")
        .def(py::init([](const std::string &name, const DataType &dtype) {
            return NativeCodeRet{name, dtype, nullptr, false};
        }))
        .def_readonly("name", &NativeCodeRet::name_)
        .def_readonly("dtype", &NativeCodeRet::dtype_)
        .def_readonly("return_closure", &NativeCodeRet::returnClosure_)
        .def_property_readonly("is_in_closure", &NativeCodeRet::isInClosure)
        .def("__str__",
             static_cast<std::string (*)(const NativeCodeRet &)>(&toString));

    py::class_<StaticInfo>(m, "StaticInfo");

    py::class_<NativeCode>(m, "NativeCode")
        .def(py::init<const std::string &, const std::vector<NativeCodeParam> &,
                      const std::vector<NativeCodeRet> &, const std::string &,
                      const std::string &, const Ref<Target> &,
                      const StaticInfo &>(),
             "name"_a, "params"_a, "returns"_a, "code"_a, "entry"_a, "target"_a,
             "static_info"_a)
        .def(py::init(&NativeCode::fromFunc), "func"_a, "code"_a, "entry"_a,
             "target"_a, "static_info"_a)
        .def_property_readonly("name", &NativeCode::name)
        .def_property_readonly("params", &NativeCode::params)
        .def_property_readonly("returns", &NativeCode::returns)
        .def_property_readonly("code", &NativeCode::code)
        .def_property_readonly("entry", &NativeCode::entry)
        .def_property_readonly("target", &NativeCode::target)
        .def_property_readonly("static_info", &NativeCode::staticInfo);

    m.def("code_gen", &codeGen, "func"_a, "target"_a);
    m.def("code_gen_cpu", &codeGenCPU, "func"_a, "target"_a);
#ifdef FT_WITH_CUDA
    m.def("code_gen_cuda", &codeGenCUDA, "func"_a, "target"_a);
#endif // FT_WITH_CUDA
}

} // namespace freetensor
