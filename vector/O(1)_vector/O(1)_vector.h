#include "freer.h"
#include <stdexcept>
#include <format>
#include <cassert>
#include <cstdint>   // uint8_t
#include <algorithm> // std::max
#include <thread>

/*

This is soley designed for cheap types -> optimisations have been made with the primary goal of optimising for int32_t. Optimisations for non cheap to copy objects like string would look very different. I may explore this in future
TODO:
interface
    - reserve
    - itterators
    - exmplace


optimisations
    - copy in chunks, std::copy. copy 16 every 8 ops etc
    - better support none integral types - only store 1 copy of each element and check m_left to find what array it is in.
        - this would have the side effect of allowing writable [] operators

*/

template <typename T>
class O1vector
{
public:
    explicit O1vector(size_t capacity = 1) : m_capacity{capacity}, m_cur{new T[m_capacity]}
    {
        assert(m_grow_at_denom < m_shrink_at_denom); // shrink point c/8 must sit below grow point c/2
        update_threasholds();
        background_freer<T>::instance(); // shared freer to deallocate memory
    }

    ~O1vector()
    {
        delete[] m_cur;
        delete[] m_next;
    }
    O1vector(const O1vector &) = delete;
    O1vector &operator=(const O1vector &) = delete;

    void pop_no_return()
    {
        check_not_empty("pop_no_return");

        --m_size;
        maybe_start_shrink();
        step_resize();
    }

    T pop() //  code duplication for the sake of not needing to repeat bounds check
    {
        check_not_empty("pop");

        T temp = std::move(m_cur[m_size - 1]);
        --m_size;
        maybe_start_shrink();
        step_resize();
        return temp;
    }

    void set(size_t idx, T element)
    {
        // forces you to set an an element < size, i.e. can only be used to modify an existing element. Prevents operations with the m_next array from needing to be handled

        check_index(idx, "set");
        m_cur[idx] = std::move(element);

        if (idx < m_left)
        {
            assert(m_next != nullptr);
            m_next[idx] = m_cur[idx];
        }
    }

    void push(T element)
    {

        assert(m_size < m_capacity);
        m_cur[m_size] = std::move(element);
        ++m_size;
        maybe_start_grow();
        step_resize();
    }

    const T *at_ptr(size_t idx) const // no garuntees this will be valid after any pop/push operation
    {
        check_index(idx, "at_ptr");
        return &m_cur[idx];
    }

    T at(size_t idx) const
    {
        check_index(idx, "at"); // own check (not via at_ptr) so the error names at()
        return m_cur[idx];
    }

    size_t size() const
    {
        return m_size;
    }

    bool not_empty() const // I think !vec.is_empty() is used a lot more then vec.is_empty, I think checking its not empty is cleaner
    {
        return m_size != 0;
    }

    size_t capacity() const
    {
        return m_capacity;
    }

    size_t next_capacity() const
    {
        return m_next_capacity;
    }

    const T &operator[](std::size_t i) const { return m_cur[i]; }

private:
    // paramater values
    int m_resize_factor{2};

    static constexpr uint8_t m_grow_at_denom{2};   // when size is at c/m_grow_at start creating a bigger vector
    static constexpr uint8_t m_shrink_at_denom{8}; // when size is c/m_shrink_at, start creating a smaller vector
    static constexpr uint8_t m_moves_per_op{4};
    static constexpr size_t k_async_free_bytes{256 * 1024};

    size_t m_size{0};
    size_t m_capacity;
    T *m_cur;
    T *m_next{nullptr}; // either an array of size c*2 or c/2 or nullptr if no resize is in progress
    size_t m_next_capacity{0};
    size_t m_left{0}; // all indexes < m_left exist in cur and m_next
    size_t m_shrink_threashold;
    size_t m_grow_threashold;

    // ---------- error reporting ----------
    // the checks stay small so they inline into the hot path, the message is only built in the
    // [[noreturn]] reporters, which only run when something has already gone wrong.
    // op is the public function's name, so the message says which call failed.

    void check_index(size_t idx, const char *op) const
    {
        if (idx >= m_size) [[unlikely]]
        {
            report_bad_index(idx, op);
        }
    }

    void check_not_empty(const char *op) const
    {
        if (m_size == 0) [[unlikely]]
        {
            report_empty(op);
        }
    }

    // e.g. "O1vector::at(5): index out of range, size is 3 (valid indices are 0 to 2)"
    [[noreturn]] void report_bad_index(size_t idx, const char *op) const
    {
        if (m_size == 0)
        {
            throw std::out_of_range(std::format("O1vector::{}({}): index out of range, the vector is empty", op, idx));
        }
        throw std::out_of_range(std::format("O1vector::{}({}): index out of range, size is {} (valid indices are 0 to {})",
                                            op, idx, m_size, m_size - 1));
    }

    // e.g. "O1vector::pop(): can't pop, the vector is empty"
    [[noreturn]] static void report_empty(const char *op)
    {
        throw std::out_of_range(std::format("O1vector::{}(): can't pop, the vector is empty", op));
    }

    void update_threasholds()
    {
        m_shrink_threashold = m_capacity / m_shrink_at_denom;
        m_grow_threashold = (m_capacity + 1) / m_grow_at_denom;
    }

    void start_resize(size_t new_capacity)
    {
        new_capacity = std::max(new_capacity, static_cast<size_t>(1)); // enforces min size of 1
        m_next_capacity = new_capacity;
        m_next = new T[m_next_capacity];
        m_left = 0;
    }

    void finish_resize()
    {
        if (m_capacity * sizeof(T) >= k_async_free_bytes)
        {
            background_freer<T>::instance().free_later(m_cur);
        }
        else
        {
            delete[] m_cur;
        }

        m_cur = m_next;
        m_next = nullptr;
        m_capacity = m_next_capacity;
        m_left = 0;
        m_next_capacity = 0;
        update_threasholds();
    }

    void step_resize()
    {
        if (m_next == nullptr)
        {
            return;
        }
        size_t n{std::min<size_t>(m_moves_per_op, m_size - m_left)};
        std::copy_n(m_cur + m_left, n, m_next + m_left);
        m_left += n;

        if (m_left == m_size)
        {
            finish_resize();
        }
    }

    void maybe_start_shrink()
    {
        if (m_next == nullptr && (m_size <= m_shrink_threashold))
        {
            start_resize(m_capacity / m_resize_factor);
        }
    }

    void maybe_start_grow()
    {
        if (m_next == nullptr && (m_size >= m_grow_threashold))
        {
            start_resize(m_capacity * m_resize_factor);
        }
    }
};
