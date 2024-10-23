/* Copyright (C) 2019-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_CORE_ONLINER_H
#define INCLUDE_LINUX_CORE_ONLINER_H

/**
 * Reads the online state of a cpu, then attempts to bring it online.
 * The destructor will restore the previous state if it was modified
 */
#include <optional>
class CoreOnliner {
public:
    CoreOnliner(unsigned core);
    CoreOnliner(const CoreOnliner &) = delete;
    CoreOnliner(CoreOnliner && that) noexcept;
    ~CoreOnliner();
    CoreOnliner & operator=(const CoreOnliner &) = delete;
    CoreOnliner & operator=(CoreOnliner && that) noexcept;

    [[nodiscard]] static std::optional<bool> isCoreOnline(unsigned core);

    [[nodiscard]] bool stateKnown() const { return known; }
    [[nodiscard]] bool stateChanged() const { return changed; }
    [[nodiscard]] bool isOnline() const { return online; }

private:
    unsigned core;
    bool known;
    bool changed;
    bool online;
};

#endif /* INCLUDE_LINUX_CORE_ONLINER_H */
