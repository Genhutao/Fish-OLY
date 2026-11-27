#include "oly/core/id.hpp"
#include "oly/core/bytes.hpp"

namespace oly {

std::string ContentId::str() const { return to_hex(bytes); }

}   // namespace oly
