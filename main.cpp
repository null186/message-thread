#include <iostream>

#include "thread.h"

int main() {
    MessageThread thread;
    auto looper = thread.GetLooper();
    Handler handler(looper);

    handler.Post([]() { std::cout << "Hello world, Delay!" << std::endl; },
                 std::chrono::seconds(100000));
    for (int i = 0; i < 1000000; ++i) {
        handler.Post([=]() { std::cout << "Hello world! Num = " << i << std::endl; });
    }

    // std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
