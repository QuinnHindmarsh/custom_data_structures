#pragma once
#include <vector>
#include <stdexcept>
#include <concepts>
#include <algorithm>
#include <utility>
#include <bit>

/*
TODO:

fixes
- overright on pops

optimisations
- manage my own memory? why am I using std::vector?
    - std::allocator, std::construct at. also removes default initilisation requirement
- thread to allocate in advanced when it is nearing capacity
- thread to then clean up the whole vec
- head/tail node cached
- remove capacity
- take in T as a const ref then construct directly?

expansions
- .emplace
- itterators
*/

template <std::default_initializable T>
class ring_buffer
{
public:
    // & m_mask only works if capacity is a power of 2, so round up (0 becomes 1)
    explicit ring_buffer(size_t inital_capacity = 2) : m_buf(std::bit_ceil(inital_capacity)), m_capacitty{m_buf.size()}, m_mask{m_buf.size() - 1} {};

    void push_front(T element)
    {
        if (size() == m_capacitty)
        {
            grow();
        }
        // if head = 0, this wraps around
        // ((size_t) 0 - 1) = max_size_t
        --m_head;
        m_buf[m_head & m_mask] = std::move(element);
    }

    void pop_front()
    {
        ++m_head;
    }

    T pop_front_return()
    {

        return std::move(m_buf[(m_head++) & m_mask]);
        ;
    }

    const T &front() const
    {

        return m_buf[m_head & m_mask];
    }

    T &front()
    {

        return m_buf[m_head & m_mask];
    }


    void push_back(T element)
    {
        if (size() == m_capacitty)
        {
            grow();
        }
        m_buf[m_tail & m_mask] = std::move(element);
        ++m_tail;
    }

    void pop_back()
    {
        --m_tail;
    }

    T pop_back_return()
    {

        return std::move(m_buf[--m_tail & m_mask]);
    }

    const T &back() const
    {

        return m_buf[(m_tail - 1) & m_mask];
    }

    T &back()
    {
        return m_buf[(m_tail - 1) & m_mask];
    }

    size_t size() const
    {
        return m_tail - m_head;
    }

    bool empty() const
    {
        return (m_tail - m_head == 0);
    }

    T &operator[](std::size_t i)
    {
        return m_buf[(m_head + i) & m_mask];
    }
    const T &operator[](std::size_t i) const
    {
        return m_buf[(m_head + i) & m_mask];
    }

private:
    size_t m_resize_factor{2}; // resize factor being a power of 2, allows for & instead of %

    std::vector<T> m_buf;
    size_t m_capacitty;
    size_t m_mask;
    size_t m_head{0}; // index of first element
    size_t m_tail{0}; // index of after last element

    void grow()
    {
        std::size_t n = size();
        std::vector<T> new_buf(n * m_resize_factor);

        size_t h{m_head & m_mask};
        size_t first_length{std::min(n, m_capacitty - h)};

        T *src = m_buf.data();
        T *dest = new_buf.data();
        std::move(src + h, src + h + first_length, dest);              // head to end
        std::move(src, src + (n - first_length), dest + first_length); // end to tail

        m_buf = std::move(new_buf);
        m_mask = m_buf.size() - 1;
        m_capacitty *= m_resize_factor;

        // elements now start at index 0 of the new buffer
        m_head = 0;
        m_tail = n;
    }
};
