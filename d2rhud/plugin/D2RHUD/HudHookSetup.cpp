#include "HudHookSetup.h"
#include <utility>

namespace HudHooks {
Installer::Installer(std::string name, std::initializer_list<Hook> hooks, Operations operations)
    : name_(std::move(name)), hooks_(hooks), previous_(hooks_.size()), operations_(operations) {}

Report Installer::Failure(LONG error, std::string_view phase, std::string_view hook) {
    const bool changed = lastError_ != error || lastPhase_ != phase || lastHook_ != hook;
    lastError_ = error; lastPhase_ = phase; lastHook_ = hook;
    return {Outcome::Failed, error, name_, phase, hook, changed};
}
Report Installer::Tick(uint64_t now) {
    if (ready_) return {Outcome::AlreadyInstalled};
    if (attempts_ && now >= lastAttempt_ && now - lastAttempt_ < 1000) return {Outcome::Waiting};
    lastAttempt_ = now; ++attempts_;
    if (hooks_.empty()) return Failure(ERROR_INVALID_PARAMETER, "configuration");
    for (size_t i = 0; i < hooks_.size(); ++i) {
        const auto& hook = hooks_[i];
        if (!hook.original || !hook.target || !hook.replacement || hook.target == hook.replacement)
            return Failure(ERROR_INVALID_PARAMETER, "configuration", hook.name);
        for (size_t other = 0; other < i; ++other)
            if (hook.original == hooks_[other].original || hook.target == hooks_[other].target)
                return Failure(ERROR_INVALID_PARAMETER, "duplicate hook", hook.name);
        previous_[i] = *hook.original;
    }

    LONG error = NO_ERROR;
    std::string_view phase = "transaction setup", failedHook;
    {
        Transaction transaction(operations_);
        error = transaction.Error();
        if (error == NO_ERROR) {
            for (const auto& hook : hooks_) {
                *hook.original = hook.target;
                if (!transaction.AttachRaw(hook.original, hook.replacement)) {
                    error = transaction.Error(); phase = "attach"; failedHook = hook.name; break;
                }
            }
            if (error == NO_ERROR && !transaction.Commit()) {
                error = transaction.Error(); phase = "commit";
            }
        }
        // Failed/incomplete transactions abort here, before restoring our slots.
    }
    if (error != NO_ERROR) {
        for (size_t i = 0; i < hooks_.size(); ++i) *hooks_[i].original = previous_[i];
        return Failure(error, phase, failedHook);
    }
    ready_ = true;
    return {Outcome::Installed, NO_ERROR, name_};
}
}
