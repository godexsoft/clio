#include "etl/WriterState.hpp"

#include "etl/SystemState.hpp"

#include <memory>
#include <utility>

namespace etl {

WriterState::WriterState(std::shared_ptr<SystemState> state) : systemState_(std::move(state))
{
}

bool
WriterState::isReadOnly() const
{
    return systemState_->isStrictReadonly;
}

bool
WriterState::isWriting() const
{
    return systemState_->isWriting;
}

void
WriterState::startWriting()
{
    if (isWriting())
        return;

    systemState_->writeCommandSignal(SystemState::WriteCommand::StartWriting);
}

void
WriterState::giveUpWriting()
{
    if (not isWriting())
        return;

    systemState_->writeCommandSignal(SystemState::WriteCommand::StopWriting);
}

void
WriterState::setWriterDecidingFallback()
{
    systemState_->isWriterDecidingFallback = true;
}

bool
WriterState::isFallback() const
{
    return systemState_->isWriterDecidingFallback;
}

bool
WriterState::isLoadingCache() const
{
    return systemState_->isLoadingCache;
}

std::unique_ptr<WriterStateInterface>
WriterState::clone() const
{
    auto c = WriterState(*this);
    return std::make_unique<WriterState>(std::move(c));
}

}  // namespace etl
