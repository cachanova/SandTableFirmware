#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Small independent blocks avoid quadratic realloc/copy and large contiguous
// allocations when Print emits JSON one character at a time.
class ResponseBuffer {
public:
    using Allocate = void* (*)(size_t);
    using Release = void (*)(void*);
    explicit ResponseBuffer(size_t maximum = 65536, Allocate allocate = std::malloc,
                            Release release = std::free)
        : m_maximum(maximum), m_allocate(allocate), m_release(release) {}
    ~ResponseBuffer() { clear(); }
    ResponseBuffer(const ResponseBuffer&) = delete;
    ResponseBuffer& operator=(const ResponseBuffer&) = delete;

    bool append(const uint8_t* data, size_t length) {
        if (m_failed) return false;
        if (length > m_maximum - m_size) return fail();
        size_t remaining = length;
        while (remaining) {
            if (!m_tail || m_tail->end == kBlockSize) {
                auto* block = static_cast<Block*>(m_allocate(sizeof(Block)));
                if (!block) return fail();
                block->next = nullptr;
                block->begin = block->end = 0;
                if (m_tail) m_tail->next = block;
                else m_head = block;
                m_tail = block;
            }
            const size_t count = std::min(remaining, kBlockSize - m_tail->end);
            std::memcpy(m_tail->data + m_tail->end, data, count);
            m_tail->end += count;
            data += count;
            remaining -= count;
            m_size += count;
        }
        return true;
    }

    size_t read(uint8_t* data, size_t length) {
        size_t copied = 0;
        while (m_head && copied < length) {
            const size_t count = std::min(length - copied, m_head->end - m_head->begin);
            std::memcpy(data + copied, m_head->data + m_head->begin, count);
            m_head->begin += count;
            copied += count;
            m_size -= count;
            if (m_head->begin == m_head->end) {
                Block* old = m_head;
                m_head = old->next;
                if (!m_head) m_tail = nullptr;
                m_release(old);
            }
        }
        return copied;
    }

    bool failed() const { return m_failed; }
    size_t size() const { return m_size; }

private:
    static constexpr size_t kBlockSize = 1024;
    struct Block { Block* next; size_t begin; size_t end; uint8_t data[kBlockSize]; };
    void clear() {
        while (m_head) {
            Block* old = m_head;
            m_head = old->next;
            m_release(old);
        }
        m_tail = nullptr;
        m_size = 0;
    }
    bool fail() { clear(); m_failed = true; return false; }
    Block* m_head = nullptr;
    Block* m_tail = nullptr;
    size_t m_size = 0;
    size_t m_maximum;
    Allocate m_allocate;
    Release m_release;
    bool m_failed = false;
};
