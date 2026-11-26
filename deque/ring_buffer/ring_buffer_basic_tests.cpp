#include "ring_buffer.h"
#include <iostream>

int main()
{
    ring_buffer<int> dq = ring_buffer<int>();

    for (int i{0}; i < 10; ++i)
    {
        dq.push_front(i);
    }

    while (!dq.empty())
    {
        std::cout << "front - " << dq.front() << '\n';
        std::cout << "back - " << dq.back() << '\n';
        dq.pop_back();
        dq.pop_front();
    }
}
