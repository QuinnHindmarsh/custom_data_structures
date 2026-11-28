// TODO(review, bug): no #pragma once (every other header has one). Including this header twice in one
//     translation unit fails with `redefinition of class O1vector<T>` (confirmed with g++ 13). Also,
//     parentheses in the path `O(1)_vector/O(1)_vector.h` need quoting in shells and some build tools; prefer
//     o1_vector.
//
// REVIEW KEY: every comment from this review starts with TODO(review, <tag>). Tags, most severe first: bug =
//     wrong behaviour today, safety = UB / exception-safety hole, latency = hot-path stall, perf = throughput
//     or memory, api = interface design, style = naming / readability. A NOTE(review) is something done well
//     that a reviewer would call out.
#include "../../shared_utility/freer.h"
#include <stdexcept>
#include <format>
#include <cassert>
#include <cstdint>   // uint8_t
#include <algorithm> // std::max
// TODO(review, style): <thread> is unused here (freer.h includes it itself). Unused includes slow every
//     translation unit that includes this header.
#include <thread>

/*

This is soley designed for cheap types -> optimisations have been made with the primary goal of optimising for int32_t. Optimisations for non cheap to copy objects like string would look very different. I may explore this in future
TODO:
interface
    - reserve
    - itterators
    - exmplace
    - properly handle copy/assignment operator


optimisations
    - copy in chunks, std::copy. copy 16 every 8 ops etc
    - better support none integral types - only store 1 copy of each element and check m_left to find what array it is in.
        - this would have the side effect of allowing writable [] operators

*/

// TODO(review, api): the header says this is only meant for cheap types. Encode that assumption in the type
//     system instead of a comment: `template <typename T> requires std::is_trivially_copyable_v<T>` (or a
//     static_assert). Then misuse with std::string is a compile error, not a silent slowdown, and it also
//     makes the copy-twice design in set() obviously safe.
//
// NOTE(review): good: grow at c/2 and shrink at c/8 gives hysteresis, so push/pop at a boundary can't thrash
//     between resizes. Worth one sentence in the header saying that's why the two denominators differ.
template <typename T>
class O1vector
{
public:
    // TODO(review, perf): `new T[n]` default-initialises n elements, which requires T to be
    //     default-constructible and, for non-trivial T, runs n constructors. Allocate raw storage
    //     (std::allocator<T>::allocate or ::operator new with alignment) and only construct on push, as
    //     ring_buffer does. Same for start_resize.
    //
    // TODO(review, safety): if background_freer<T>::instance() throws (std::thread creation can throw
    //     std::system_error), m_cur leaks because the destructor of a partly constructed object doesn't run.
    //     Call instance() before allocating, or hold the buffer in a std::unique_ptr<T[]>.
    explicit O1vector(size_t capacity = 1) : m_capacity{capacity >= 1 ? capacity : 1}, m_cur{new T[m_capacity]}
    {
        assert(m_grow_at_denom < m_shrink_at_denom); // shrink point c/8 must sit below grow point c/2
        update_threasholds();
        background_freer<T>::instance(); // shared freer to deallocate memory
    }

    // TODO(review, latency): the destructor frees both buffers synchronously, even huge ones. That is
    //     inconsistent with finish_resize(), which hands large blocks to the background freer: destroying a
    //     big O1vector on the hot path still blocks in munmap.
    ~O1vector()
    {
        delete[] m_cur;
        delete[] m_next;
    }
    // TODO(review, api): rule of five. Copy is deleted and no move is declared, so O1vector can't be moved at
    //     all: a named local can't be returned from a function (NRVO needs a move ctor), stored in a
    //     std::vector, or swapped. Add a noexcept move ctor and move assignment that steal m_cur/m_next and
    //     leave the source as a valid empty vector (which means allocating, or allowing m_cur == nullptr with
    //     capacity 0). A copy is easy to support too: copy [0, size) into one fresh buffer and drop any
    //     in-progress resize.
    O1vector(const O1vector &) = delete;
    O1vector &operator=(const O1vector &) = delete;

    // TODO(review, api): naming differs from both std and your own ring_buffer: std uses push_back / pop_back
    //     (void) / back(), ring_buffer has push_back / pop_back / pop_back_return, and this has push /
    //     pop_no_return / pop. Pick one convention across all your containers. Since JAQR will generate C++
    //     against these, matching std names lets generated code, templates and std algorithms treat every
    //     container the same way.
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

    // TODO(review, api): there is no mutable element access (no T& operator[], no T& at). That follows from
    //     keeping two copies during a resize, so it's a real design constraint, not an oversight. Document it
    //     in the header, because it's the first thing a user will try. Your TODO about storing each element
    //     once (and using m_left to pick the array) is the fix and also halves the copy work.
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

    // TODO(review, safety): the only overflow check is an assert, so in release builds correctness depends on
    //     an unstated relationship between the constants: the copy must finish before size reaches capacity.
    //     Starting at c/d with m moves per op, the copy finishes in time only if m * (d - 1) > d. With d = 2
    //     that means m >= 3, so m_moves_per_op = 2 would overflow. Add `static_assert(m_moves_per_op *
    //     (m_grow_at_denom - 1) > m_grow_at_denom)` next to the constants so a future tweak can't silently
    //     break it.
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

    // TODO(review, api): size(), capacity(), next_capacity() and not_empty() should be [[nodiscard]] and
    //     noexcept.
    size_t size() const
    {
        return m_size;
    }

    // TODO(review, api): std and generic code (std::ranges::empty, templates, JAQR codegen) expect empty().
    //     Keep not_empty() as a helper if you like it, but provide empty() too; ring_buffer already has it.
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

    // TODO(review, safety): unchecked, which is fine for operator[], but add `assert(i < m_size)` so debug
    //     and test builds catch out-of-range reads. It costs nothing in release.
    const T &operator[](std::size_t i) const { return m_cur[i]; }

private:
    // paramater values
    // TODO(review, perf): m_resize_factor is a non-static member but never changes. Make it static constexpr:
    //     it saves 8 bytes per object and a load, and lets the compiler turn *2 and /2 into shifts.
    //
    // TODO(review, perf): the object is 9 words, 72 bytes, so it spans two cache lines. push touches m_size,
    //     m_cur, m_next, m_left, m_grow_threashold and m_capacity (assert). Put those first so the common
    //     path reads one line, and the cold fields (m_next_capacity, m_shrink_threashold) after.
    size_t m_resize_factor{2};

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
    size_t m_shrink_threashold{0};
    size_t m_grow_threashold{0};

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
    // TODO(review, perf): [[noreturn]] alone doesn't stop the compiler inlining these reporters (and all of
    //     std::format) into every caller. Add [[gnu::cold]] and [[gnu::noinline]] (MSVC:
    //     __declspec(noinline)) so the hot functions stay small. The check-then-call-cold-reporter split you
    //     already have is the right shape.
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

    // TODO(review, latency): this allocation runs inside push/pop. For a large buffer it is an mmap syscall,
    //     and the new pages are then faulted in one 4 KB page at a time as step_resize copies into them:
    //     about one page fault per 1024 ints copied. So the worst case is O(1) in element moves but still has
    //     periodic microsecond page-fault spikes. Options: pre-fault a few pages ahead per op (like
    //     ring_buffer's touch_pages, but incremental), or keep the old buffer after a shrink and reuse it on
    //     the next grow so alloc/free pairs don't repeat.
    //
    // TODO(review, safety): m_next_capacity is written before `new` succeeds. If new throws, next_capacity()
    //     reports a resize that isn't happening. Assign it after the allocation.
    void start_resize(size_t new_capacity)
    {
        new_capacity = std::max(new_capacity, static_cast<size_t>(1)); // enforces min size of 1
        m_next_capacity = new_capacity;
        m_next = new T[m_next_capacity];
        m_left = 0;
    }

    // TODO(review, latency): free_later() runs on the push/pop path. It takes a mutex, may grow its
    //     std::vector under that mutex (an allocation), and notify_one() is often a futex syscall. Any of
    //     these can stall for microseconds, and every O1vector<T> in the process contends on one lock. In
    //     trading code the hot path should never lock or syscall. Use a preallocated single-producer queue of
    //     pointers that the freer drains, or avoid the free entirely by reusing buffers (see start_resize).
    //
    // TODO(review, safety): the freer is a function-local static. Its lifetime ends during static
    //     destruction, so a static or global O1vector whose resize finishes during shutdown (e.g. in another
    //     static's destructor) would call free_later on a destroyed object.
    //
    // NOTE(review): good: the 256 KB threshold roughly matches glibc's mmap threshold (128 KB by default), so
    //     small blocks are freed inline where free() is cheap, and only big munmap-backed blocks go to the
    //     background thread. Measure it on Windows too: the heap there behaves differently.
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

    // TODO(review, perf): 4 ints per op is 16 bytes, a quarter of a cache line. Copying a full line (16 ints)
    //     costs about the same memory traffic, finishes the resize 4x sooner and shrinks the window where two
    //     arrays are live. Also, std::copy_n with a runtime count of 0 to 4 usually becomes a small loop.
    //     When m_size - m_left >= m_moves_per_op, a fixed-size copy compiles to one or two vector moves.
    //     Benchmark both: your TODO about copying 16 every 8 ops is the same idea.
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
