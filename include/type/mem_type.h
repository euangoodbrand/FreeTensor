#ifndef FREE_TENSOR_MEM_TYPE_H
#define FREE_TENSOR_MEM_TYPE_H

#include <array>
#include <iostream>
#include <string>

#include <container_utils.h>
#include <except.h>
#include <serialize/to_string.h>

namespace freetensor {

enum class MemType : size_t {
    ByValue = 0, // Passed by value. Always in stack or registers
    CPU,         // Main memory
    GPUGlobal,
    GPUShared,
    GPULocal,
    GPUWarp,
    // ------
    CPUHeap,       // AccessType must be Cache
    GPUGlobalHeap, // ditto
    // ------
    NumTypes,
};

// First deduce array length, then assert, to ensure the length
constexpr std::array memTypeNames = {
    "byvalue",   "cpu",      "gpu/global", "gpu/shared",
    "gpu/local", "gpu/warp", "cpu/heap",   "gpu/global/heap",
};
static_assert(memTypeNames.size() == (size_t)MemType::NumTypes);

namespace detail {

template <typename T, T... i>
constexpr auto createAllMemTypes(std::integer_sequence<T, i...>) {
    return std::array{(MemType)i...};
}

} // namespace detail

constexpr auto allMemTypes = detail::createAllMemTypes(
    std::make_index_sequence<(size_t)MemType::NumTypes>{});

inline std::ostream &operator<<(std::ostream &os, MemType mtype) {
    return os << memTypeNames.at((size_t)mtype);
}

inline MemType parseMType(const std::string &_str) {
    auto &&str = tolower(_str);
    for (auto &&[i, s] : views::enumerate(memTypeNames)) {
        if (s == str) {
            return (MemType)i;
        }
    }
    ERROR(FT_MSG << "Unrecognized memory type \"" << _str
                 << "\". Candidates are (case-insensitive): "
                 << (memTypeNames | join(", ")));
}

} // namespace freetensor

#endif // FREE_TENSOR_MEM_TYPE_H
