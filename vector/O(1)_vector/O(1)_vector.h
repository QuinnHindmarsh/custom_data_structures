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
    explicit O1vector(int capacity = 1) : m_capacity{capacity}, m_next{new T[m_capacity]}
    {
        assert(m_shrink_at < m_grow_at);
    }

    T pop()
    { // python style pop, returns the element, can std::move/copy elision so shouldnt really slow it down anyways
        assert(not_empty());

        T temp = std::move(m_cur[m_size - 1]);
        --m_size;
        handle_next();
        return temp; // std::move() isnt used to allow copy ellision to apply.
    }

    T set(size_t idx, T element)
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
        if (m_size + 1 >= m_capacity) // if attempting to push and array is full, swap to larger array before.
        {
            handle_next();
        }
        m_cur[m_size] = std::move(element);
        ++m_size;
        handle_next();
    }

    T at(size_t idx) const
    {

        if (idx >= m_size)
        {
            // 0-based indexing
            report_out_of_bounds_indexing(idx);
        }

        return m_cur[idx];
    }

    const T *at_ptr(size_t idx) // no garuntees this will be valid after any pop/push operation
    {
        if (idx >= m_size)
        {
            // 0-based indexing
            report_out_of_bounds_indexing(idx);
        }

        return &m_cur[idx];
    }

    size_t size()
    {
        return m_size;
    }

    bool not_empty() // I think !vec.is_empty() is used a lot more then vec.is_empty, I think checking its not empty is cleaner
    {
        return m_size != 0;
    }

private:
    // paramater values
    int m_resize_factor{2};

    float m_grow_at{1 / 2};   // when size is at c/m_grow_at start creating a bigger vector
    float m_shrink_at{1 / 8}; // when size is c/m_shrink_at, start creating a smaller vector
    int m_move_for_grow{2};   // number of elements to move when vector is in growing position
    int m_move_for_shrink{3}; // number of elements to move when vector is in in shrinking position

    size_t m_size{0};
    size_t m_capacity;
    T *m_cur;
    T *m_next{nullptr}; // either an array of size c*2 or c/2 or nullptr if no resize is in progress
    size_t m_next_capacity{0};
    size_t m_left{0}; // all indexes < m_left exist in cur and m_next

    void swap_to_next()
    {
        delete[] m_cur;
        m_cur = m_next;
        m_next = nullptr;
        m_capacity = m_next_capacity;
        m_left = 0;
        m_next_capacity = 0;
    }

    void handle_shrink()
    {
        assert(m_next != nullptr);

        for (int i{0}; i < m_move_for_shrink; ++i)
        {
            if (m_left == m_size)
                break;

            m_next[m_left] = m_cur[m_left];
            ++m_left;
        }
        if (m_left == m_size)
        {
            swap_to_next();
        }
    }
    // TODO: repeated logic
    void handle_grow() // TODO: maybe i dont need this? push should only ever need to handle grow, pop only ever needs handle shrink. And i can pass the next element into handle grow meaning i dont need to handle the wierd case of calling it before and after pushing
    {
        assert(m_next != nullptr);

        for (int i{0}; i < m_move_for_grow; ++i)
        {
            if (m_left == m_size)
                break;

            // TODO: if T is large this copy is expensive. where T is large I can have it so I move instead of copy.
            // however this has a trade off, when doing an index the element may exist in one of two arrays, if index heavy this might have some preformance issues, which is why I just want to copy cheap types
            m_next[m_left] = m_cur[m_left];
            ++m_left;
        }

        if (m_left == m_size)
        {
            swap_to_next();
        }
    }
    // TODO: name better?
    void handle_next()
    {
        if (m_size >= m_capacity * m_grow_at)
        {
            if (m_next == nullptr)
            {
                m_next_capacity = m_capacity * m_resize_factor;
                m_next = new T[m_next_capacity];
                m_left = 0;
            }

            handle_grow();
        }
        else if (m_size <= m_capacity * m_shrink_at)
        {
            if (m_next == nullptr)
            {
                m_next_capacity = m_capacity / m_resize_factor;
                m_next = new T[m_next_capacity];
                m_left = 0;
            }
            handle_shrink();
        }
        else
        {
            delete[] m_next;
            m_next = nullptr;
            m_next_capacity = 0;
            m_left = 0;
        }
    }
};
