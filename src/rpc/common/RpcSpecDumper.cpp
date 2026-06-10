#include "rpc/common/RpcSpecDumper.hpp"

#include "rpc/common/impl/HandlerRegistry.hpp"
#include <rpcspec/SpecDumpWriter.hpp>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <vector>

namespace rpc {

void
dumpAllRpcSpecs(std::ostream& os, uint32_t apiVersion)
{
    auto const entries = impl::handlerRegistry();
    std::vector<impl::HandlerEntry const*> sorted;
    sorted.reserve(entries.size());
    for (auto const& e : entries)
        sorted.push_back(&e);
    std::ranges::sort(sorted, [](auto const* a, auto const* b) { return a->name < b->name; });

    spec::SpecDumpWriter writer{os};
    os << "apiVersion: " << apiVersion << "\nhandlers:\n";
    writer.push();
    for (auto const* entry : sorted) {
        writer.bulletGroup(entry->name, [&] {
            if (entry->specFn != nullptr) {
                entry->specFn(apiVersion).dump(writer);
            } else {
                writer.line("(no inputs)");
            }
        });
    }
    writer.pop();
}

}  // namespace rpc
