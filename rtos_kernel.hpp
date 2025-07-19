#ifndef RTOS_KERNEL_HPP
#define RTOS_KERNEL_HPP

#include <iostream>
#include <vector>
#include <queue>      // For scheduling queue
#include <functional> // For std::function
#include <string>     // For task names
#include <list>       // For waiting queues in semaphores
#include <algorithm>  // For std::sort

// Task states
enum TaskState {
    READY,    // Ready to run
    RUNNING,  // Currently executing
    BLOCKED,  // Blocked (e.g., waiting for semaphore)
    DELAYED,  // Temporarily delayed (waiting for specific tick)
    SUSPENDED // Temporarily suspended (not fully implemented yet)
};

// --- Task Control Block (TCB) Definition ---
// Structure to hold information about each task
struct TCB {
    int id;                               // Unique task ID
    std::string name;                     // Task name
    TaskState state;                      // Current task state
    int priority;                         // Task priority (higher value = higher priority)
    std::function<void()> task_function;  // Function to be executed by the task
    
    // For semaphore implementation: points to the semaphore this task is waiting on
    int waiting_on_semaphore_id; 

    // For delay implementation: tick count until the task should be unblocked
    unsigned long delay_until_tick; 

    // Constructor for TCB
    TCB(int _id, const std::string& _name, int _priority, std::function<void()> func)
        : id(_id), name(_name), state(READY), priority(_priority), task_function(func),
          waiting_on_semaphore_id(-1), delay_until_tick(0) {} 
};

// --- Semaphore Definition ---
// Structure to represent a basic counting semaphore
struct Semaphore {
    int id;                // Unique semaphore ID
    std::string name;      // Semaphore name
    int count;             // Current value of the semaphore
    std::list<TCB*> waiting_queue; // List of tasks waiting for this semaphore (FIFO)

    // Constructor for Semaphore
    Semaphore(int _id, const std::string& _name, int initial_count)
        : id(_id), name(_name), count(initial_count) {}
};

// --- RTOS Kernel Class Definition ---
class RTOSKernel {
public:
    RTOSKernel();
    ~RTOSKernel();

    // Task management
    void createTask(const std::string& name, int priority, std::function<void()> task_func);
    void delay(unsigned long ticks); // Delay current task for specified ticks

    // Semaphore management
    int createSemaphore(const std::string& name, int initial_count); // Returns semaphore ID
    void semaphoreWait(int semaphore_id);   // Task requests to acquire semaphore
    void semaphoreSignal(int semaphore_id); // Task releases semaphore

    // RTOS simulation control
    void startScheduler();

    // Get the currently running task (useful for debugging/logging from task functions)
    TCB* getCurrentTask() const { return current_task; }

    // Get current system tick
    unsigned long getCurrentTick() const { return current_tick; }

protected: 
    std::vector<TCB*> tasks;        // Vector to store all TCBs
    TCB* current_task;              // Pointer to the currently executing task
    int next_task_id;               // Next available task ID for tasks
    unsigned long current_tick;     // Current system tick count

    // Ready queue for tasks (conceptually a priority queue based on current sort)
    std::vector<TCB*> ready_queue; 
    // Queue for tasks currently in DELAYED state
    std::vector<TCB*> delayed_queue;

    // Semaphore management
    std::vector<Semaphore*> semaphores; // Vector to store all semaphores
    int next_semaphore_id;               // Next available semaphore ID

    // Context switch simulation (conceptual)
    void contextSwitch(TCB* next_task);

    // Scheduler logic (selects the next task to run)
    void scheduler();
    
    // Helper to log messages
    void log(const std::string& message);

    // Helper to re-sort the ready queue after changes
    void sortReadyQueue();

    // Handle tick increment and unblocking delayed tasks
    void handleTick();
};

#endif // RTOS_KERNEL_HPP