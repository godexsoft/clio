#include "util/requests/SslContext.hpp"

#include "util/requests/impl/SslContext.hpp"

#include <optional>
#include <string>

namespace util::requests {

std::optional<std::string>
initClientSslContext()
{
    return impl::initClientSslContext();
}

}  // namespace util::requests
