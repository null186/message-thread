#include <iostream>
#include <memory>

#include "thread.h"

int main() {
  auto thread = std::make_shared<mt::HandlerThread>("my-thread");
  thread->Start();
  auto handler = thread->handler();

  handler->PostDelay([]() { std::cout << "Hello world, Delay!" << std::endl; },
                     std::chrono::seconds(5));
  for (int i = 0; i < 3; ++i) {
    handler->Post(
        [=]() { std::cout << "Hello world! Num = " << i << std::endl; });
  }

  thread->Quit();

  return 0;
}
