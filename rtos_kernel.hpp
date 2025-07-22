#ifndef RTOS_KERNEL_HPP
#define RTOS_KERNEL_HPP

#include <iostream>
#include <vector>
#include <queue>      // For scheduling queue and MessageQueue data storage
#include <functional> // For std::function
#include <string>     // For task names
#include <list>       // For waiting queues in semaphores/mutexes/message queues
#include <algorithm>  // For std::sort

// Forward declaration to avoid circular dependency
class RTOSKernel; 

// Task states
enum TaskState {
    READY,    // Ready to run
    RUNNING,  // Currently executing
    BLOCKED,  // Blocked (e.g., waiting for semaphore/mutex/message)
    DELAYED,  // Temporarily delayed (waiting for specific tick)
    SUSPENDED // Temporarily suspended (not fully implemented yet)
};

// --- Task Control Block (TCB) Class Definition ---
// Represents a task within the RTOS
class TCB {
public:
    int id;                               // Unique task ID
    std::string name;                     // Task name
    TaskState state;                      // Current task state
    int priority;                         // Task priority (higher value = higher priority)
    std::function<void()> task_function;  // Function to be executed by the task
    
    // For synchronization primitives
    int waiting_on_semaphore_id; 
    int waiting_on_mutex_id;
    int owner_mutex_id; 
    int waiting_on_queue_id; // For message queue

    // For delay implementation
    unsigned long delay_until_tick; 

    // Pointer to the kernel instance to allow tasks to call kernel services (e.g., delay, semaphoreWait)
    RTOSKernel* kernel_ptr; 

    // Constructor
    TCB(int _id, const std::string& _name, int _priority, std::function<void()> func, RTOSKernel* _kernel_ptr)
        : id(_id), name(_name), state(READY), priority(_priority), task_function(func),
          waiting_on_semaphore_id(-1), waiting_on_mutex_id(-1), owner_mutex_id(-1), 
          waiting_on_queue_id(-1), delay_until_tick(0), kernel_ptr(_kernel_ptr) {}

    // Method to execute the task's function
    void execute() {
        if (task_function) {
            task_function();
        }
    }
};

// --- Semaphore Class Definition ---
// Represents a basic counting semaphore for task synchronization
class Semaphore {
public:
    int id;                // Unique semaphore ID
    std::string name;      // Semaphore name
    int count;             // Current value of the semaphore
    std::list<TCB*> waiting_queue; // List of tasks waiting for this semaphore (FIFO)

    // Constructor
    Semaphore(int _id, const std::string& _name, int initial_count)
        : id(_id), name(_name), count(initial_count) {}

    // Acquire the semaphore (decrement count, block if 0)
    bool acquire(TCB* task); // Returns true if acquired, false if blocked

    // Release the semaphore (increment count, unblock a waiting task if any)
    void release();
};

// --- Mutex Class Definition ---
// Represents a basic binary mutex for mutual exclusion
class Mutex {
public:
    int id;                 // Unique mutex ID
    std::string name;       // Mutex name
    TCB* owner;             // Pointer to the TCB that currently owns the mutex (nullptr if free)
    std::list<TCB*> waiting_queue; // List of tasks waiting for this mutex (FIFO)

    // Constructor
    Mutex(int _id, const std::string& _name)
        : id(_id), name(_name), owner(nullptr) {}

    // Acquire the mutex (lock)
    bool lock(TCB* task); // Returns true if locked, false if blocked

    // Release the mutex (unlock)
    void unlock(TCB* task);
};

// --- MessageQueue Class Definition ---
// Represents a basic message queue for inter-task communication
class MessageQueue {
public:
    int id;                  // Unique message queue ID
    std::string name;        // Message queue name
    std::queue<int> messages; // Queue to store integer messages
    size_t max_size;         // Maximum number of messages the queue can hold
    std::list<TCB*> sender_waiting_queue; // Tasks blocked trying to send to full queue
    std::list<TCB*> receiver_waiting_queue; // Tasks blocked trying to receive from empty queue

    // Constructor
    MessageQueue(int _id, const std::string& _name, size_t _max_size)
        : id(_id), name(_name), max_size(_max_size) {}

    // Send a message to the queue
    bool send(TCB* task, int message); // Returns true if sent, false if blocked

    // Receive a message from the queue
    bool receive(TCB* task, int& out_message); // Returns true if received, false if blocked
};


// --- RTOS Kernel Class Definition ---
class RTOSKernel {
public:
    RTOSKernel();
    ~RTOSKernel();

    // Task management
    void createTask(const std::string& name, int priority, std::function<void()> task_func);
    void delay(unsigned long ticks); // Delay current task for specified ticks

    // Synchronization primitives
    int createSemaphore(const std::string& name, int initial_count); // Returns semaphore ID
    void semaphoreWait(int semaphore_id);   // Task requests to acquire semaphore
    void semaphoreSignal(int semaphore_id); // Task releases semaphore

    int createMutex(const std::string& name); // Returns mutex ID
    void mutexLock(int mutex_id);           // Task requests to lock mutex
    void mutexUnlock(int mutex_id);         // Task releases mutex

    // Message Queue management
    int createMessageQueue(const std::string& name, size_t max_size); // Returns queue ID
    void messageQueueSend(int queue_id, int message); // Task sends message to queue
    bool messageQueueReceive(int queue_id, int& out_message); // Task receives message from queue

    // RTOS simulation control
    void startScheduler();

    // Get the currently running task (useful for debugging/logging from task functions)
    TCB* getCurrentTask() const { return current_task; }

    // Get current system tick
    unsigned long getCurrentTick() const { return current_tick; }

protected: 
    std::vector<TCB*> all_tasks;    // Vector to store all TCB objects
    TCB* current_task;              // Pointer to the currently executing task
    int next_task_id;               // Next available task ID for tasks
    unsigned long current_tick;     // Current system tick count

    // Queues for task states
    std::vector<TCB*> ready_queue; 
    std::vector<TCB*> delayed_queue;

    // Synchronization primitives storage
    std::vector<Semaphore*> all_semaphores; 
    int next_semaphore_id;               

    std::vector<Mutex*> all_mutexes;     
    int next_mutex_id;                   

    std::vector<MessageQueue*> all_message_queues; // Vector to store all message queues
    int next_message_queue_id;                   // Next available message queue ID


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

    // Helper to find objects by ID
    TCB* findTaskById(int id);
    Semaphore* findSemaphoreById(int id);
    Mutex* findMutexById(int id);
    MessageQueue* findMessageQueueById(int id);

    // Helper to move a task from BLOCKED/DELAYED to READY state
    void unblockTask(TCB* task);
};

#endif // RTOS_KERNEL_HPP