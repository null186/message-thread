# Message-Thread

Message-Thread is a C++ library for managing message queues and background threads, designed to simplify asynchronous message handling in multi-threaded applications.

## Features

- **Message Queue**: Provides a message queue implementation with priority scheduling based on message send time.
- **Background Threads**: Enables running message loops in background threads for processing queued messages.
- **Handler Interface**: Offers a handler interface for posting messages with callbacks to be executed at a specified delay.
- **Looper Class**: Provides a looper class for managing message queues and message loop execution.

## Usage

To use Message-Thread in your project, include the necessary headers and link against the library.

```cpp
#include "MessageThread.h"

int main() {
    // Create a message thread
    MessageThread message_thread;

    // Create a handler
    Handler handler(message_thread.GetLooper());

    // Post a message with a callback
    handler.Post([](){ std::cout << "Callback executed!" << std::endl; });

    // Quit the message thread
    message_thread.Braking();

    return 0;
}
