#ifndef RTOS_KERNEL_HPP
#define RTOS_KERNEL_HPP

#include <iostream>
#include <vector>
#include <queue>      // For scheduling queue and message queue
#include <functional> // For std::function
#include <string>     // For task names
#include <list>       // For waiting queues in semaphores/mutexes/event flags
#include <algorithm>  // For std::sort

// Forward declaration to avoid circular dependency
class RTOSKernel; 

// Task states
enum TaskState {
    READY,    // Ready to run
    RUNNING,  // Currently executing
    BLOCKED,  // Blocked (e.g., waiting for semaphore/mutex/event flag/message)
    DELAYED,  // Temporarily delayed (waiting for specific tick)
    SUSPENDED // Temporarily suspended (not fully implemented yet)
};

// Event Flag Wait Modes
enum EventWaitMode {
    WAIT_ALL, // Task waits for all specified flags to be set
    WAIT_ANY  // Task waits for any of the specified flags to be set
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
    int waiting_on_event_flag_id;     // ID of the event flag this task is waiting on
    unsigned int event_flags_to_wait_for; // Bitmask of flags the task is waiting for
    EventWaitMode event_wait_mode;    // WAIT_ALL or WAIT_ANY
    int waiting_on_message_queue_id;  // ID of the message queue this task is waiting on

    // For delay implementation: tick count until the task should be unblocked
    unsigned long delay_until_tick; 
    
    // For periodic tasks
    bool is_periodic;                     // True if this is a periodic task
    unsigned long period_ticks;           // Period for periodic tasks
    unsigned long next_run_tick;          // Next scheduled run time for periodic tasks

    // Pointer to the kernel instance to allow tasks to call kernel services (e.g., delay, semaphoreWait)
    RTOSKernel* kernel_ptr; 

    // Constructor
    TCB(int _id, const std::string& _name, int _priority, std::function<void()> func, RTOSKernel* _kernel_ptr,
        bool _is_periodic = false, unsigned long _period_ticks = 0)
        : id(_id), name(_name), state(READY), priority(_priority), task_function(func),
          waiting_on_semaphore_id(-1), waiting_on_mutex_id(-1), owner_mutex_id(-1), 
          waiting_on_event_flag_id(-1), event_flags_to_wait_for(0), event_wait_mode(WAIT_ANY),
          waiting_on_message_queue_id(-1),
          delay_until_tick(0), 
          is_periodic(_is_periodic), period_ticks(_period_ticks), next_run_tick(0),
          kernel_ptr(_kernel_ptr) {}

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

// --- EventFlag Class Definition ---
// Represents a set of event flags for inter-task communication
class EventFlag {
public:
    int id;                 // Unique event flag ID
    std::string name;       // Event flag name
    unsigned int flags;     // Current state of the flags (bitmask)
    std::list<TCB*> waiting_queue; // List of tasks waiting for specific flags

    // Constructor
    EventFlag(int _id, const std::string& _name)
        : id(_id), name(_name), flags(0) {}

    // Set specified flags (OR operation)
    void set(unsigned int flags_to_set);

    // Clear specified flags (AND NOT operation)
    void clear(unsigned int flags_to_clear);

    // Check if a task's wait condition is met
    bool check_and_clear_flags(TCB* task); // Returns true if condition met and flags consumed
};

// --- MessageQueue Class Definition ---
// Represents a queue for inter-task message passing
class MessageQueue {
public:
    int id;                      // Unique message queue ID
    std::string name;            // Message queue name
    std::queue<std::string> queue_data; // Simple queue to store string messages
    size_t max_size;             // Maximum number of messages in the queue
    std::list<TCB*> send_waiting_queue; // Tasks waiting to send (queue is full)
    std::list<TCB*> receive_waiting_queue; // Tasks waiting to receive (queue is empty)

    // Constructor
    MessageQueue(int _id, const std::string& _name, size_t _max_size)
        : id(_id), name(_name), max_size(_max_size) {}

    // Send a message to the queue
    bool send(TCB* task, const std::string& message); // Returns true if sent, false if blocked

    // Receive a message from the queue
    bool receive(TCB* task, std::string& out_message); // Returns true if received, false if blocked
};

// --- SoftwareTimer Class Definition ---
// Represents a software timer that can trigger a callback or set an event flag
class SoftwareTimer {
public:
    int id;                               // Unique timer ID
    std::string name;                     // Timer name
    unsigned long period_ticks;           // Period of the timer (0 for one-shot)
    unsigned long expiry_tick;            // Tick at which the timer will expire
    bool is_running;                      // Is the timer currently active?
    bool is_periodic;                     // Is this a periodic timer?
    std::function<void()> callback_function; // Function to call when timer expires
    int event_flag_id_to_set;             // Event Flag ID to set on expiry (-1 if none)
    unsigned int event_flags_to_set;      // Flags to set on expiry (if event_flag_id_to_set is valid)
    RTOSKernel* kernel_ptr;               // Pointer to the kernel for service calls

    // Constructor for callback-based timer
    SoftwareTimer(int _id, const std::string& _name, unsigned long _period_ticks, 
                  std::function<void()> _callback, RTOSKernel* _kernel_ptr)
        : id(_id), name(_name), period_ticks(_period_ticks), expiry_tick(0), 
          is_running(false), is_periodic(_period_ticks > 0), 
          callback_function(_callback), 
          event_flag_id_to_set(-1), event_flags_to_set(0),
          kernel_ptr(_kernel_ptr) {}

    // Constructor for event flag-based timer
    SoftwareTimer(int _id, const std::string& _name, unsigned long _period_ticks, 
                  int _event_flag_id, unsigned int _event_flags_to_set, RTOSKernel* _kernel_ptr)
        : id(_id), name(_name), period_ticks(_period_ticks), expiry_tick(0), 
          is_running(false), is_periodic(_period_ticks > 0), 
          callback_function(nullptr), 
          event_flag_id_to_set(_event_flag_id), event_flags_to_set(_event_flags_to_set),
          kernel_ptr(_kernel_ptr) {}

    // Start or restart the timer
    void start(unsigned long current_tick) {
        expiry_tick = current_tick + period_ticks;
        is_running = true;
    }

    // Stop the timer
    void stop() {
        is_running = false;
    }

    // Check if timer has expired and execute action
    void check_and_expire(unsigned long current_tick);
};


// --- RTOS Kernel Class Definition ---
class RTOSKernel {
public:
    RTOSKernel();
    ~RTOSKernel();

    // Task management
    void createTask(const std::string& name, int priority, std::function<void()> task_func);
    // Overload for periodic tasks
    void createTask(const std::string& name, int priority, std::function<void()> task_func, unsigned long period_ticks);
    void delay(unsigned long ticks); // Delay current task for specified ticks

    // Synchronization primitives
    int createSemaphore(const std::string& name, int initial_count); // Returns semaphore ID
    void semaphoreWait(int semaphore_id);   // Task requests to acquire semaphore
    void semaphoreSignal(int semaphore_id); // Task releases semaphore

    int createMutex(const std::string& name); // Returns mutex ID
    void mutexLock(int mutex_id);           // Task requests to lock mutex
    void mutexUnlock(int mutex_id);         // Task releases mutex

    int createEventFlag(const std::string& name); // Returns event flag ID
    // Task waits for specific event flags. Returns true if flags were met and consumed.
    bool eventFlagWait(int event_flag_id, unsigned int flags_to_wait_for, EventWaitMode mode);
    void eventFlagSet(int event_flag_id, unsigned int flags_to_set);   // Set flags
    void eventFlagClear(int event_flag_id, unsigned int flags_to_clear); // Clear flags

    // Message Queue management
    int createMessageQueue(const std::string& name, size_t max_size); // Returns message queue ID
    bool messageQueueSend(int mq_id, const std::string& message); // Task sends a message
    bool messageQueueReceive(int mq_id, std::string& out_message); // Task receives a message

    // Software Timer management
    // Create a timer that calls a callback function
    int createTimer(const std::string& name, unsigned long period_ticks, std::function<void()> callback_func);
    // Create a timer that sets event flags
    int createTimer(const std::string& name, unsigned long period_ticks, int event_flag_id, unsigned int event_flags_to_set);
    void startTimer(int timer_id);
    void stopTimer(int timer_id);
    void resetTimer(int timer_id); // Resets timer to its initial state and restarts it

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

    // Synchronization primitive storage
    std::vector<Semaphore*> all_semaphores; 
    int next_semaphore_id;               

    std::vector<Mutex*> all_mutexes;     
    int next_mutex_id;                   

    std::vector<EventFlag*> all_event_flags;
    int next_event_flag_id;

    std::vector<MessageQueue*> all_message_queues;
    int next_message_queue_id;

    std::vector<SoftwareTimer*> all_timers; // Vector to store all SoftwareTimer objects
    int next_timer_id;

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
    // Handle periodic tasks
    void handlePeriodicTasks();
    // Handle software timers
    void handleSoftwareTimers();

    // Helper to find objects by ID
    TCB* findTaskById(int id);
    Semaphore* findSemaphoreById(int id);
    Mutex* findMutexById(int id);
    EventFlag* findEventFlagById(int id);
    MessageQueue* findMessageQueueById(int id);
    SoftwareTimer* findTimerById(int id);

    // Helper to move a task from BLOCKED/DELAYED to READY state
    void unblockTask(TCB* task);
    // Helper to check and unblock tasks waiting on an EventFlag
    void checkAndUnblockEventFlagWaiters(EventFlag* ef);
    // Helper to check and unblock tasks waiting on a MessageQueue
    void checkAndUnblockMessageQueueWaiters(MessageQueue* mq);
};

#endif // RTOS_KERNEL_HPP