'''
Facility to build AST expressions

Classes and functions in this module are not only used internally for constructing AST nodes,
and also exposed to users via multi-stage programming
'''

__all__ = [
    'VarRef', 'VarRefFromVarDef', 'VarVersionRef', 'add', 'sub', 'mul',
    'truediv', 'floordiv', 'ceildiv', 'round_towards_0_div', 'mod', 'remainder',
    'min', 'max', 'l_and', 'l_or', 'lt', 'le', 'gt', 'ge', 'eq', 'ne', 'l_not',
    'neg', 'abs', 'sqrt', 'exp', 'ln', 'square', 'sigmoid', 'sin', 'cos', 'tan',
    'tanh', 'floor', 'ceil', 'unbound', 'if_then_else', 'cast', 'intrinsic',
    'any', 'load_at_version', 'ndim', 'shape', 'dtype', 'mtype'
]

import collections
import builtins
import math
from typing import Sequence, Union
from dataclasses import dataclass

from .. import ffi

from .context import ctx_stack
from .meta import is_writable


class AlreadyMadeReduceTo:
    """
    A single-value type that marks a ReduceTo node is already made, and there is no need to
    make another Store node

    In standard Python data model, functions like __iadd__ returns the modified self, and
    __setitem__ does a self-assignment. We do the augmenting assignment directly in __iadd__
    and return AlreadyMadeReduceTo, so we do not have to Store it again
    """
    pass


class VarRef(ffi.FrontendVar):
    '''
    Variable of FreeTensor

    All variables in FreeTensor DSL (declared via `Var`, created by `empty` or `var`,
    returned by `libop`, etc.), and their slices, are `VarRef` objects. Operations
    on `VarRef` objects generates AST nodes
    '''

    def __init__(self,
                 name: str,
                 full_shape: Sequence,
                 dtype: ffi.DataType,
                 mtype: ffi.MemType,
                 indices: Sequence = [],
                 is_load_at_version: bool = False):
        super(VarRef, self).__init__(name, full_shape, dtype, mtype, indices,
                                     is_load_at_version)

        from .stmt import find_borrowed_vardefs
        self.borrowed_vardefs = find_borrowed_vardefs(indices)
        for item in self.borrowed_vardefs:
            item.lend_out()

    # If `__iter__` is just missing, Python will try to use `__getitem__` in a loop
    # (https://docs.python.org/3/library/functions.html#iter), which is incorrect in our case.
    # So we set it to None to avoiding the fallback
    # (https://docs.python.org/3/reference/datamodel.html#special-method-names).
    __iter__ = None

    def __del__(self):
        for item in self.borrowed_vardefs:
            item.reclaim()

    def __getitem__(self, key):
        return self.__class__(self.name, self.full_shape, self.dtype,
                              self.mtype,
                              self.chain_indices(self._parse_key(key)))

    def __setitem__(self, key, value):
        var = self.__class__(self.name, self.full_shape, self.dtype, self.mtype,
                             self.chain_indices(self._parse_key(key)))
        if var.ndim > 0:
            if value is AlreadyMadeReduceTo:
                return
            from .. import libop
            libop.assign(var, value)
            return
        if value is AlreadyMadeReduceTo:
            return
        top = ctx_stack.top()
        top.append_stmt(var.as_store(top.get_metadata(), value))

    def as_store(self, metadata, value):
        self._check_write_permission()
        if (not isinstance(value, ffi.AnyExpr) and
                ffi.up_cast(dtype(value), self.dtype).base != self.dtype.base):
            # Add explicit cast node, to avoid confusion after propagation
            value = cast(value, self.dtype)
        return super(VarRef, self).as_store(metadata, value)

    def as_reduce_to(self, reduce_op, metadata, value, atomic=False):
        self._check_write_permission()
        if (not isinstance(value, ffi.AnyExpr) and
                ffi.up_cast(dtype(value), self.dtype).base != self.dtype.base):
            # Add explicit cast node, to avoid confusion after propagation
            value = cast(value, self.dtype)
        return super(VarRef, self).as_reduce_to(reduce_op, metadata, value,
                                                atomic)

    def select(self, idx: Union[int, ffi.Expr, slice], dim: int):
        ''' Alias for `self[..., idx, ...]`, where `idx` is at the `dim`-th dimension '''
        assert isinstance(dim, int)
        assert dim >= 0 and dim < self.ndim
        indices = [
            slice(None, None) if d != dim else idx for d in range(self.ndim)
        ]
        return self[indices]

    def select_slice(self, begin, end, *, dim: int):
        ''' Alias for `self[..., begin:end, ...]`, where `begin:end` is at the `dim`-th dimension '''
        return self.select(slice(begin, end), dim=dim)

    def shape(self, dim=None):
        '''
        Return lengths of all dimensions or the length of one dimension

        `.shape()` -> list of lengths of all dimensions

        `.shape(dim)` -> length of dimension `dim`, where `dim` can be `int`
        or `Expr`

        All lengths can be `Expr` (if the length is dynamically decided) or
        `int` (if statically decided)
        '''
        intOrExpr = lambda x: x.val if isinstance(x, ffi.IntConst) else x
        if dim is None:
            return [intOrExpr(d) for d in super(VarRef, self).shape()]
        else:
            return intOrExpr(super(VarRef, self).shape(dim))

    def _check_write_permission(self):
        if not is_writable(self.vardef.atype):
            raise ffi.InvalidProgram(
                f"Cannot modify an \"{self.vardef.atype}\" tensor `{self.name}`"
            )
        if self.vardef.borrower_cnt > 0:
            raise ffi.InvalidProgram(
                "Cannot modify tensor `" + self.name +
                "` becuase it has been borrowed in another tensor's shape, "
                "a tensor slice, or a range of a loop")

    def _parse_key(self, key):
        if key is None or key is ...:
            key = ()
        if not isinstance(key, collections.abc.Sequence):
            key = (key,)
        ffiIdx = []
        if len(key) > self.ndim:
            key_str = ', '.join(map(str, key))
            raise ffi.InvalidProgram(
                f"Too many indices for {self.name}, expected no more than {self.ndim}, got [{key_str}]"
            )
        for idx, length in zip(key, self.shape()):
            if isinstance(idx, slice):
                start = idx.start if idx.start is not None else 0
                stop = idx.stop if idx.stop is not None else length
                assert idx.step is None or idx.step == 1
                ffiIdx.append(ffi.FrontendVarIdx(start, stop))
            elif isinstance(idx, VarRef):
                if len(idx.full_shape) == len(idx.indices):
                    ffiIdx.append(ffi.FrontendVarIdx(idx.as_load()))
                else:
                    assert len(
                        key
                    ) == 1, f"Shape of an index of {self.name} should be 1-D, instead of {idx.name}"
                    assert type(
                        idx.full_shape[0]
                    ) is ffi.IntConst, "Dynamic number of dimensions is not supported"
                    ndim = idx.full_shape[0].val
                    ffiIdx += [
                        ffi.FrontendVarIdx(idx[i].as_load())
                        for i in range(ndim)
                    ]
            else:
                ffiIdx.append(ffi.FrontendVarIdx(idx))
        return ffiIdx

    def __iadd__(self, other):
        if self.ndim > 0:
            from .. import libop
            libop.add_to(self, other)
            return AlreadyMadeReduceTo
        top = ctx_stack.top()
        top.append_stmt(
            self.as_reduce_to(ffi.ReduceOp.Add, top.get_metadata(), other))
        return AlreadyMadeReduceTo

    def __isub__(self, other):
        if self.ndim > 0:
            from .. import libop
            libop.sub_to(self, other)
            return AlreadyMadeReduceTo
        top = ctx_stack.top()
        top.append_stmt(
            self.as_reduce_to(ffi.ReduceOp.Add, top.get_metadata(), -other))
        return AlreadyMadeReduceTo

    def __imul__(self, other):
        if self.ndim > 0:
            from .. import libop
            libop.mul_to(self, other)
            return AlreadyMadeReduceTo
        top = ctx_stack.top()
        top.append_stmt(
            self.as_reduce_to(ffi.ReduceOp.Mul, top.get_metadata(), other))
        return AlreadyMadeReduceTo

    def __itruediv__(self, other):
        if self.ndim > 0:
            from .. import libop
            libop.truediv_to(self, other)
            return AlreadyMadeReduceTo
        top = ctx_stack.top()
        top.append_stmt(
            self.as_reduce_to(ffi.ReduceOp.Mul, top.get_metadata(), 1. / other))
        return AlreadyMadeReduceTo

    def __ifloordiv__(self, other):
        if self.ndim > 0:
            from .. import libop
            libop.floordiv_to(self, other)
            return AlreadyMadeReduceTo
        return NotImplemented  # Fallback to x = x // y

    def __imod__(self, other):
        if self.ndim > 0:
            from .. import libop
            libop.mod_to(self, other)
            return AlreadyMadeReduceTo
        return NotImplemented  # Fallback to x = x % y

    def __neg__(self):
        if self.ndim > 0:
            from .. import libop
            return libop.neg(self)
        return 0 - self.as_load()

    def __matmul__(self, other):
        from .. import libop
        return libop.matmul(self, other)

    def __rmatmul__(self, other):
        from .. import libop
        return libop.matmul(other, self)


class VarRefFromVarDef(VarRef):
    '''
    VarRef with extra checks
    '''

    def __init__(self,
                 name: str,
                 vardef,
                 full_shape: Sequence,
                 dtype: ffi.DataType,
                 mtype: ffi.MemType,
                 indices: Sequence = []):
        super(VarRefFromVarDef, self).__init__(name, full_shape, dtype, mtype,
                                               indices)
        self.vardef = vardef

    def __getitem__(self, key):
        return VarRefFromVarDef(self.name, self.vardef, self.full_shape,
                                self.dtype, self.mtype,
                                self.chain_indices(self._parse_key(key)))

    def __setitem__(self, key, value):
        var = VarRefFromVarDef(self.name, self.vardef, self.full_shape,
                               self.dtype, self.mtype,
                               self.chain_indices(self._parse_key(key)))
        if var.ndim > 0:
            if value is AlreadyMadeReduceTo:
                return
            from .. import libop
            libop.assign(var, value)
            return
        if value is AlreadyMadeReduceTo:
            return
        top = ctx_stack.top()
        top.append_stmt(var.as_store(top.get_metadata(), value))


class VarVersionRef(VarRef):
    '''
    Special VarRef used for custom gradient, generated from `mark_version`
    '''

    def __init__(self,
                 name: str,
                 full_shape: Sequence,
                 dtype: ffi.DataType,
                 mtype: ffi.MemType,
                 indices: Sequence = []):
        for dim in full_shape:
            if not isinstance(dim, int) and not isinstance(dim, ffi.IntConst):
                raise ffi.InvalidAutoGrad(
                    "`mark_version` on dynamic-shaped variables is not supported"
                    " yet")
        super(VarVersionRef, self).__init__(name, full_shape, dtype, mtype,
                                            indices, True)


def _is_tensor(x):
    return isinstance(x, VarRef) and x.ndim > 0


def _is_runtime_scalar(x):
    return isinstance(x, VarRef) and x.ndim == 0 or isinstance(x, ffi.Expr)


######################################
# Binary Operators


def add(lhs, rhs):
    '''
    `lhs + rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.add

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The sum
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.add(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeAdd(lhs, rhs)
    return lhs + rhs


def sub(lhs, rhs):
    '''
    `lhs - rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.sub

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The difference
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.sub(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeSub(lhs, rhs)
    return lhs - rhs


def mul(lhs, rhs):
    '''
    `lhs * rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.mul

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The product
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.mul(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeMul(lhs, rhs)
    return lhs * rhs


def truediv(lhs, rhs):
    '''
    Floating point division of `lhs` dividing by `rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.truediv

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The quotient
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.truediv(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeRealDiv(lhs, rhs)
    return lhs / rhs


def floordiv(lhs, rhs):
    '''
    Floored integer division of `lhs` dividing by `rhs`

    The result rounds towards negative infinity (following Python convention, instead of C)
    This function is recommended over `round_towards_0_div`, as it enjoys more optimizations

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.floordiv

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The quotient
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.floordiv(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeFloorDiv(lhs, rhs)
    return lhs // rhs


def ceildiv(lhs, rhs):
    '''
    Ceiling integer division of `lhs` dividing by `rhs`

    The result rounds towards positive infinity

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.ceildiv

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The quotient
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.ceildiv(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeCeilDiv(lhs, rhs)
    if type(lhs) is int and type(rhs) is int:
        return lhs // rhs + (lhs % rhs > 0)
    raise TypeError("ceildiv is only supported for integer operands")


def round_towards_0_div(lhs, rhs):
    '''
    C-style integer division of `lhs` dividing by `rhs`

    The result rounds towards 0 (following C convention, instead of Python)
    End users are encouraged to use `lhs // rhs` instead, which follows Python convetion,
    and enjoys better optimization in FreeTensor

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.round_towards_0_div

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The quotient
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.round_towards_0_div(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeRoundTowards0Div(lhs, rhs)
    if type(lhs) is int and type(rhs) is int:
        return a // b if (a < 0) == (b < 0) else -(-a // b)
    raise TypeError(
        "round_towards_0_div is only supported for integer operands")


def mod(lhs, rhs):
    '''
    `lhs` modulus `rhs`

    This function follows floored division (following Python convention, instead of C).
    See https://en.wikipedia.org/wiki/Modulo for details.
    This function is recommended over `remainder`, as it enjoys more optimizations

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.mod

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The modulo
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.mod(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeMod(lhs, rhs)
    return lhs % rhs


def remainder(lhs, rhs):
    '''
    Remainder of `lhs` dividing `rhs`

    This function follows truncated division (following C convention, instead of Python).
    See https://en.wikipedia.org/wiki/Modulo for details.
    End users are encouraged to use `lhs % rhs` instead, which follows Python convetion,
    and enjoys better optimization in FreeTensor

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.remainder

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The remainder
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.remainder(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeRemainder(lhs, rhs)
    if type(lhs) is int and type(rhs) is int:
        return lhs - rhs * round_towards_0_div(lhs, rhs)
    raise TypeError("remainder is only supported for integer operands")


def min(lhs, rhs):
    '''
    Minimum of `lhs` and `rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.min

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The minimum
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.min(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeMin(lhs, rhs)
    return builtins.min(lhs, rhs)


def max(lhs, rhs):
    '''
    Maximum of `lhs` and `rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.max

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The maximum
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.max(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeMax(lhs, rhs)
    return builtins.max(lhs, rhs)


def l_and(lhs, rhs):
    '''
    Logical and of `lhs` and `rhs`

    NOTE: Short-circuit evaluation is NOT supported

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.l_and

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The logical and
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.l_and(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeLAnd(lhs, rhs)
    if type(lhs) is bool and type(rhs) is bool:
        return lhs and rhs
    raise TypeError("l_and is only supported for boolean operands")


def l_or(lhs, rhs):
    '''
    Logical or of `lhs` and `rhs`

    NOTE: Short-circuit evaluation is NOT supported

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.l_or

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The logical or
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.l_or(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeLOr(lhs, rhs)
    if type(lhs) is bool and type(rhs) is bool:
        return lhs or rhs
    raise TypeError("l_or is only supported for boolean operands")


def lt(lhs, rhs):
    '''
    `lhs < rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.lt

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The comparison
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.lt(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeLT(lhs, rhs)
    return lhs < rhs


def le(lhs, rhs):
    '''
    `lhs <= rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.le

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The comparison
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.le(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeLE(lhs, rhs)
    return lhs <= rhs


def gt(lhs, rhs):
    '''
    `lhs > rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.gt

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The comparison
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.gt(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeGT(lhs, rhs)
    return lhs > rhs


def ge(lhs, rhs):
    '''
    `lhs >= rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.ge

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The comparison
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.ge(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeGE(lhs, rhs)
    return lhs >= rhs


def eq(lhs, rhs):
    '''
    `lhs == rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.eq

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The comparison
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.eq(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeEQ(lhs, rhs)
    return lhs == rhs


def ne(lhs, rhs):
    '''
    `lhs != rhs`

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.ne

    Parameters
    ----------
    lhs : VarRef or Number
        The left-hand-side operand
    rhs : VarRef or Number
        The right-hand-side operand

    Returns
    -------
    VarRef or Number
        The comparison
    '''
    if _is_tensor(lhs) or _is_tensor(rhs):
        from .. import libop
        return libop.ne(lhs, rhs)
    if _is_runtime_scalar(lhs) or _is_runtime_scalar(rhs):
        return ffi.makeNE(lhs, rhs)
    return lhs != rhs


######################################
# Unary Operators


def l_not(expr):
    '''
    Logical not

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.l_not

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The logical not
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.l_not(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeLNot(expr)
    if type(expr) is bool:
        return not expr
    raise TypeError("l_not is only supported for boolean operands")


def neg(expr):
    '''
    Negation

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.neg

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The negation
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.neg(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeSub(0, expr)
    return -expr


def abs(expr):
    '''
    Absolute value

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.abs

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The absolute value
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.abs(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeAbs(expr)
    return builtins.abs(expr)


def sqrt(expr):
    '''
    Square root

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.sqrt

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The square root
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.sqrt(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeSqrt(expr)
    return math.sqrt(expr)


def exp(expr):
    '''
    Natural exponent

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.exp

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The exponent
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.exp(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeExp(expr)
    return math.exp(expr)


def ln(expr):
    '''
    Natural logarithm

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.ln

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The exponent
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.ln(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeLn(expr)
    return math.log(expr)  # Defaults to ln without the base


def square(expr):
    '''
    Square

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.square

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The square
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.square(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeSquare(expr)
    return expr * expr


def sigmoid(expr):
    '''
    Sigmoid

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.sigmoid

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.sigmoid(expr)
    return ffi.makeSigmoid(expr)


def sin(expr):
    '''
    Sine

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.tanh

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.sin(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeSin(expr)
    return math.sin(expr)


def cos(expr):
    '''
    Cosine

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.tanh

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.cos(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeCos(expr)
    return math.cos(expr)


def tan(expr):
    '''
    Tangent

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.tanh

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.tan(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeTan(expr)
    return math.tan(expr)


def tanh(expr):
    '''
    Hyperbolic tangent

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.tanh

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.tanh(expr)
    if _is_runtime_scalar(expr):
        return ffi.makeTanh(expr)
    return math.tanh(expr)


def floor(expr):
    '''
    Round a float down to an interger (towards -inf)

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.floor

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.floor(expr)
    return ffi.makeFloor(expr)


def ceil(expr):
    '''
    Round a float up to an interger (towards +inf)

    For scalar operands, it emit an expression node in AST. For non-scalar operands,
    it calls libop.ceil

    Parameters
    ----------
    expr : VarRef or Number
        The operand

    Returns
    -------
    VarRef or Number
        The result
    '''
    if _is_tensor(expr):
        from .. import libop
        return libop.ceil(expr)
    return ffi.makeCeil(expr)


def unbound(expr):
    '''
    Explicitly mark a bool expression not to used for boundary analysis and thus
    not used for shrinking loops or variables

    This is useful when we want to leave a loop length constatnt or regular, and
    guard it with a branch, e.g.:

    ```
    for i = 0 to 100
      if unbound(i < x)
        ...
    ```

    Parameters
    ----------
    expr : VarRef or Number
        The bool expression to mark

    Returns
    -------
    VarRef or Number
        The marked expression
    '''
    if isinstance(expr, bool):
        return expr
    return ffi.makeUnbound(expr)


def if_then_else(cond, then_case, else_case):
    '''
    Similar to `then_case if cond else else_case`

    NOTE: there is NO guarantee that only one branch will be executed. In some cases,
    both branches will be executed and the result of one of them will be picked.
    Therefore, please do NOT use `if_then_else` to guard an out-of-bound array indexing

    Parameters
    ----------
    cond : VarRef of Number
        Condition
    then_case : VarRef or Number
        Then-case experssion
    else_case : VarRef or Number
        Else-case expression

    Returns
    -------
    VarRef or Number
        The result
    '''
    if type(cond) is bool:
        return then_case if cond else else_case
    return ffi.makeIfExpr(cond, then_case, else_case)


def cast(expr, dtype):
    '''
    Cast to another type

    Parameters
    ----------
    expr : VarRef or Number
        The operand
    dtype : DataTypr or str
        The target data type

    Returns
    -------
    VarRef or Number
        The result
    '''
    return ffi.makeCast(expr, ffi.DataType(dtype))


def intrinsic(fmt, *params, ret_type="void", has_side_effect: bool = False):
    """
    Invoke whatever target code

    Parameters
    ----------
    fmt : str
        What to run. "%" is filled by parameters one by one. E.g. sinf(%). Use "%%" to
        escape for "%". If you need two adjacent parameters, type "(%)(%)" or "% %".
    *params : Sequence[Expr]
        (Positional variadic) Parameters to `fmt`
    ret_type : DataType or str
        (Keyword argument only) The return type. Void for no return type. Defaults to Void
    has_side_effect: bool
        (Keyword argument only) True to indicate the intrinsic modifes something other
        than the return value. Defaults to false
    """
    ret_type = ffi.DataType(ret_type)
    return ffi.makeIntrinsic(fmt, params, ret_type, has_side_effect)


def any():
    '''
    Create an AnyExpr node (only for testing and type inference)

    Any nodes matches any expression nodes in `ast.match`
    '''
    return ffi.makeAnyExpr()


def load_at_version(tape_name: str, dtype, *indices):
    '''
    Create an LoadAtVersion node (only for custom gradient)

    This node is only used for custom gradient. See `UserGradForPrevStmt`.
    '''
    return ffi.makeLoadAtVersion(tape_name, indices, ffi.DataType(dtype))


def ndim(var):
    ''' Get the number of dimensions of a variable '''
    if isinstance(var, VarRef):
        return var.ndim
    else:
        return 0


def shape(var, i=None):
    ''' shape(var, i): Get size of specified dimension of a variable
        shape(var): Get sizes of all dimensions of a variable '''
    if isinstance(var, VarRef):
        return var.shape(i)
    else:
        if i is None:
            return ()
        else:
            raise Exception(f'Getting size of dimension {i} of scalar {var}')


@dataclass
class UndeclaredParam:
    ''' Error type. For error reporting only. '''

    name: str


def dtype(var):
    ''' Get element data type of a variable '''
    if isinstance(var, VarRef):
        return var.dtype
    elif isinstance(var, ffi.Expr):
        return var.dtype
    else:
        # TODO: Config default type
        if isinstance(var, bool):
            # NOTE: before int, because bool in Python is a sub-class of int
            return ffi.DataType("bool")
        elif isinstance(var, float):
            return ffi.DataType("float32")
        elif isinstance(var, int):
            return ffi.DataType("int32")
        elif isinstance(var, UndeclaredParam):
            raise TypeError(
                f"Parameter '{var.name}' should be declared to be either a `freetensor.Var`"
                f" or a `freetensor.JIT`")
        else:
            raise TypeError(f"Unknown scalar type: {type(var)}")


def mtype(var):
    ''' Get memory type of a variable '''
    if isinstance(var, VarRef):
        return var.mtype
    else:
        return 'byvalue'


def _register_operators(cls):
    cls.__add__ = add
    cls.__radd__ = lambda self, other: add(other, self)
    cls.__sub__ = sub
    cls.__rsub__ = lambda self, other: sub(other, self)
    cls.__mul__ = mul
    cls.__rmul__ = lambda self, other: mul(other, self)
    cls.__truediv__ = truediv
    cls.__rtruediv__ = lambda self, other: truediv(other, self)
    cls.__floordiv__ = floordiv
    cls.__rfloordiv__ = lambda self, other: floordiv(other, self)
    cls.__mod__ = mod
    cls.__rmod__ = lambda self, other: mod(other, self)
    cls.__lt__ = lt
    cls.__le__ = le
    cls.__gt__ = gt
    cls.__ge__ = ge
    cls.__eq__ = eq
    cls.__ne__ = ne
    cls.__neg__ = neg


_register_operators(ffi.FrontendVar)
_register_operators(ffi.Expr)
