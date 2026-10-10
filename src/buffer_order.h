// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <cstddef>
namespace chord {
// Audio blocks must be submitted in completion-queue order, even if the
// producer wakes after several headers have completed across a ring wrap.
template<std::size_t Count> class BufferOrder {
    std::size_t next = 0;
public:
    std::size_t front() const { return next; }
    void submitted() { next = (next + 1) % Count; }
};
}
