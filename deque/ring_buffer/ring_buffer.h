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

// REVIEW KEY: every comment from this review starts with TODO(review, <tag>). Tags, most severe first: bug =
//     wrong behaviour today, safety = UB / exception-safety hole, latency = hot-path stall, perf = throughput
//     or memory, api = interface design, style = naming / readability. A NOTE(review) is something done well
//     that a reviewer would call out.
/*
TODO:
optimisations
- thread to allocate in advanced when it is nearing capacity?
- thread to then dealloc after?

expansions
- itterators
- .at
*/
// TODO(review, safety): grow() uses std::uninitialized_move. If T's move ctor throws halfway, the elements
//     already moved out are lost (the source objects are moved-from), and new_data leaks. std::vector avoids
//     this with std::move_if_noexcept. For a low-latency container the usual answer is simpler: require it,
//     e.g. `template <typename T> requires std::is_nothrow_move_constructible_v<T>`, so the problem can't
//     compile.
//
// TODO(review, api): beyond the move ops, .at() and emplace already being added in another PR, a reviewer
//     will still ask for clear(), capacity(), iterators (begin/end so range-for and <algorithm> work), and an
//     Allocator template parameter. The allocator matters in trading: you often want every container to draw
//     from a pre-faulted arena set up at startup, and std::allocator<T> is hard-coded today.
//
// NOTE(review): good: head/tail as free-running unsigned counters that are only masked on access. size() is
//     one subtraction, full vs empty needs no extra flag, and unsigned wrap-around makes push_front from 0
//     correct. This is the textbook way to do it.
template <std::move_constructible T>
class ring_buffer
{
    /* Out of bounds actions are purposefully undefined behaviour, to match std::deque*/
public:
    // TODO(review, api): `ring_buffer<int> rb{5}` picks the initializer_list ctor (one element, 5), while
    //     `rb(5)` means capacity 8. Same trap as std::vector, and reviewers flag it on custom containers.
    //     Either document it here or replace the size ctor with a named factory such as
    //     `ring_buffer::with_capacity(n)`.
    // & m_mask only works if capacity is a power of 2, so round up (0 becomes 1)
    explicit ring_buffer(size_t inital_capacity = 2) : m_capacitty(std::bit_ceil(inital_capacity)), m_data{m_alloc.allocate(m_capacitty)}, m_mask{m_capacitty - 1} {}

    ring_buffer(std::initializer_list<T> list) : ring_buffer(list.size())
    {
        // TODO(review, perf): touch_pages is redundant here: the memcpy / uninitialized_copy below writes the
        //     same pages front-to-back immediately after.
        touch_pages(m_capacitty, m_data);

        // TODO(review, safety): m_tail is set BEFORE the elements exist. The delegating ctor above has
        //     already finished, so the object counts as constructed: if a copy below throws, ~ring_buffer
        //     runs and std::destroy_at's list.size() slots, some of which were never constructed (UB). Set
        //     m_tail after the copy succeeds.
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

    // TODO(review, safety): m_data is allocated in the body, and if an element copy throws in the second
    //     uninitialized_copy, the objects from the first copy are never destroyed and m_data leaks (a ctor
    //     that throws never runs its own destructor). Delegate to `ring_buffer(source.m_capacitty)` first so
    //     the destructor owns the allocation, then advance m_tail per constructed element (or use a scope
    //     guard).
    //
    // TODO(review, perf): copies the source's capacity, not its size. A buffer that grew to 1M elements and
    //     drained to 3 makes a 1M-slot copy, and touch_pages then faults in every page of it (about 4 MB of
    //     page faults for 3 ints). Allocate std::bit_ceil(source.size()) unless you deliberately want the
    //     copy to keep headroom; either way, say which in a comment.
    //
    // TODO(review, bug): rule of five. Declaring this copy ctor suppresses the implicit moves, so `auto b =
    //     std::move(a)` deep-copies. Being fixed in the move-semantics PR; when merging, make sure the move
    //     ctor and move assignment are noexcept, or std::vector<ring_buffer<T>> will still copy on
    //     reallocation.
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

    // TODO(review, perf): swap should be noexcept, and needs a friend `swap(ring_buffer&, ring_buffer&)
    //     noexcept` so ADL finds it. Today std::swap(a, b) is three moves, and with no move ctor each of
    //     those is a full deep copy.
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

    // TODO(review, bug): aliasing use-after-free when the buffer is full. In `rb.push_back(rb.front())` (or
    //     push_front), grow() moves and destroys the old elements and frees m_data, then construct_at reads
    //     the now-dangling reference in args. Confirmed under ASan with std::string: heap-use-after-free.
    //     Same bug class as the open_map insert one. Fix the way std::vector does: in the grow path,
    //     construct the new element at its final slot in the NEW buffer first, then move the old elements
    //     across, then free the old buffer. Applies to emplace_back below too.
    template <typename... Args>
    void emplace_front(Args &&...args)
    {
        if (size() == m_capacitty) [[unlikely]]
        {
            grow(m_capacitty * resize_factor);
        }
        // TODO(review, safety): m_head is decremented before the element exists. If T's constructor throws,
        //     the buffer now claims an extra element at the front and the destructor will destroy an object
        //     that was never constructed. Construct at `(m_head - 1) & m_mask` first, then decrement.
        //     emplace_back below already does this in the right order.
        --m_head;
        std::construct_at(m_data + (m_head & m_mask), std::forward<Args>(args)...);
    }

    // TODO(review, safety): out-of-bounds is documented UB to match std::deque, which is fine, but std
    //     implementations still assert in debug builds (_GLIBCXX_ASSERTIONS, MSVC iterator debugging). Add
    //     `assert(!empty())` to pop_*, front(), back() and an index assert to operator[]. Zero cost in
    //     release, and it turns a silent corruption in your tests into an immediate failure.
    //
    // TODO(review, api): pop_front, pop_back, size, empty, operator[], front and back can all be noexcept
    //     (pops when T's destructor is noexcept, which is the default). noexcept is part of the API contract
    //     and lets callers such as std::move_if_noexcept and generic code pick faster paths.
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

    // TODO(review, api): [[nodiscard]] and noexcept on size() and empty(). `rb.empty();` as a statement
    //     (meaning clear()) is a classic bug that [[nodiscard]] catches.
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
    // TODO(review, perf): std::allocator is an empty type, but as a normal member it still occupies a byte
    //     padded to 8: sizeof(ring_buffer<int>) is 48. Mark it [[no_unique_address]] and drop m_capacitty (it
    //     is always m_mask + 1) to get 32 bytes, half a cache line, with the hot fields (m_data, m_mask,
    //     m_head, m_tail) together.
    //
    // TODO(review, style): typos in identifiers: m_capacitty, inital_capacity. Reviewers do flag these:
    //     misspelled names are hard to grep for and spread through call sites.
    std::allocator<T> m_alloc{};
    size_t m_capacitty;

    static constexpr size_t resize_factor = 2; // resize factor being a power of 2, allows for & instead of %

    T *m_data;
    size_t m_mask;
    size_t m_head{0}; // index of first element
    size_t m_tail{0}; // index of after last element

    // TODO(review, perf): the 4096 page size is hard-coded. Low-latency systems often run on 2 MB huge pages
    //     (fewer TLB misses), where this loop does 512x more writes than needed; query the page size once
    //     (sysconf / GetSystemInfo) or make it a constant you can override. It also touches the whole
    //     capacity on every grow, although the move/memcpy already writes the first n elements front-to-back;
    //     only [n, capacity) needs pre-faulting. It doesn't use any members, so make it static.
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

    // NOTE(review): good: keeping grow() out of line with noinline keeps the push fast path small. Adding
    //     [[gnu::cold]] as well moves it to a cold text section so it doesn't share instruction cache lines
    //     with hot code.
    //
    // TODO(review, latency): growth doubles inside push on the caller's thread: an allocation (an mmap
    //     syscall for big buffers), touch_pages faulting every page, then moving every element. That is one
    //     large latency spike per doubling. Trading code avoids it with reserve() at startup; if you want it
    //     handled in the container, the incremental-resize idea from O1vector applies here too.
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
