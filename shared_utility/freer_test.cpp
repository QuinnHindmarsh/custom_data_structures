#include "freer.h"
#include <string>
#include <iostream>

int main()
{
    {
        auto &f = background_freer<int>::instance();
        for (int i = 0; i < 1000; ++i)
            f.free_later(new int[1'000'000]);
    } // destructor: drains queue, joins
    std::cout << "int: ok\n";

    {
    auto& f        = background_freer<std::string>::instance();
        for (int i = 0; i < 300; ++i)
            f.free_later(new std::string[1000]{}); // empty strings: tests delete[] runs fine
    }
    std::cout << "string: ok\n";
}
