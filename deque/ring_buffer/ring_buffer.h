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

/*
TODO:
optimisations
- thread to allocate in advanced when it is nearing capacity?
- thread to then dealloc after?

expansions
- itterators
- .at
*/
template <std::move_constructible T>
class ring_buffer
{
    /* Out of bounds actions are purposefully undefined behaviour, to match std::deque*/
public:
    // & m_mask only works if capacity is a power of 2, so round up (0 becomes 1)
    explicit ring_buffer(size_t inital_capacity = 2) : m_capacitty(std::bit_ceil(inital_capacity)), m_data{m_alloc.allocate(m_capacitty)}, m_mask{m_capacitty - 1} {}

    ring_buffer(std::initializer_list<T> list) : ring_buffer(list.size())
    {
        touch_pages(m_capacitty, m_data);

        m_tail = list.size();
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(m_data, list.begin(), list.size() * sizeof(T));
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

        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(m_data, source.m_data + h, first * sizeof(T));
            std::memcpy(m_data + first, source.m_data, second * sizeof(T));
        }
        else
        {
            std::uninitialized_copy(source.m_data + h, source.m_data + h + first, m_data);
            std::uninitialized_copy(source.m_data, source.m_data + second, m_data + first);
        }
    }

    ring_buffer &operator=(const ring_buffer &source)
    {
        ring_buffer copy(source); // slightly unoptimal, if the buffer of this is large enough to be reused it should be
        swap(copy);
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

        m_alloc.deallocate(m_data, m_capacitty);
    }

    void swap(ring_buffer &other)
    {
        std::swap(m_capacitty, other.m_capacitty);
        std::swap(m_data, other.m_data);
        std::swap(m_mask, other.m_mask);
        std::swap(m_head, other.m_head);
        std::swap(m_tail, other.m_tail);
    }

    void push_front(const T &element) { emplace_front(element); }
    void push_front(T &&element) { emplace_front(std::move(element)); }

    template <typename... Args>
    void emplace_front(Args &&...args)
    {
        if (size() == m_capacitty) [[unlikely]]
        {
            grow(m_capacitty * resize_factor);
        }
        --m_head;
        std::construct_at(m_data + (m_head & m_mask), std::forward<Args>(args)...);
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
    void emplace_back(Args &&...args)
    {
        if (size() == m_capacitty) [[unlikely]]
        {
            grow(m_capacitty * resize_factor);
        }
        std::construct_at(m_data + (m_tail & m_mask), std::forward<Args>(args)...);

        ++m_tail;
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

    size_t size() const
    {
        return m_tail - m_head;
    }

    bool empty() const
    {
        return size() == 0;
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
            std::memcpy(new_data, m_data + h, first * sizeof(T));
            std::memcpy(new_data + first, m_data, second * sizeof(T));
        }
        else
        {
            std::uninitialized_move(m_data + h, m_data + h + first, new_data);
            std::uninitialized_move(m_data, m_data + second, new_data + first);
            std::destroy(m_data + h, m_data + h + first);
            std::destroy(m_data, m_data + second);
        }

        m_alloc.deallocate(m_data, m_capacitty);

        m_data = new_data;

        m_head = 0;
        m_tail = n; // not m_capacitty: reserve() can grow a buffer that isn't full
        m_capacitty = new_capacity;
        m_mask = new_capacity - 1;
    }
};
