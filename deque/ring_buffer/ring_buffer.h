#pragma once
#include <vector>
#include <stdexcept>
#include <concepts>
#include <algorithm>
#include <utility>
#include <bit>
#include <memory>

/*
TODO:


optimisations
- thread to allocate in advanced when it is nearing capacity?
- thread to then dealloc after?
- head/tail node cached
- take in T as a const ref then construct directly?

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

    void push_front(const T &element) { emplace_front(element); }
    void push_front(T &&element) { emplace_front(std::move(element)); }

    template <typename... Args>
    void emplace_front(Args &&...args)
    {
        if (size() == m_capacitty)
        {
            grow();
        }
        --m_head;
        std::construct_at(m_data + (m_head & m_mask), std::forward<Args>(args)...);
        m_head_ptr = m_data[(m_head & m_mask)];
    }

    void pop_front()
    {
        if (m_head_ptr)
        {
            std::destroy_at(m_head_ptr);
            m_head_ptr = nullptr;
        }
        else
        {
            std::destroy_at(m_data + (m_head & m_mask));
        }
        ++m_head;
    }
    T pop_front_return()
    {

        if (m_head_ptr)
        {
            T out = std::move(*m_head_ptr);
            std::destroy_at(m_head_ptr);
            m_head_ptr = nullptr;
            ++m_head;
            return out;
        }

        T *p = m_data + (m_head & m_mask);
        T out = std::move(*p);
        std::destroy_at(p);
        ++m_head;
        return out;
    }

    const T &front() const
    {
        if (m_head_ptr)
        {
            return *m_head_ptr;
        }
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
        if (size() == m_capacitty)
        {
            grow();
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

    // TODO: add deconstructor, copy assignment awnd copy constructor

private:
    std::allocator<T> m_alloc{};
    size_t m_capacitty;

    size_t m_resize_factor{2}; // resize factor being a power of 2, allows for & instead of %

    T *m_data;
    size_t m_mask;
    size_t m_head{0}; // index of first element
    size_t m_tail{0}; // index of after last element
    T *m_head_ptr{nullptr};
    T *m_tail_ptr{nullptr};

    void grow()
    {
        size_t new_capacity = m_capacitty * m_resize_factor;
        T *new_data = m_alloc.allocate(new_capacity);

        // The OS only maps RAM to a page on its first write. Touch every page now, front to back,
        // otherwise push_front faults them in backwards one at a time, which is ~2x slower.
        // volatile stops the compiler deleting these writes, since nothing reads them.
        auto *bytes = reinterpret_cast<volatile char *>(new_data);
        for (size_t off = 0; off < new_capacity * sizeof(T); off += 4096)
        {
            bytes[off] = 0;
        }

        for (size_t i{0};
             i < size();
             ++i)
        {
            T *src = m_data + ((m_head + i) & m_mask);
            std::construct_at(new_data + i, std::move(*src));
            std::destroy_at(src);
        }
        m_alloc.deallocate(m_data, m_capacitty);

        m_data = new_data;

        m_head = 0;
        m_tail = m_capacitty;
        m_capacitty = new_capacity;
        m_mask = new_capacity - 1;
    }
};
