#ifndef RTOS_KERNEL_HPP
#define RTOS_KERNEL_HPP

#include <iostream>
#include <vector>
#include <queue>      // For scheduling queue
#include <functional> // For std::function
#include <string>     // For task names

// Task states
enum TaskState {
    READY,    // Ready to run
    RUNNING,  // Currently executing
    BLOCKED,  // Blocked (e.g., waiting for semaphore)
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

    // Constructor for TCB
    TCB(int _id, const std::string& _name, int _priority, std::function<void()> func)
        : id(_id), name(_name), state(READY), priority(_priority), task_function(func) {}
};

// --- RTOS Kernel Class Definition ---
class RTOSKernel {
public:
    RTOSKernel();
    ~RTOSKernel();

    // Create and add a new task to the kernel
    void createTask(const std::string& name, int priority, std::function<void()> task_func);

    // Start the RTOS scheduler simulation
    void startScheduler();

    // Get the currently running task
    TCB* getCurrentTask() const { return current_task; }

protected: // Changed to protected for potential inheritance/extension, though private is also fine.
    std::vector<TCB*> tasks;        // Vector to store all TCBs
    TCB* current_task;              // Pointer to the currently executing task
    int next_task_id;               // Next available task ID

    // Ready queue for tasks (conceptually a priority queue based on current sort)
    std::vector<TCB*> ready_queue; 

    // Context switch simulation (conceptual)
    void contextSwitch(TCB* next_task);

    // Scheduler logic (selects the next task to run)
    void scheduler();
    
    // Log messages for simulation tracing
    void log(const std::string& message);
};

#endif // RTOS_KERNEL_HPP