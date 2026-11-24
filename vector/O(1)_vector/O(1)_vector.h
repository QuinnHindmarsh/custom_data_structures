#include <stdexcept>
#include <format>

[[noreturn]] void report_unexpected_char(size_t idx)
{
    throw std::runtime_error(std::format("Invalid indexation: attempt to use .at('{}') failed as this index does not exist.",
                                         idx));
}

template <typename T>
class O1vector
{
private:
    // paramater values
    int m_resize_factor{2};
    float m_grow_at{1 / 2};   // when size is at c/m_grow_at start creating a bigger vector
    float m_shrink_at{1 / 8}; // when size is c/m_shrink_at, start creating a smaller vector
    int m_move_for_grow{1};   // number of additonal elements to move when vector is in growing position
    int m_move_for_shrink{2}; // number of additonal elements to move when vector is in in shrinking position

    size_t m_size{0};
    size_t m_capacity;
    T *m_cur;
    T *m_next{nullptr}; // either an array of size c*2 or c/2 or nullptr if no resize is in progress
    bool m_is_growing{false};
    bool m_is_shrinking{false}; //  not just !m_is_growing, a vector can neither be growing or shrinking.

public:
    explicit O1vector(int capacity = 1) : m_capacity{capacity}, m_next{new T[m_capacity]}
    {
    }

    void push(T element) {}

    T pop() // python style pop, returns the element
    {
    }

    T at(size_t idx)
    {
    }
    size_t size()
    {
        return m_size;
    }

    bool not_empty() // I think !vec.is_empty() is used a lot more then vec.is_empty, I think checking its not empty is cleaner

    {
        return m_size != 0;
    }
};
