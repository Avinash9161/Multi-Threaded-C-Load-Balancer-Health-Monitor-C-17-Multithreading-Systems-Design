ulti-Threaded C++ Load Balancer & Health Monitor
A high-performance, concurrent Layer 7 load balancer and health monitoring system built in C++17. Designed to demonstrate core systems programming principles, efficient thread synchronization, lock-free patterns, and real-time backend health tracking.

Key Features
Multi-Threaded Architecture: Utilizes a worker thread pool model to process incoming network connections concurrently with minimal overhead.

Health Monitoring: Background worker threads periodically perform active health checks (e.g., HTTP/TCP pings) on registered backend servers to maintain an active backend pool.

Load Balancing Algorithms:

Round Robin: Sequential request distribution across available backends.

Least Connections: Routes traffic dynamically to the backend with the lowest active workload.

Weighted Distribution: Support for backends with varying server capacities.

Graceful Degradation & Failover: Dynamically marks unhealthy nodes as down and re-routes requests seamlessly without interrupting existing traffic.

Modern C++17 Design: Leverages standard concurrency primitives (std::thread, std::mutex, std::condition_variable, std::atomic, std::shared_ptr) for clean and memory-safe design.

Architecture Overview
                          +-------------------------+
                          |     Incoming Client     |
                          +------------+------------+
                                       |
                                       v
                          +-------------------------+
                          |   Multi-Threaded        |
                          |   Load Balancer Router  |
                          +------------+------------+
                                       |
                +----------------------+----------------------+
                |                      |                      |
                v                      v                      v
        +---------------+      +---------------+      +---------------+
        | Backend Server|      | Backend Server|      | Backend Server|
        |    Node 1     |      |    Node 2     |      |    Node 3     |
        +---------------+      +---------------+      +---------------+
                ^                      ^                      ^
                |                      |                      |
                +----------------------+----------------------+
                                       |
                          +------------+------------+
                          |   Health Monitor Loop   |
                          |   (Periodic Worker)     |
                          +-------------------------+
Prerequisites
Ensure you have the following installed on your environment:

Compiler: GCC 7+, Clang 5+, or MSVC supporting C++17 standard.

Build System: CMake 3.14+ or make.

OS: Linux / macOS / WSL (Windows Subsystem for Linux).

Build & Installation
Clone the repository:

Bash
git clone https://github.com/Avinash9161/Multi-Threaded-C-Load-Balancer-Health-Monitor-C-17-Multithreading-Systems-Design.git
cd Multi-Threaded-C-Load-Balancer-Health-Monitor-C-17-Multithreading-Systems-Design
Create a build directory:

Bash
mkdir build && cd build
Configure and compile:

Bash
cmake ..
make -j$(nproc)
Usage
After compiling, run the generated binary with optional configuration flags or configuration files:

Bash
./load_balancer --port 8080 --config config.json
Example Output
Plaintext
[INFO] Starting Multi-Threaded Load Balancer on port 8080...
[INFO] Initialized Thread Pool with 8 threads.
[HEALTH] Checking backend 192.168.1.10:8081 ... [HEALTHY]
[HEALTH] Checking backend 192.168.1.11:8081 ... [HEALTHY]
[HEALTH] Checking backend 192.168.1.12:8081 ... [UNHEALTHY] -> Removed from active pool.
[ROUTER] Request routed to 192.168.1.10:8081 via Round Robin.
Project Structure
Plaintext
├── CMakeLists.txt
├── include/
│   ├── load_balancer.hpp      # Main balancing logic & request distribution
│   ├── health_checker.hpp     # Health monitor worker logic
│   ├── thread_pool.hpp        # Worker thread management
│   └── backend.hpp           # Backend node abstraction & metrics
├── src/
│   ├── load_balancer.cpp
│   ├── health_checker.cpp
│   ├── thread_pool.cpp
│   └── main.cpp               # Entry point
└── tests/                     # Unit & load tests
