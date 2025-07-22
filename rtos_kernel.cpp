#include "rtos_kernel.hpp"
#include <algorithm> // For std::sort, std::remove_if
#include <stdexcept> // For std::runtime_error

// --- Semaphore Class Implementation ---
bool Semaphore::acquire(TCB* task) {
    if (count > 0) {
        count--;
        return true; // Acquired
    } else {
        // Not available, block the task
        waiting_queue.push_back(task);
        return false; // Blocked
    }
}

void Semaphore::release() {
    count++;
    // The actual unblocking of a task is handled by the RTOSKernel.
}

// --- Mutex Class Implementation ---
bool Mutex::lock(TCB* task) {
    if (owner == nullptr) { // Mutex is free
        owner = task;
        task->owner_mutex_id = this->id; // Mark task as owner
        return true; // Acquired
    } else { // Mutex is held by another task
        waiting_queue.push_back(task);
        return false; // Blocked
    }
}

void Mutex::unlock(TCB* task) {
    if (owner != task) {
        // Error: Task trying to unlock a mutex it doesn't own
        std::cout << "[RTOS Log] ERROR: Task '" << task->name << "' (ID:" << task->id 
                  << ") tried to unlock mutex '" << name << "' (ID:" << id 
                  << ") but is not the owner (Owner: " << (owner ? owner->name : "None") << ")." << std::endl;
        return;
    }

    owner = nullptr; // Release ownership
    task->owner_mutex_id = -1; // Clear owner info in TCB

    // The actual unblocking of a task is handled by the RTOSKernel.
}

// --- MessageQueue Class Implementation ---
bool MessageQueue::send(TCB* task, int message) {
    if (messages.size() < max_size) {
        messages.push(message);
        return true; // Message sent
    } else {
        // Queue is full, block the sender
        sender_waiting_queue.push_back(task);
        return false; // Blocked
    }
}

bool MessageQueue::receive(TCB* task, int& out_message) {
    if (!messages.empty()) {
        out_message = messages.front();
        messages.pop();
        return true; // Message received
    } else {
        // Queue is empty, block the receiver
        receiver_waiting_queue.push_back(task);
        return false; // Blocked
    }
}


// --- RTOS Kernel Class Implementation ---
RTOSKernel::RTOSKernel() : current_task(nullptr), next_task_id(0), 
                           next_semaphore_id(0), next_mutex_id(0), 
                           next_message_queue_id(0), current_tick(0) {
    log("RTOS Kernel initialized.");
}

RTOSKernel::~RTOSKernel() {
    // Deallocate dynamically created TCB objects
    for (TCB* task : all_tasks) {
        delete task;
    }
    all_tasks.clear(); 

    // Deallocate dynamically created Semaphore objects
    for (Semaphore* sem : all_semaphores) {
        delete sem;
    }
    all_semaphores.clear(); 

    // Deallocate dynamically created Mutex objects
    for (Mutex* mtx : all_mutexes) {
        delete mtx;
    }
    all_mutexes.clear();

    // Deallocate dynamically created MessageQueue objects
    for (MessageQueue* mq : all_message_queues) {
        delete mq;
    }
    all_message_queues.clear();

    // Clear queues (pointers are already deleted)
    ready_queue.clear();
    delayed_queue.clear();

    log("RTOS Kernel terminated and resources freed.");
}

void RTOSKernel::log(const std::string& message) {
    std::cout << "[RTOS Log] " << message << std::endl;
}

// Helper to sort the ready queue
void RTOSKernel::sortReadyQueue() {
    std::sort(ready_queue.begin(), ready_queue.end(), [](TCB* a, TCB* b) {
        if (a->priority != b->priority) {
            return a->priority > b->priority; // Higher priority (larger number) comes first
        }
        return a->id < b->id; // If priorities are equal, sort by ID (creation order)
    });
}

// Helper to find a TCB by ID
TCB* RTOSKernel::findTaskById(int id) {
    for (TCB* task : all_tasks) {
        if (task->id == id) {
            return task;
        }
    }
    return nullptr;
}

// Helper to find a Semaphore by ID
Semaphore* RTOSKernel::findSemaphoreById(int id) {
    for (Semaphore* sem : all_semaphores) {
        if (sem->id == id) {
            return sem;
        }
    }
    return nullptr;
}

// Helper to find a Mutex by ID
Mutex* RTOSKernel::findMutexById(int id) {
    for (Mutex* mtx : all_mutexes) {
        if (mtx->id == id) {
            return mtx;
        }
    }
    return nullptr;
}

// Helper to find a MessageQueue by ID
MessageQueue* RTOSKernel::findMessageQueueById(int id) {
    for (MessageQueue* mq : all_message_queues) {
        if (mq->id == id) {
            return mq;
        }
    }
    return nullptr;
}

// Helper to move a task from BLOCKED/DELAYED to READY state
void RTOSKernel::unblockTask(TCB* task) {
    if (task->state == BLOCKED || task->state == DELAYED) {
        task->state = READY;
        task->waiting_on_semaphore_id = -1;
        task->waiting_on_mutex_id = -1;
        task->waiting_on_queue_id = -1; // Clear message queue wait info
        task->delay_until_tick = 0;
        ready_queue.push_back(task);
        log("Task '" + task->name + "' unblocked and moved to READY state.");
        sortReadyQueue();
    }
}

// Create and add a task to the kernel's task list and ready queue
void RTOSKernel::createTask(const std::string& name, int priority, std::function<void()> task_func) {
    TCB* new_tcb = new TCB(next_task_id++, name, priority, task_func, this); // Pass kernel pointer to TCB
    all_tasks.push_back(new_tcb);
    ready_queue.push_back(new_tcb);

    sortReadyQueue(); // Sort the ready queue after adding a new task

    log("Task created: ID=" + std::to_string(new_tcb->id) +
        ", Name='" + new_tcb->name +
        "', Priority=" + std::to_string(new_tcb->priority));
}

// Delay the current task for 'ticks' amount of time
void RTOSKernel::delay(unsigned long ticks) {
    if (current_task == nullptr) {
        log("ERROR: delay called outside of a task context.");
        return;
    }

    current_task->state = DELAYED;
    current_task->delay_until_tick = current_tick + ticks;
    delayed_queue.push_back(current_task);

    log("Task '" + current_task->name + "' delayed until tick " + std::to_string(current_task->delay_until_tick) + ".");

    // Remove from ready queue if it was there
    for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
        if (*it == current_task) {
            ready_queue.erase(it);
            break;
        }
    }
    current_task = nullptr; // Force a context switch
    scheduler(); // Immediately call scheduler to pick next task
}


// Create a new semaphore
int RTOSKernel::createSemaphore(const std::string& name, int initial_count) {
    if (initial_count < 0) {
        throw std::runtime_error("Semaphore initial count cannot be negative.");
    }
    Semaphore* new_sem = new Semaphore(next_semaphore_id++, name, initial_count);
    all_semaphores.push_back(new_sem);
    log("Semaphore created: ID=" + std::to_string(new_sem->id) +
        ", Name='" + new_sem->name + "', Count=" + std::to_string(new_sem->count));
    return new_sem->id;
}

// Task requests to acquire (wait on) a semaphore
void RTOSKernel::semaphoreWait(int semaphore_id) {
    if (current_task == nullptr) {
        log("ERROR: semaphoreWait called outside of a task context.");
        return;
    }

    Semaphore* sem = findSemaphoreById(semaphore_id);
    if (sem == nullptr) {
        log("ERROR: Semaphore with ID " + std::to_string(semaphore_id) + " not found.");
        return;
    }

    log("Task '" + current_task->name + "' trying to acquire semaphore '" + sem->name + "'. Count: " + std::to_string(sem->count));

    if (sem->acquire(current_task)) { // Try to acquire using Semaphore's method
        log("Task '" + current_task->name + "' acquired semaphore '" + sem->name + "'. New Count: " + std::to_string(sem->count));
    } else {
        // Semaphore not available, task is blocked by Semaphore::acquire
        log("Task '" + current_task->name + "' is blocked, waiting for semaphore '" + sem->name + "'.");
        current_task->state = BLOCKED;
        current_task->waiting_on_semaphore_id = semaphore_id; 
        
        // Remove from ready queue if it was there
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
    }
}

// Task releases (signals) a semaphore
void RTOSKernel::semaphoreSignal(int semaphore_id) {
    if (current_task == nullptr) {
        log("ERROR: semaphoreSignal called outside of a task context or no current task.");
        return;
    }

    Semaphore* sem = findSemaphoreById(semaphore_id);
    if (sem == nullptr) {
        log("ERROR: Semaphore with ID " + std::to_string(semaphore_id) + " not found.");
        return;
    }

    sem->release(); // Release using Semaphore's method
    log("Task '" + current_task->name + "' released semaphore '" + sem->name + "'. New Count: " + std::to_string(sem->count));

    // Check if any task was unblocked by the semaphore release
    if (!sem->waiting_queue.empty()) {
        TCB* unblocked_task = sem->waiting_queue.front();
        sem->waiting_queue.pop_front();
        unblockTask(unblocked_task); // Use helper to unblock
    }
}

// Create a new mutex
int RTOSKernel::createMutex(const std::string& name) {
    Mutex* new_mtx = new Mutex(next_mutex_id++, name);
    all_mutexes.push_back(new_mtx);
    log("Mutex created: ID=" + std::to_string(new_mtx->id) + ", Name='" + new_mtx->name + "'.");
    return new_mtx->id;
}

// Task requests to lock a mutex
void RTOSKernel::mutexLock(int mutex_id) {
    if (current_task == nullptr) {
        log("ERROR: mutexLock called outside of a task context.");
        return;
    }

    Mutex* mtx = findMutexById(mutex_id);
    if (mtx == nullptr) {
        log("ERROR: Mutex with ID " + std::to_string(mutex_id) + " not found.");
        return;
    }

    log("Task '" + current_task->name + "' trying to lock mutex '" + mtx->name + "'. Owner: " + (mtx->owner ? mtx->owner->name : "None"));

    if (mtx->lock(current_task)) { // Try to lock using Mutex's method
        log("Task '" + current_task->name + "' locked mutex '" + mtx->name + "'.");
    } else {
        // Mutex not available, task is blocked by Mutex::lock
        log("Task '" + current_task->name + "' is blocked, waiting for mutex '" + mtx->name + "'.");
        current_task->state = BLOCKED;
        current_task->waiting_on_mutex_id = mutex_id; 
        
        // Remove from ready queue if it was there
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
    }
}

// Task releases (unlocks) a mutex
void RTOSKernel::mutexUnlock(int mutex_id) {
    if (current_task == nullptr) {
        log("ERROR: mutexUnlock called outside of a task context or no current task.");
        return;
    }

    Mutex* mtx = findMutexById(mutex_id);
    if (mtx == nullptr) {
        log("ERROR: Mutex with ID " + std::to_string(mutex_id) + " not found.");
        return;
    }

    mtx->unlock(current_task); // Release using Mutex's method
    log("Task '" + current_task->name + "' unlocked mutex '" + mtx->name + "'.");

    // Check if any task was unblocked by the mutex release
    if (!mtx->waiting_queue.empty()) {
        TCB* next_owner_task = mtx->waiting_queue.front();
        mtx->waiting_queue.pop_front();
        // Assign ownership to the unblocked task immediately
        mtx->owner = next_owner_task;
        next_owner_task->owner_mutex_id = mtx->id;
        log("Mutex '" + mtx->name + "' now owned by Task '" + next_owner_task->name + "'.");
        unblockTask(next_owner_task); // Use helper to unblock
    }
}

// Create a new message queue
int RTOSKernel::createMessageQueue(const std::string& name, size_t max_size) {
    if (max_size == 0) {
        throw std::runtime_error("Message Queue max_size cannot be zero.");
    }
    MessageQueue* new_mq = new MessageQueue(next_message_queue_id++, name, max_size);
    all_message_queues.push_back(new_mq);
    log("Message Queue created: ID=" + std::to_string(new_mq->id) + ", Name='" + new_mq->name + "', Max Size=" + std::to_string(new_mq->max_size) + ".");
    return new_mq->id;
}

// Task sends a message to the queue
void RTOSKernel::messageQueueSend(int queue_id, int message) {
    if (current_task == nullptr) {
        log("ERROR: messageQueueSend called outside of a task context.");
        return;
    }

    MessageQueue* mq = findMessageQueueById(queue_id);
    if (mq == nullptr) {
        log("ERROR: Message Queue with ID " + std::to_string(queue_id) + " not found.");
        return;
    }

    log("Task '" + current_task->name + "' trying to send message " + std::to_string(message) + " to queue '" + mq->name + "'. Current size: " + std::to_string(mq->messages.size()));

    if (mq->send(current_task, message)) { // Try to send using MessageQueue's method
        log("Task '" + current_task->name + "' sent message " + std::to_string(message) + " to queue '" + mq->name + "'. New size: " + std::to_string(mq->messages.size()));
        // If a receiver was waiting, unblock it
        if (!mq->receiver_waiting_queue.empty()) {
            TCB* unblocked_receiver = mq->receiver_waiting_queue.front();
            mq->receiver_waiting_queue.pop_front();
            unblockTask(unblocked_receiver);
        }
    } else {
        // Queue is full, task is blocked by MessageQueue::send
        log("Task '" + current_task->name + "' is blocked, waiting to send to queue '" + mq->name + "'.");
        current_task->state = BLOCKED;
        current_task->waiting_on_queue_id = queue_id;
        
        // Remove from ready queue
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
    }
}

// Task receives a message from the queue
bool RTOSKernel::messageQueueReceive(int queue_id, int& out_message) {
    if (current_task == nullptr) {
        log("ERROR: messageQueueReceive called outside of a task context.");
        return false;
    }

    MessageQueue* mq = findMessageQueueById(queue_id);
    if (mq == nullptr) {
        log("ERROR: Message Queue with ID " + std::to_string(queue_id) + " not found.");
        return false;
    }

    log("Task '" + current_task->name + "' trying to receive message from queue '" + mq->name + "'. Current size: " + std::to_string(mq->messages.size()));

    if (mq->receive(current_task, out_message)) { // Try to receive using MessageQueue's method
        log("Task '" + current_task->name + "' received message " + std::to_string(out_message) + " from queue '" + mq->name + "'. New size: " + std::to_string(mq->messages.size()));
        // If a sender was waiting, unblock it
        if (!mq->sender_waiting_queue.empty()) {
            TCB* unblocked_sender = mq->sender_waiting_queue.front();
            mq->sender_waiting_queue.pop_front();
            unblockTask(unblocked_sender);
        }
        return true;
    } else {
        // Queue is empty, task is blocked by MessageQueue::receive
        log("Task '" + current_task->name + "' is blocked, waiting to receive from queue '" + mq->name + "'.");
        current_task->state = BLOCKED;
        current_task->waiting_on_queue_id = queue_id;

        // Remove from ready queue
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
        return false; // Task is blocked, no message received yet
    }
}


// Handle tick increment and unblocking delayed tasks
void RTOSKernel::handleTick() {
    current_tick++;
    log("System Tick: " + std::to_string(current_tick));

    // Iterate through delayed tasks and unblock those whose delay time has expired
    for (auto it = delayed_queue.begin(); it != delayed_queue.end(); ) {
        if ((*it)->delay_until_tick <= current_tick) {
            unblockTask(*it); // Use helper to unblock
            it = delayed_queue.erase(it); // Remove from delayed queue
        } else {
            ++it;
        }
    }
}

// Scheduler: Selects the next task to execute
void RTOSKernel::scheduler() {
    // Clean up ready_queue: remove tasks that are no longer READY (e.g., became BLOCKED or DELAYED)
    ready_queue.erase(std::remove_if(ready_queue.begin(), ready_queue.end(), 
                                      [](TCB* task){ return task->state != READY; }),
                      ready_queue.end());
    sortReadyQueue(); // Ensure it's sorted after potential removals

    if (ready_queue.empty()) {
        log("No tasks are ready to run. Scheduler is idle.");
        current_task = nullptr; // No task running
        return;
    }

    // Select the highest priority task (first in sorted ready_queue)
    TCB* next_task = ready_queue.front();

    // Perform a context switch if no task is running, or if the current task is not the highest priority
    // or if the current task has changed state (e.g., became blocked/delayed)
    if (current_task == nullptr || current_task->state != RUNNING || current_task != next_task) {
        contextSwitch(next_task);
    } else {
        log("Current task ('" + current_task->name + "') continues execution.");
    }
}

// Simulate context switching between tasks
void RTOSKernel::contextSwitch(TCB* next_task) {
    if (current_task != nullptr && current_task->state == RUNNING) {
        // Change state of previous task to READY if it was running and not blocked/delayed
        current_task->state = READY;
        log("Context switch: '" + current_task->name + "' -> READY.");
    }

    // Set the new task as current and change its state to RUNNING
    current_task = next_task;
    current_task->state = RUNNING;
    log("Context switch: '" + current_task->name + "' -> RUNNING.");
}

// Start the RTOS simulation
void RTOSKernel::startScheduler() {
    log("RTOS Scheduler started...");

    // Main simulation loop. In a real RTOS, this would be an infinite loop,
    // with scheduler called by timers/interrupts.
    int simulation_max_ticks = 30; // Simulate for 30 ticks
    for (int i = 0; i < simulation_max_ticks; ++i) {
        handleTick(); // Increment tick and handle delayed tasks
        log("\n--- Simulation Cycle (Tick " + std::to_string(current_tick) + ") ---");
        scheduler(); // Select the next task to run

        if (current_task != nullptr && current_task->state == RUNNING) {
            // Execute the selected task's function
            log("Executing task '" + current_task->name + "'...");
            current_task->execute(); 
            
            // If the task is still running (didn't block or delay itself), re-queue it
            // This simulates a time slice ending and the task being put back into the ready queue
            if (current_task->state == RUNNING) { 
                // Find current_task in ready_queue 
                auto it = std::remove(ready_queue.begin(), ready_queue.end(), current_task);
                ready_queue.erase(it, ready_queue.end());
                
                ready_queue.push_back(current_task); // Move to end for Round Robin effect among same priority
                sortReadyQueue(); // Re-sort to maintain priority across all tasks
            }
        }
    }
    log("RTOS Scheduler terminated after " + std::to_string(current_tick) + " ticks.");
}