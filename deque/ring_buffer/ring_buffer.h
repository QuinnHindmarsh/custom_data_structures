#pragma once
#include <vector>
#include <stdexcept>
#include <concepts>
#include <algorithm>
#include <utility>
#include <bit>
#include <memory>
#include <cstring>
#include <type_traits>
#include <initializer_list>
#include <string>

/*
TODO:
optimisations
- thread to allocate in advanced when it is nearing capacity?
- thread to then dealloc after?

expansions
- itterators
*/
template <std::move_constructible T>
class ring_buffer
{
    /* Out of bounds actions are purposefully undefined behaviour, to match std::deque.
       Use .at() for a bounds-checked access. */
public:
    // Standard member types. Generic code (and the std algorithms) ask for these by name,
    // e.g. typename Container::value_type, so a container should provide them.
    using value_type = T;
    using size_type = std::size_t;
    using reference = T &;
    using const_reference = const T &;

    // & m_mask only works if capacity is a power of 2, so round up (0 becomes 1)
    explicit ring_buffer(size_t inital_capacity = 2) : m_capacitty(std::bit_ceil(inital_capacity)), m_data{m_alloc.allocate(m_capacitty)}, m_mask{m_capacitty - 1} {}

    ring_buffer(std::initializer_list<T> list) : ring_buffer(list.size())
    {
        touch_pages(m_capacitty, m_data);

        m_tail = list.size();
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (list.size() > 0) // an empty list's begin() can be nullptr, and memcpy from nullptr is UB even for 0 bytes
            {
                std::memcpy(m_data, list.begin(), list.size() * sizeof(T));
            }
        }
        else
        {
            std::uninitialized_copy(list.begin(), list.end(), m_data);
        }
    }

    ring_buffer(const ring_buffer &source) :

                                             m_capacitty{source.m_capacitty},
                                             m_mask{source.m_mask},
                                             m_head{0},

                                             m_tail{source.size()}
    {
        m_data = m_alloc.allocate(m_capacitty);
        size_t h{source.m_head & m_mask};
        size_t n{size()};
        size_t first{std::min(n, m_capacitty - h)};
        size_t second{n - first};

        touch_pages(m_capacitty, m_data);

        // n == 0 covers a moved-from source, whose m_data is nullptr. memcpy from nullptr is UB even for 0 bytes.
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (n > 0)
            {
                std::memcpy(m_data, source.m_data + h, first * sizeof(T));
                std::memcpy(m_data + first, source.m_data, second * sizeof(T));
            }
        }
        else
        {
            std::uninitialized_copy(source.m_data + h, source.m_data + h + first, m_data);
            std::uninitialized_copy(source.m_data, source.m_data + second, m_data + first);
        }
    }

    // Move constructor: steal source's buffer instead of copying every element. O(1).
    // source is left empty with no buffer (capacity 0). It is still valid: it can be pushed to
    // (grow handles capacity 0), assigned to, or destroyed.
    // noexcept matters: std::vector<ring_buffer> only moves (rather than copies) its elements when
    // it grows if the move constructor is noexcept, since it cannot undo a half finished move.
    ring_buffer(ring_buffer &&source) noexcept
        : m_capacitty{std::exchange(source.m_capacitty, 0)},
          m_data{std::exchange(source.m_data, nullptr)},
          m_mask{std::exchange(source.m_mask, 0)},
          m_head{std::exchange(source.m_head, 0)},
          m_tail{std::exchange(source.m_tail, 0)}
    {
    }

    ring_buffer &operator=(const ring_buffer &source)
    {
        ring_buffer copy(source); // slightly unoptimal, if the buffer of this is large enough to be reused it should be
        swap(copy);
        return *this;
    }

    // Move assignment: move source into a temporary, then swap it in. Our old contents end up in the
    // temporary and are destroyed when it goes out of scope. Self move (a = std::move(a)) is safe:
    // the temporary takes the buffer and the swap hands it straight back.
    ring_buffer &operator=(ring_buffer &&source) noexcept
    {
        ring_buffer tmp(std::move(source));
        swap(tmp);
        return *this;
    }

    ~ring_buffer()
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (size_t i{m_head}; i != m_tail; ++i)
            {
                std::destroy_at(m_data + (i & m_mask));
            }
        }

        if (m_data != nullptr) // nullptr after being moved from
        {
            m_alloc.deallocate(m_data, m_capacitty);
        }
    }

    void swap(ring_buffer &other) noexcept
    {
        std::swap(m_capacitty, other.m_capacitty);
        std::swap(m_data, other.m_data);
        std::swap(m_mask, other.m_mask);
        std::swap(m_head, other.m_head);
        std::swap(m_tail, other.m_tail);
    }

    // Free function swap, found by argument dependent lookup. This is what std::swap(a, b) style
    // generic code (`using std::swap; swap(a, b);`) calls, so it picks the O(1) member swap
    // instead of the generic three moves.
    friend void swap(ring_buffer &a, ring_buffer &b) noexcept { a.swap(b); }

    void push_front(const T &element) { emplace_front(element); }
    void push_front(T &&element) { emplace_front(std::move(element)); }

    // Builds the element in place from args, so no temporary T is made.
    // Returns a reference to the new element, like std::deque since C++17.
    template <typename... Args>
    T &emplace_front(Args &&...args)
    {
        if (size() == m_capacitty) [[unlikely]]
        {
            return emplace_front_grow(std::forward<Args>(args)...);
        }
        T *p = m_data + ((m_head - 1) & m_mask);
        std::construct_at(p, std::forward<Args>(args)...);
        --m_head; // only after construction succeeds, so a throwing constructor leaves the buffer unchanged
        return *p;
    }

    void pop_front()
    {
        std::destroy_at(m_data + (m_head & m_mask));
        ++m_head;
    }
    T pop_front_return()
    {
        T *p = m_data + (m_head & m_mask);
        T out = std::move(*p);
        std::destroy_at(p);
        ++m_head;
        return out;
    }

    const T &front() const
    {
        return m_data[m_head & m_mask];
    };

    T &front()
    {
        return const_cast<T &>(std::as_const(*this).front());
    }

    void push_back(const T &element) { emplace_back(element); }
    void push_back(T &&element) { emplace_back(std::move(element)); }

    template <typename... Args>
    T &emplace_back(Args &&...args)
    {
        if (size() == m_capacitty) [[unlikely]]
        {
            return emplace_back_grow(std::forward<Args>(args)...);
        }
        T *p = m_data + (m_tail & m_mask);
        std::construct_at(p, std::forward<Args>(args)...);
        ++m_tail;
        return *p;
    }
    void pop_back()
    {
        --m_tail;
        std::destroy_at(m_data + (m_tail & m_mask));
    }

    T pop_back_return()
    {
        T *p = m_data + ((m_tail - 1) & m_mask);
        T out = std::move(*p);
        std::destroy_at(p);
        --m_tail;
        return out;
    }

    const T &back() const
    {
        return m_data[(m_tail - 1) & m_mask];
    }

    T &back()
    {
        return m_data[(m_tail - 1) & m_mask];
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return m_tail - m_head;
    }

    // [[nodiscard]]: `rb.empty();` on its own line is almost always a typo for `rb.clear();`
    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] size_t capacity() const noexcept
    {
        return m_capacitty;
    }

    // Destroys every element. Keeps the buffer, so capacity() is unchanged.
    void clear() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (size_t i{m_head}; i != m_tail; ++i)
            {
                std::destroy_at(m_data + (i & m_mask));
            }
        }
        m_head = 0;
        m_tail = 0;
    }

    // Bounds-checked access: throws std::out_of_range instead of being undefined behaviour.
    T &at(std::size_t i)
    {
        return const_cast<T &>(std::as_const(*this).at(i));
    }
    const T &at(std::size_t i) const
    {
        if (i >= size())
        {
            throw std::out_of_range("ring_buffer::at: index " + std::to_string(i) +
                                    " out of range for size " + std::to_string(size()));
        }
        return (*this)[i];
    }

    T &operator[](std::size_t i)
    {
        return m_data[(m_head + i) & m_mask];
    }
    const T &operator[](std::size_t i) const
    {
        return m_data[(m_head + i) & m_mask];
    }

    // Never shrinks: asking for less than the current capacity does nothing
    void reserve(size_t n)
    {
        if (n > m_capacitty)
        {
            grow(std::bit_ceil(n));
        }
    }

    // Same elements in the same order. Capacity and where the elements sit in memory do not matter.
    // Only exists if T can be compared with ==.
    friend bool operator==(const ring_buffer &a, const ring_buffer &b)
        requires std::equality_comparable<T>
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (size_t i{0}; i < a.size(); ++i)
        {
            if (!(a[i] == b[i]))
            {
                return false;
            }
        }
        return true;
    }

private:
    std::allocator<T> m_alloc{};
    size_t m_capacitty;

    static constexpr size_t resize_factor = 2; // resize factor being a power of 2, allows for & instead of %

    T *m_data;
    size_t m_mask;
    size_t m_head{0}; // index of first element
    size_t m_tail{0}; // index of after last element

    void touch_pages(size_t capacity, T *data)
    {
        // The OS only maps RAM to a page on its first write. Touch every page now, front to back,
        // otherwise push_front faults them in backwards one at a time, which is ~2x slower.
        // volatile stops the compiler deleting these writes, since nothing reads them.
        auto *bytes = reinterpret_cast<volatile char *>(data);
        for (size_t off = 0; off < capacity * sizeof(T); off += 4096)
        {
            bytes[off] = 0;
        }
    }

    size_t next_capacity() const
    {
        // capacity is 0 after being moved from, and 0 * 2 is still 0
        return m_capacitty == 0 ? 1 : m_capacitty * resize_factor;
    }

    // Slow paths for emplace when the buffer is full.
    // args may refer to an element of this buffer, e.g. rb.push_back(rb.front()). grow() frees the old
    // storage, so building from args after grow would read freed memory. Build the new element first,
    // then grow, then move it into place. std::deque guarantees this case works, so we must too.
    template <typename... Args>
    [[gnu::noinline]] T &emplace_back_grow(Args &&...args)
    {
        T tmp(std::forward<Args>(args)...);
        grow(next_capacity());
        T *p = m_data + (m_tail & m_mask);
        std::construct_at(p, std::move(tmp));
        ++m_tail;
        return *p;
    }

    template <typename... Args>
    [[gnu::noinline]] T &emplace_front_grow(Args &&...args)
    {
        T tmp(std::forward<Args>(args)...);
        grow(next_capacity());
        T *p = m_data + ((m_head - 1) & m_mask);
        std::construct_at(p, std::move(tmp));
        --m_head;
        return *p;
    }

    [[gnu::noinline]] void grow(size_t new_capacity)
    {
        T *new_data = m_alloc.allocate(new_capacity);
        size_t h{m_head & m_mask};
        size_t n{size()};
        size_t first{std::min(n, m_capacitty - h)};
        size_t second{n - first};

        touch_pages(new_capacity, new_data);

        if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (n > 0) // m_data is nullptr after being moved from
            {
                std::memcpy(new_data, m_data + h, first * sizeof(T));
                std::memcpy(new_data + first, m_data, second * sizeof(T));
            }
        }
        else
        {
            std::uninitialized_move(m_data + h, m_data + h + first, new_data);
            std::uninitialized_move(m_data, m_data + second, new_data + first);
            std::destroy(m_data + h, m_data + h + first);
            std::destroy(m_data, m_data + second);
        }

        if (m_data != nullptr) // nullptr after being moved from
        {
            m_alloc.deallocate(m_data, m_capacitty);
        }

        m_data = new_data;

        m_head = 0;
        m_tail = n; // not m_capacitty: reserve() can grow a buffer that isn't full
        m_capacitty = new_capacity;
        m_mask = new_capacity - 1;
    }
};
