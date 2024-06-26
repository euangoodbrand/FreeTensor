''' Global configurations '''

__all__ = [
    'with_mkl', 'with_cuda', 'with_pytorch', 'set_pretty_print', 'pretty_print',
    'set_print_all_id', 'print_all_id', 'set_print_source_location',
    'print_source_location', 'set_fast_math', 'fast_math', 'set_werror',
    'werror', 'set_debug_binary', 'debug_binary', 'set_debug_cuda_with_um',
    'debug_cuda_with_um', 'set_backend_compiler_cxx', 'backend_compiler_cxx',
    'set_backend_compiler_nvcc', 'backend_compiler_nvcc', 'backend_openmp',
    'set_backend_openmp', 'set_default_target', 'default_target',
    'set_default_device', 'default_device', 'set_runtime_dir', 'runtime_dir'
]

from .. import ffi


def _import_func(f):
    ''' Make our doc builder know we have this function '''

    def g(*args, **kvs):
        return f(*args, **kvs)

    g.__name__ = f.__name__
    g.__qualname__ = f.__qualname__
    if hasattr(f, '__annotations__'):
        g.__annotations__ = f.__annotations__
    g.__doc__ = f.__doc__
    return g


with_mkl = _import_func(ffi.with_mkl)

with_cuda = _import_func(ffi.with_cuda)

with_pytorch = _import_func(ffi.with_pytorch)

set_pretty_print = _import_func(ffi.set_pretty_print)
pretty_print = _import_func(ffi.pretty_print)

set_print_all_id = _import_func(ffi.set_print_all_id)
print_all_id = _import_func(ffi.print_all_id)

set_print_source_location = _import_func(ffi.set_print_source_location)
print_source_location = _import_func(ffi.print_source_location)

set_fast_math = _import_func(ffi.set_fast_math)
fast_math = _import_func(ffi.fast_math)

set_werror = _import_func(ffi.set_werror)
werror = _import_func(ffi.werror)

set_debug_binary = _import_func(ffi.set_debug_binary)
debug_binary = _import_func(ffi.debug_binary)

set_debug_cuda_with_um = _import_func(ffi.set_debug_cuda_with_um)
debug_cuda_with_um = _import_func(ffi.debug_cuda_with_um)

set_backend_compiler_cxx = _import_func(ffi.set_backend_compiler_cxx)
backend_compiler_cxx = _import_func(ffi.backend_compiler_cxx)

set_backend_compiler_nvcc = _import_func(ffi.set_backend_compiler_nvcc)
backend_compiler_nvcc = _import_func(ffi.backend_compiler_nvcc)

set_backend_openmp = _import_func(ffi.set_backend_openmp)
backend_openmp = _import_func(ffi.backend_openmp)

set_default_target = _import_func(ffi.set_default_target)
default_target = _import_func(ffi.default_target)

set_default_device = _import_func(ffi.set_default_device)
default_device = _import_func(ffi.default_device)

set_runtime_dir = _import_func(ffi.set_runtime_dir)
runtime_dir = _import_func(ffi.runtime_dir)
