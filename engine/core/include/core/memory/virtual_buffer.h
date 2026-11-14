#pragma once

#include "core/types.h"

namespace aurora {

// A reserved address range whose committed prefix grows without moving: pointers into it stay valid.
// Single owner, not thread-safe. Base for Arena, Pool and DenseArray.
class VirtualBuffer {
public:
    VirtualBuffer() = default;
    ~VirtualBuffer();

    VirtualBuffer(VirtualBuffer&& other) noexcept;
    VirtualBuffer& operator=(VirtualBuffer&& other) noexcept;
    VirtualBuffer(const VirtualBuffer&) = delete;
    VirtualBuffer& operator=(const VirtualBuffer&) = delete;

    [[nodiscard]] bool reserve(usize capacity);

    [[nodiscard]] bool ensure_committed(usize size);

    void decommit_all();

    void release();

    u8*   data() const { return base_; }
    usize capacity() const { return capacity_; }
    usize committed() const { return committed_; }
    bool  is_reserved() const { return base_ != nullptr; }

private:
    u8*   base_      = nullptr;
    usize capacity_  = 0;
    usize committed_ = 0;
};

} // namespace aurora
