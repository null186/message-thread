#include "thread.h"

int main() {
    MessageThread thread;
    auto looper = thread.GetLooper();
    Handler handler(looper);

    handler.Post([]() { printf("Hello world, Delay!\n"); }, std::chrono::seconds(5));
    for (int i = 0; i < 10; ++i) {
        handler.Post([=]() { printf("Hello world! Num = %d\n", i); });
    }

    thread.Braking();

    return 0;
}
