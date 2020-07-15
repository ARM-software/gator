/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_CORE_ONLINER_H
#define INCLUDE_LINUX_CORE_ONLINER_H

/**
 * Reads the online state of a cpu, then attempts to bring it online.
 * The destructor will restore the previous state if it was modified
 */
class CoreOnliner {
public:
    CoreOnliner(unsigned core);
    CoreOnliner(const CoreOnliner &) = delete;
    CoreOnliner(CoreOnliner &&) noexcept;
    ~CoreOnliner();
    CoreOnliner & operator=(const CoreOnliner &) = delete;
    CoreOnliner & operator=(CoreOnliner &&) noexcept;
    inline bool stateKnown() const { return known; }
    inline bool stateChanged() const { return changed; }
    inline bool isOnline() const { return online; }

private:
    unsigned core;
    bool known;
    bool changed;
    bool online;
};

#endif /* INCLUDE_LINUX_CORE_ONLINER_H */
