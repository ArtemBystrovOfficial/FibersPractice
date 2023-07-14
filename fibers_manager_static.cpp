#include <boost/fiber/fiber.hpp>
#include <iostream>
#include <vector>
#include <memory>
#include <functional>

#define INIT_CO ctx::fiber&& f2

#define CO_EXIT(SIG_PACK_NAME) \
* SIG_PACK_NAME.killed_sig = true; \
* SIG_PACK_NAME.exited_sig = true; \
f2 = std::move(f2).resume(); \
return std::move(f2);

#define CO_RETURN(SIG_PACK_NAME) \
*SIG_PACK_NAME.exited_sig = true; \
f2 = std::move(f2).resume(); \
return std::move(f2);

#define CO_YIELD \
f2 = std::move(f2).resume();

namespace ctx = boost::context;

using Job = std::function< ctx::fiber && (ctx::fiber&&) >;
using KilledSignal = std::shared_ptr<bool>;
using ExitedSignal = std::shared_ptr<bool>;

struct SignalPack {
    KilledSignal killed_sig{ std::make_shared<bool>(false) };
    ExitedSignal exited_sig{ std::make_shared<bool>(false) };
};

struct Task {
    Task(Job jb, KilledSignal ks, ExitedSignal es,int j_index)
    : worker(jb),
      killed_signal(ks),
      exited_signal(es),
      job_index(j_index) {
        *killed_signal = false;
        *exited_signal = false;
    }

    Task&& operator=(Task&& task) noexcept {
        job_index = task.job_index;
        worker = std::move(task.worker);
        killed_signal = std::move(task.killed_signal);
        exited_signal = std::move(task.exited_signal);
        return std::move(*this);
    }

    Task(Task&& other) noexcept 
      : worker(std::move(other.worker)),
      killed_signal(std::move(other.killed_signal)),
      exited_signal(std::move(other.exited_signal)),
      job_index(other.job_index) {}
    
    int job_index;
    KilledSignal killed_signal;
    ExitedSignal exited_signal;
    ctx::fiber worker;
};



class SerivceManager {
public:
    SerivceManager(): m_count_killed(0) {}

    void createService(Job job, SignalPack signal_pack) {
        m_jobs.push_back(job);
        m_tasks.push_back(Task(job, signal_pack.killed_sig, signal_pack.exited_sig, m_jobs.size()-1));
    }

    void run() {
        m_killed_tasks.resize(m_tasks.size(), false);
        while (m_count_killed != m_tasks.size()) {
            for (int i = 0; i < m_tasks.size(); ++i) {
                if (m_killed_tasks[i])
                    continue;
                auto& task = m_tasks[i];
                if (*(task.exited_signal)) {
                    if (!(*task.killed_signal))
                        m_tasks[i] = Task(m_jobs[task.job_index], task.killed_signal, task.exited_signal, task.job_index);
                    else {
                        ++m_count_killed;
                        m_killed_tasks[i] = true;
                    }
                } else 
                    task.worker = std::move(task.worker).resume(); 
            }
        }
    }

private:
    int m_count_killed;
    std::vector<bool> m_killed_tasks;
    std::vector<Task> m_tasks;
    std::vector<Job> m_jobs;
};

class ClientExample {
public:

    ClientExample() {
        //asyns reciver ~ 2500 / 1s stack switch
        SignalPack sig_reciver;
        manager.createService([this, sig_reciver](INIT_CO) {
            if (m_count_reciver % 2500 == 0)  // check if bytes available for recive
                std::cout << "Data recived\n";
            CO_YIELD
                ++m_count_reciver;
            CO_RETURN(sig_reciver)
        }, sig_reciver);

        //asyns sender ~ 1000 / 1s stack switch
        SignalPack sig_sender;
        manager.createService([this, sig_sender](INIT_CO) {
            if (m_count_sender % 1000 == 0) { // check if bytes available for send
                std::cout << "Data sended\n";
                CO_EXIT(sig_sender)
            }
            CO_YIELD
                ++m_count_sender;
            CO_RETURN(sig_sender)
        }, sig_sender);
    }

    void exec() { manager.run(); }

private:
    SerivceManager manager;
    int m_count_reciver = 1;
    int m_count_sender = 1;
};


int main() {
    ClientExample client;
    client.exec();
}