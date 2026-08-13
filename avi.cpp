#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <map>
#include <algorithm>
#include <functional>
#include <sstream>

using namespace std;
using namespace std::chrono;

// ==========================================
// 1. BACKEND SERVER ENTITY
// ==========================================
class Server {
public:
    string id;
    string address;
    atomic<bool> is_healthy{true};
    atomic<int> active_connections{0};
    atomic<uint64_t> total_requests{0};

    Server(string id, string addr) 
        : id(move(id)), address(move(addr)) {}

    void increment_connection() {
        active_connections++;
        total_requests++;
    }

    void decrement_connection() {
        if (active_connections > 0) {
            active_connections--;
        }
    }
};

// ==========================================
// 2. STRATEGY PATTERN FOR ROUTING ALGORITHMS
// ==========================================
class ILoadBalancerStrategy {
public:
    virtual ~ILoadBalancerStrategy() = default;
    virtual shared_ptr<Server> select_server(
        const vector<shared_ptr<Server>>& servers, 
        const string& request_key) = 0;
};

// --- A. ROUND-ROBIN STRATEGY ---
class RoundRobinStrategy : public ILoadBalancerStrategy {
private:
    atomic<size_t> index_{0};

public:
    shared_ptr<Server> select_server(
        const vector<shared_ptr<Server>>& servers, 
        const string& /* request_key */) override 
    {
        if (servers.empty()) return nullptr;

        size_t n = servers.size();
        for (size_t i = 0; i < n; ++i) {
            size_t curr = (index_++) % n;
            if (servers[curr]->is_healthy.load()) {
                return servers[curr];
            }
        }
        return nullptr;
    }
};

// --- B. LEAST-CONNECTIONS STRATEGY ---
class LeastConnectionsStrategy : public ILoadBalancerStrategy {
public:
    shared_ptr<Server> select_server(
        const vector<shared_ptr<Server>>& servers, 
        const string& /* request_key */) override 
    {
        shared_ptr<Server> best_server = nullptr;
        int min_conns = numeric_limits<int>::max();

        for (const auto& server : servers) {
            if (!server->is_healthy.load()) continue;

            int conns = server->active_connections.load();
            if (conns < min_conns) {
                min_conns = conns;
                best_server = server;
            }
        }
        return best_server;
    }
};

// --- C. CONSISTENT HASHING STRATEGY ---
class ConsistentHashingStrategy : public ILoadBalancerStrategy {
private:
    int vnodes_per_server_;
    map<uint32_t, shared_ptr<Server>> ring_;
    mutex ring_mutex_;

    uint32_t hash_fn(const string& key) {
        uint32_t hash = 2166136261u;
        for (char c : key) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

public:
    explicit ConsistentHashingStrategy(int vnodes = 3) : vnodes_per_server_(vnodes) {}

    void rebuild_ring(const vector<shared_ptr<Server>>& servers) {
        lock_guard<mutex> lock(ring_mutex_);
        ring_.clear();
        for (const auto& server : servers) {
            for (int i = 0; i < vnodes_per_server_; ++i) {
                string vnode_key = server->id + "#vnode_" + to_string(i);
                uint32_t hash = hash_fn(vnode_key);
                ring_[hash] = server;
            }
        }
    }

    shared_ptr<Server> select_server(
        const vector<shared_ptr<Server>>& servers, 
        const string& request_key) override 
    {
        lock_guard<mutex> lock(ring_mutex_);
        if (ring_.empty()) return nullptr;

        uint32_t hash = hash_fn(request_key);
        
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) {
            it = ring_.begin();
        }

        auto start_it = it;
        do {
            if (it->second->is_healthy.load()) {
                return it->second;
            }
            it++;
            if (it == ring_.end()) it = ring_.begin();
        } while (it != start_it);

        return nullptr;
    }
};

// ==========================================
// 3. LOAD BALANCER CORE ENGINE
// ==========================================
class LoadBalancer {
private:
    vector<shared_ptr<Server>> servers_;
    unique_ptr<ILoadBalancerStrategy> strategy_;
    mutex lb_mutex_;
    
    atomic<bool> running_{true};
    thread health_check_thread_;

    void run_health_checks() {
        while (running_.load()) {
            this_thread::sleep_for(milliseconds(800));
            lock_guard<mutex> lock(lb_mutex_);
            
            for (auto& server : servers_) {
                bool current_health = server->is_healthy.load();
                if (!current_health) {
                    cout << "  [HEALTH CHECKER] Warning: " << server->id 
                         << " (" << server->address << ") is DOWN!\n";
                }
            }
        }
    }

public:
    explicit LoadBalancer(unique_ptr<ILoadBalancerStrategy> strategy) 
        : strategy_(move(strategy)) 
    {
        health_check_thread_ = thread(&LoadBalancer::run_health_checks, this);
    }

    ~LoadBalancer() {
        running_ = false;
        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }
    }

    void add_server(const shared_ptr<Server>& server) {
        lock_guard<mutex> lock(lb_mutex_);
        servers_.push_back(server);
        
        auto ch = dynamic_cast<ConsistentHashingStrategy*>(strategy_.get());
        if (ch) {
            ch->rebuild_ring(servers_);
        }
    }

    void set_strategy(unique_ptr<ILoadBalancerStrategy> new_strategy) {
        lock_guard<mutex> lock(lb_mutex_);
        strategy_ = move(new_strategy);
        
        auto ch = dynamic_cast<ConsistentHashingStrategy*>(strategy_.get());
        if (ch) {
            ch->rebuild_ring(servers_);
        }
    }

    void route_request(const string& client_key, const function<void(Server&)>& mock_work) {
        shared_ptr<Server> target = nullptr;
        
        {
            lock_guard<mutex> lock(lb_mutex_);
            target = strategy_->select_server(servers_, client_key);
        }

        if (!target) {
            cout << "❌ [503 Service Unavailable] No healthy backend available for key: " 
                 << client_key << "\n";
            return;
        }

        target->increment_connection();
        cout << "  -> Routed request [" << client_key << "] to " << target->id 
             << " | Active Conns: " << target->active_connections.load() << "\n";

        mock_work(*target);
        target->decrement_connection();
    }

    void print_server_stats() {
        lock_guard<mutex> lock(lb_mutex_);
        cout << "\n=================== SERVER METRICS ===================\n";
        for (const auto& s : servers_) {
            cout << "Server [" << s->id << "] (" << s->address << ")"
                 << " | Status: " << (s->is_healthy.load() ? "HEALTHY" : "DOWN")
                 << " | Active Conns: " << s->active_connections.load()
                 << " | Total Served: " << s->total_requests.load() << "\n";
        }
        cout << "======================================================\n\n";
    }
};

// ==========================================
// 4. DEMONSTRATION HARNESS
// ==========================================
int main() {
    cout << "=== INITIALIZING C++ LOAD BALANCER ===\n\n";

    auto s1 = make_shared<Server>("Server-A", "192.168.1.10:8080");
    auto s2 = make_shared<Server>("Server-B", "192.168.1.11:8080");
    auto s3 = make_shared<Server>("Server-C", "192.168.1.12:8080");

    LoadBalancer lb(make_unique<RoundRobinStrategy>());
    lb.add_server(s1);
    lb.add_server(s2);
    lb.add_server(s3);

    auto dummy_work = [](Server& s) {
        this_thread::sleep_for(milliseconds(50));
    };

    cout << "--- DEMO 1: Round-Robin Routing ---\n";
    for (int i = 1; i <= 6; ++i) {
        lb.route_request("client_ip_" + to_string(i), dummy_work);
    }
    lb.print_server_stats();

    cout << "--- DEMO 2: Switching to Least-Connections ---\n";
    lb.set_strategy(make_unique<LeastConnectionsStrategy>());
    
    s1->active_connections = 10;
    cout << "[Simulated] Server-A spiked to 10 active connections.\n";

    for (int i = 1; i <= 4; ++i) {
        lb.route_request("client_ip_" + to_string(i), dummy_work);
    }
    s1->active_connections = 0; 
    lb.print_server_stats();

    cout << "--- DEMO 3: Switching to Consistent Hashing ---\n";
    lb.set_strategy(make_unique<ConsistentHashingStrategy>(3));

    cout << "Routing key 'User_Session_99' 3 times:\n";
    for (int i = 0; i < 3; ++i) {
        lb.route_request("User_Session_99", dummy_work);
    }
    cout << "Routing key 'User_Session_42' 2 times:\n";
    for (int i = 0; i < 2; ++i) {
        lb.route_request("User_Session_42", dummy_work);
    }

    cout << "\n--- DEMO 4: Automatic Failover (Server-B Crashes) ---\n";
    cout << "[SIMULATION] Crashing Server-B...\n";
    s2->is_healthy = false; 

    for (int i = 1; i <= 5; ++i) {
        lb.route_request("User_Session_" + to_string(i * 10), dummy_work);
    }

    lb.print_server_stats();

    cout << "=== DEMO COMPLETED SUCCESSFULLY ===\n";
    return 0;
}
