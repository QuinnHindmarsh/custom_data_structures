#include <type_traits>
#include <utility>

/* This doesnt offer any advantages to std::unique_ptr, I just wanted to make it*/

template <typename T>
class UniquePtr
{
private:
    using elem = std::remove_extent_t<T>;

    elem *m_ptr;

public:
    explicit UniquePtr(elem *ptr = nullptr) : m_ptr{ptr}
    {
    }

    UniquePtr(const UniquePtr &other) = delete;

    UniquePtr(UniquePtr &&other) noexcept : m_ptr{
                                                std::exchange(other.m_ptr, nullptr)} {};
    ~UniquePtr()
    {
        reset();
    }

    UniquePtr &operator=(const UniquePtr &other) = delete;

    UniquePtr &operator=(UniquePtr &&other) noexcept
    {
        reset(other.release());
        return *this;
    }

    elem &operator*() const noexcept
    {
        return *m_ptr;
    }

    elem *operator->() const noexcept
    {
        return m_ptr;
    }

    explicit operator bool() const noexcept
    {
        return m_ptr != nullptr;
    }

    elem *get() const noexcept
    {
        return m_ptr;
    }

    [[nodiscard]] elem *release() noexcept
    {
        elem *ptr{m_ptr};
        m_ptr = nullptr;
        return ptr;
    }

    void reset(elem *other = nullptr) noexcept
    {

        elem *old{m_ptr};
        m_ptr = other;
        if constexpr (std::is_array_v<T>)
        {
            delete[] old;
        }

        else
        {
            delete old;
        }
    }
};
