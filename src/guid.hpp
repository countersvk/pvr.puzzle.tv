#ifndef GUID_H
#define GUID_H

#include <string>
#include <array>
#include <format>

namespace CUSTOM_GUID {

[[nodiscard]] std::string generate() noexcept;

} // namespace CUSTOM_GUID

#endif // GUID_H
