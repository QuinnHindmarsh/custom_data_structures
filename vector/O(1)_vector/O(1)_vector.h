#include <stdexcept>
#include <format>
#include <cassert>

[[noreturn]] void report_out_of_bounds_indexing(size_t idx)
{
    throw std::runtime_error(std::format("Invalid indexation: '{}' failed as this index does not exist.",
                                         idx));
}

/*
TODO:
    - add a .remove()? calling it O1vec then exposing O(n) operations are wierd
    - itterators?
    - For large T/T with 1 owner like unique_ptr I can store each element exactly once and check if it is in m_cur or m_next
    - improve error messages
    - handle_next is stupid, I should just have handle_grow/handle_shrink which should check if a shrink/grow operation needs to happen/initalise values if it does, it then calls a function which does what they currently do now - this is just duplication
    - more checks in place? currently a lot of my code just trusts the rest works, but at the same time im not exposing these areas so it might not be an issue
    - to add to interface
        - .begin()
        - .end()
        - .remove()
        - [] overload? after ch21?
        - .front()/.back() - pop without returning? does copy elision make this not matter? now does copy elsion works if the value isnt being stored?
        - does it make sense to have .reserve?
        - .shrink_to_fit?
        - .emplace_back()?


*/

template <typename T>
class O1vector
{
public:
    explicit O1vector(size_t capacity = 1) : m_capacity{capacity}, m_cur{new T[m_capacity]}
    {
        assert(m_shrink_at < m_grow_at);
    }

    T pop()
    { // python style pop, returns the element, can std::move/copy elision so shouldnt really slow it down anyways
        if (!not_empty())
        {
            report_out_of_bounds_indexing(0);
        }

        T temp = std::move(m_cur[m_size - 1]);
        --m_size;
        maybe_start_shrink();
        step_resize();
        return temp; // std::move() isnt used to allow copy ellision to apply.
    }

    void set(size_t idx, T element)
    {
        // forces you to set an an element < size, i.e. can only be used to modify an existing element. Prevents operations with the m_next array from needing to be handled

        if (idx >= m_size)
        {
            report_out_of_bounds_indexing(idx);
        }
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
        if (idx >= m_size)
        {
            // 0-based indexing
            report_out_of_bounds_indexing(idx);
        }

        return &m_cur[idx];
    }

    T at(size_t idx) const
    {

        return *at_ptr(idx);
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

private:
    // paramater values
    int m_resize_factor{2};

    float m_grow_at{1.0f / 2};   // when size is at c/m_grow_at start creating a bigger vector
    float m_shrink_at{1.0f / 8}; // when size is c/m_shrink_at, start creating a smaller vector
    int m_move_for_grow{2};      // number of elements to move when vector is in growing position
    int m_move_for_shrink{3};    // number of elements to move when vector is in in shrinking position
    int m_moves_per_op{};

    size_t m_size{0};
    size_t m_capacity;
    T *m_cur;
    T *m_next{nullptr}; // either an array of size c*2 or c/2 or nullptr if no resize is in progress
    size_t m_next_capacity{0};
    size_t m_left{0}; // all indexes < m_left exist in cur and m_next

    void start_resize(size_t new_capacity, int moves)
    {
        new_capacity = std::max(new_capacity, static_cast<size_t>(1)); // enforces min size of 1
        m_next_capacity = new_capacity;
        m_next = new T[m_next_capacity];
        m_left = 0;
        m_moves_per_op = moves;
    }

    void finish_resize()
    {
        delete[] m_cur;
        m_cur = m_next;
        m_next = nullptr;
        m_capacity = m_next_capacity;
        m_left = 0;
        m_next_capacity = 0;
    }

    void step_resize()
    {
        if (m_next == nullptr)
        {
            return;
        }

        for (int i{0}; i < m_moves_per_op; ++i)
        {
            if (m_left == m_size)
                break;

            m_next[m_left] = m_cur[m_left];
            ++m_left;
        }

        if (m_left == m_size)
        {
            finish_resize();
        }
    }

    void maybe_start_shrink()
    {
        if (m_next == nullptr && (m_size <= m_capacity * m_shrink_at))
        {
            start_resize(m_capacity / m_resize_factor, m_move_for_shrink);
        }
    }

    void maybe_start_grow()
    {
        if (m_next == nullptr && (m_size >= m_capacity * m_grow_at))
        {
            start_resize(m_capacity * m_resize_factor, m_move_for_grow);
        }
    }
};
