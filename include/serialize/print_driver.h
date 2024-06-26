#ifndef FREE_TENSOR_PRINT_DRIVER_H
#define FREE_TENSOR_PRINT_DRIVER_H

// for multi-machine-parallel xmlrpc

#include <string>

#include <driver/array.h>
#include <driver/device.h>
#include <driver/target.h>
#include <ref.h>

namespace freetensor {

std::pair<std::string, std::string> dumpTarget(const Ref<Target> &target_);
std::pair<std::string, std::string> dumpDevice(const Ref<Device> &device_);
std::pair<std::string, std::string> dumpArray(const Ref<Array> &array_);

} // namespace freetensor

#endif // FREE_TENSOR_PRINT_DRIVER_H
