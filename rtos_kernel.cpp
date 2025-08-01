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
    // The actual unblocking of a task from the waiting queue and moving it to the
    // kernel's ready_queue will be handled by the RTOSKernel::semaphoreSignal.
    // This separation allows the kernel to manage global task states.
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

    // If there are tasks waiting, unblock the first one (FIFO)
    if (!waiting_queue.empty()) {
        TCB* next_owner = waiting_queue.front();
        waiting_queue.pop_front();
        // The kernel will handle moving this task to the ready queue and assigning ownership
        // This separation of concerns (Mutex manages its queue, Kernel manages global queues)
        // is a design choice.
    }
}

// --- EventFlag Class Implementation ---
void EventFlag::set(unsigned int flags_to_set) {
    flags |= flags_to_set; // Set flags using bitwise OR
}

void EventFlag::clear(unsigned int flags_to_clear) {
    flags &= ~flags_to_clear; // Clear flags using bitwise AND NOT
}

bool EventFlag::check_and_clear_flags(TCB* task) {
    bool condition_met = false;
    if (task->event_wait_mode == WAIT_ALL) {
        // Check if all required flags are set
        if ((flags & task->event_flags_to_wait_for) == task->event_flags_to_wait_for) {
            condition_met = true;
        }
    } else { // WAIT_ANY
        // Check if any of the required flags are set
        if ((flags & task->event_flags_to_wait_for) != 0) {
            condition_met = true;
        }
    }

    if (condition_met) {
        // Consume the flags if the condition is met (common RTOS behavior)
        flags &= ~task->event_flags_to_wait_for; 
        return true;
    }
    return false;
}

// --- MessageQueue Class Implementation ---
bool MessageQueue::send(TCB* task, const std::string& message) {
    if (queue_data.size() < max_size) {
        queue_data.push(message);
        return true; // Message sent
    } else {
        // Queue is full, block the sending task
        send_waiting_queue.push_back(task);
        return false; // Blocked
    }
}

bool MessageQueue::receive(TCB* task, std::string& out_message) {
    if (!queue_data.empty()) {
        out_message = queue_data.front();
        queue_data.pop();
        return true; // Message received
    } else {
        // Queue is empty, block the receiving task
        receive_waiting_queue.push_back(task);
        return false; // Blocked
    }
}

// --- SoftwareTimer Class Implementation ---
void SoftwareTimer::check_and_expire(unsigned long current_tick) {
    if (!is_running) return;

    if (current_tick >= expiry_tick) {
        // Timer has expired
        kernel_ptr->log("  [Timer '" + name + "' (ID:" + std::to_string(id) + ")] Expired at tick " + std::to_string(current_tick) + ".");

        if (callback_function) {
            callback_function(); // Execute the callback
        }
        
        if (event_flag_id_to_set != -1) {
            // Set event flags through the kernel
            kernel_ptr->eventFlagSet(event_flag_id_to_set, event_flags_to_set);
            kernel_ptr->log("  [Timer '" + name + "'] Set flags 0x" + std::hex + event_flags_to_set + std::dec + 
                            " on Event Flag ID " + std::to_string(event_flag_id_to_set) + ".");
        }

        if (is_periodic) {
            // For periodic timers, reschedule for the next period
            start(current_tick); 
            kernel_ptr->log("  [Timer '" + name + "'] Rescheduled for next expiry at tick " + std::to_string(expiry_tick) + ".");
        } else {
            // For one-shot timers, stop after expiry
            stop();
        }
    }
}


// --- RTOS Kernel Class Implementation ---
RTOSKernel::RTOSKernel() : current_task(nullptr), next_task_id(0), next_semaphore_id(0), 
                           next_mutex_id(0), next_event_flag_id(0), next_message_queue_id(0), 
                           next_timer_id(0), current_tick(0) {
    log("RTOS Kernel initialized.");
    // Create the idle task with the lowest possible priority
    createTask("Idle Task", 0, [&]() { 
        // The idle task simply runs an infinite loop, doing nothing or low-priority work
        // In a real RTOS, this might put the CPU into a low-power state.
        // Here, it just logs its activity.
        // std::cout << "  [Idle Task] Idling... Current Tick: " << current_tick << std::endl;
    });
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

    // Deallocate dynamically created EventFlag objects
    for (EventFlag* ef : all_event_flags) {
        delete ef;
    }
    all_event_flags.clear();

    // Deallocate dynamically created MessageQueue objects
    for (MessageQueue* mq : all_message_queues) {
        delete mq;
    }
    all_message_queues.clear();

    // Deallocate dynamically created SoftwareTimer objects
    for (SoftwareTimer* timer : all_timers) {
        delete timer;
    }
    all_timers.clear();

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

// Helper to find an EventFlag by ID
EventFlag* RTOSKernel::findEventFlagById(int id) {
    for (EventFlag* ef : all_event_flags) {
        if (ef->id == id) {
            return ef;
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

// Helper to find a SoftwareTimer by ID
SoftwareTimer* RTOSKernel::findTimerById(int id) {
    for (SoftwareTimer* timer : all_timers) {
        if (timer->id == id) {
            return timer;
        }
    }
    return nullptr;
}


// Helper to move a task from BLOCKED/DELAYED to READY state
void RTOSKernel::unblockTask(TCB* task) {
    if (task->state == BLOCKED || task->state == DELAYED) {
        task->state = READY;
        // Reset all waiting states
        task->waiting_on_semaphore_id = -1;
        task->waiting_on_mutex_id = -1;
        task->waiting_on_event_flag_id = -1;
        task->event_flags_to_wait_for = 0;
        task->event_wait_mode = WAIT_ANY;
        task->waiting_on_message_queue_id = -1; 
        task->delay_until_tick = 0;
        
        ready_queue.push_back(task);
        log("Task '" + task->name + "' unblocked and moved to READY state.");
        sortReadyQueue();
    }
}

// Create and add a task to the kernel's task list and ready queue (regular task)
void RTOSKernel::createTask(const std::string& name, int priority, std::function<void()> task_func) {
    TCB* new_tcb = new TCB(next_task_id++, name, priority, task_func, this); // Pass kernel pointer to TCB
    all_tasks.push_back(new_tcb);
    ready_queue.push_back(new_tcb);

    sortReadyQueue(); // Sort the ready queue after adding a new task

    log("Task created: ID=" + std::to_string(new_tcb->id) +
        ", Name='" + new_tcb->name +
        "', Priority=" + std::to_string(new_tcb->priority));
}

// Create and add a task to the kernel's task list and ready queue (periodic task)
void RTOSKernel::createTask(const std::string& name, int priority, std::function<void()> task_func, unsigned long period_ticks) {
    TCB* new_tcb = new TCB(next_task_id++, name, priority, task_func, this, true, period_ticks);
    new_tcb->next_run_tick = current_tick + period_ticks; // Set initial run time
    all_tasks.push_back(new_tcb);
    // Periodic tasks are not immediately added to ready_queue.
    // They are scheduled to run by handlePeriodicTasks when their next_run_tick is reached.

    log("Periodic Task created: ID=" + std::to_string(new_tcb->id) +
        ", Name='" + new_tcb->name +
        "', Priority=" + std::to_string(new_tcb->priority) +
        ", Period=" + std::to_string(new_tcb->period_ticks) + " ticks. Next run at tick " + std::to_string(new_tcb->next_run_tick));
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

// Create a new event flag group
int RTOSKernel::createEventFlag(const std::string& name) {
    EventFlag* new_ef = new EventFlag(next_event_flag_id++, name);
    all_event_flags.push_back(new_ef);
    log("Event Flag created: ID=" + std::to_string(new_ef->id) + ", Name='" + new_ef->name + "'.");
    return new_ef->id;
}

// Task waits for specific event flags
bool RTOSKernel::eventFlagWait(int event_flag_id, unsigned int flags_to_wait_for, EventWaitMode mode) {
    if (current_task == nullptr) {
        log("ERROR: eventFlagWait called outside of a task context.");
        return false;
    }

    EventFlag* ef = findEventFlagById(event_flag_id);
    if (ef == nullptr) {
        log("ERROR: Event Flag with ID " + std::to_string(event_flag_id) + " not found.");
        return false;
    }

    log("Task '" + current_task->name + "' trying to wait for flags 0x" + std::hex + flags_to_wait_for + std::dec + 
        " on Event Flag '" + ef->name + "' (Current: 0x" + std::hex + ef->flags + std::dec + "). Mode: " + 
        (mode == WAIT_ALL ? "WAIT_ALL" : "WAIT_ANY"));

    current_task->event_flags_to_wait_for = flags_to_wait_for;
    current_task->event_wait_mode = mode;

    if (ef->check_and_clear_flags(current_task)) {
        kernel_ptr->log("  Task '" + current_task->name + "' condition met for Event Flag '" + ef->name + "'. Flags consumed.");
        return true; // Condition met, flags consumed
    } else {
        // Condition not met, block the task
        log("Task '" + current_task->name + "' is blocked, waiting for Event Flag '" + ef->name + "'.");
        current_task->state = BLOCKED;
        current_task->waiting_on_event_flag_id = event_flag_id;
        ef->waiting_queue.push_back(current_task);

        // Remove from ready queue if it was there
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
        return false; // Task blocked
    }
}

// Set specified flags in an event flag group
void RTOSKernel::eventFlagSet(int event_flag_id, unsigned int flags_to_set) {
    if (current_task == nullptr) {
        // This can be called from a timer callback, so current_task might be null.
        // We log a warning but allow it.
        // log("WARNING: eventFlagSet called outside of a task context. (Allowed for timers)");
    }

    EventFlag* ef = findEventFlagById(event_flag_id);
    if (ef == nullptr) {
        log("ERROR: Event Flag with ID " + std::to_string(event_flag_id) + " not found.");
        return;
    }

    std::string caller_name = (current_task != nullptr) ? current_task->name : "System/Timer";
    log(caller_name + " setting flags 0x" + std::hex + flags_to_set + std::dec + 
        " on Event Flag '" + ef->name + "'. Current flags: 0x" + std::hex + ef->flags + std::dec);
    ef->set(flags_to_set);
    log("Event Flag '" + ef->name + "' new flags: 0x" + std::hex + ef->flags + std::dec);

    checkAndUnblockEventFlagWaiters(ef); // Check if any waiting tasks can now be unblocked
}

// Clear specified flags in an event flag group
void RTOSKernel::eventFlagClear(int event_flag_id, unsigned int flags_to_clear) {
    if (current_task == nullptr) {
        log("ERROR: eventFlagClear called outside of a task context.");
        return;
    }

    EventFlag* ef = findEventFlagById(event_flag_id);
    if (ef == nullptr) {
        log("ERROR: Event Flag with ID " + std::to_string(event_flag_id) + " not found.");
        return;
    }

    log("Task '" + current_task->name + "' clearing flags 0x" + std::hex + flags_to_clear + std::dec + 
        " on Event Flag '" + ef->name + "'. Current flags: 0x" + std::hex + ef->flags + std::dec);
    ef->clear(flags_to_clear);
    log("Event Flag '" + ef->name + "' new flags: 0x" + std::hex + ef->flags + std::dec);
    // Clearing flags doesn't unblock tasks, so no checkAndUnblockEventFlagWaiters call here.
}

// Helper to check and unblock tasks waiting on an EventFlag
void RTOSKernel::checkAndUnblockEventFlagWaiters(EventFlag* ef) {
    for (auto it = ef->waiting_queue.begin(); it != ef->waiting_queue.end(); ) {
        TCB* waiting_task = *it;
        if (ef->check_and_clear_flags(waiting_task)) {
            log("Event Flag '" + ef->name + "': Task '" + waiting_task->name + "' unblocked by flag condition.");
            unblockTask(waiting_task); // Use helper to unblock
            it = ef->waiting_queue.erase(it); // Remove from event flag's waiting queue
        } else {
            ++it;
        }
    }
}

// Create a new message queue
int RTOSKernel::createMessageQueue(const std::string& name, size_t max_size) {
    if (max_size == 0) {
        throw std::runtime_error("Message Queue max_size cannot be zero.");
    }
    MessageQueue* new_mq = new MessageQueue(next_message_queue_id++, name, max_size);
    all_message_queues.push_back(new_mq);
    log("Message Queue created: ID=" + std::to_string(new_mq->id) +
        ", Name='" + new_mq->name + "', Max Size=" + std::to_string(new_mq->max_size));
    return new_mq->id;
}

// Task sends a message to a queue
bool RTOSKernel::messageQueueSend(int mq_id, const std::string& message) {
    if (current_task == nullptr) {
        log("ERROR: messageQueueSend called outside of a task context.");
        return false;
    }

    MessageQueue* mq = findMessageQueueById(mq_id);
    if (mq == nullptr) {
        log("ERROR: Message Queue with ID " + std::to_string(mq_id) + " not found.");
        return false;
    }

    log("Task '" + current_task->name + "' trying to send message '" + message + 
        "' to Message Queue '" + mq->name + "'. Current size: " + std::to_string(mq->queue_data.size()));

    if (mq->send(current_task, message)) {
        log("Task '" + current_task->name + "' sent message '" + message + 
            "' to Message Queue '" + mq->name + "'. New size: " + std::to_string(mq->queue_data.size()));
        checkAndUnblockMessageQueueWaiters(mq); // Check if any receiving tasks can now be unblocked
        return true;
    } else {
        // Queue full, task blocked
        log("Task '" + current_task->name + "' is blocked, Message Queue '" + mq->name + "' is full.");
        current_task->state = BLOCKED;
        current_task->waiting_on_message_queue_id = mq_id;

        // Remove from ready queue if it was there
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
        return false;
    }
}

// Task receives a message from a queue
bool RTOSKernel::messageQueueReceive(int mq_id, std::string& out_message) {
    if (current_task == nullptr) {
        log("ERROR: messageQueueReceive called outside of a task context.");
        return false;
    }

    MessageQueue* mq = findMessageQueueById(mq_id);
    if (mq == nullptr) {
        log("ERROR: Message Queue with ID " + std::to_string(mq_id) + " not found.");
        return false;
    }

    log("Task '" + current_task->name + "' trying to receive message from Message Queue '" + mq->name + 
        "'. Current size: " + std::to_string(mq->queue_data.size()));

    if (mq->receive(current_task, out_message)) {
        log("Task '" + current_task->name + "' received message '" + out_message + 
            "' from Message Queue '" + mq->name + "'. New size: " + std::to_string(mq->queue_data.size()));
        checkAndUnblockMessageQueueWaiters(mq); // Check if any sending tasks can now be unblocked
        return true;
    } else {
        // Queue empty, task blocked
        log("Task '" + current_task->name + "' is blocked, Message Queue '" + mq->name + "' is empty.");
        current_task->state = BLOCKED;
        current_task->waiting_on_message_queue_id = mq_id;

        // Remove from ready queue if it was there
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == current_task) {
                ready_queue.erase(it);
                break;
            }
        }
        current_task = nullptr; // Force a context switch
        scheduler(); // Immediately call scheduler to pick next task
        return false;
    }
}

// Helper to check and unblock tasks waiting on a MessageQueue
void RTOSKernel::checkAndUnblockMessageQueueWaiters(MessageQueue* mq) {
    // Check tasks waiting to receive (queue was empty, now might have messages)
    if (!mq->queue_data.empty()) {
        for (auto it = mq->receive_waiting_queue.begin(); it != mq->receive_waiting_queue.end(); ) {
            TCB* waiting_task = *it;
            // Try to receive for the waiting task (this will immediately succeed and unblock)
            std::string temp_message; // Dummy variable, actual message passed to task when it runs
            if (mq->receive(waiting_task, temp_message)) { // This effectively just checks if it can receive
                log("Message Queue '" + mq->name + "': Task '" + waiting_task->name + "' unblocked by new message.");
                unblockTask(waiting_task);
                it = mq->receive_waiting_queue.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Check tasks waiting to send (queue was full, now might have space)
    if (mq->queue_data.size() < mq->max_size) {
        for (auto it = mq->send_waiting_queue.begin(); it != mq->send_waiting_queue.end(); ) {
            TCB* waiting_task = *it;
            // We don't actually send the message here, just unblock.
            // The task will re-attempt sending when it runs.
            log("Message Queue '" + mq->name + "': Task '" + waiting_task->name + "' unblocked by space available.");
            unblockTask(waiting_task);
            it = mq->send_waiting_queue.erase(it);
        }
    }
}

// Create a new software timer (callback-based)
int RTOSKernel::createTimer(const std::string& name, unsigned long period_ticks, std::function<void()> callback_func) {
    SoftwareTimer* new_timer = new SoftwareTimer(next_timer_id++, name, period_ticks, callback_func, this);
    all_timers.push_back(new_timer);
    log("Timer created (callback): ID=" + std::to_string(new_timer->id) +
        ", Name='" + new_timer->name + "', Period=" + std::to_string(new_timer->period_ticks) +
        (new_timer->is_periodic ? " (Periodic)" : " (One-shot)"));
    return new_timer->id;
}

// Create a new software timer (event flag-based)
int RTOSKernel::createTimer(const std::string& name, unsigned long period_ticks, int event_flag_id, unsigned int event_flags_to_set) {
    EventFlag* ef = findEventFlagById(event_flag_id);
    if (ef == nullptr) {
        log("ERROR: Cannot create timer. Event Flag with ID " + std::to_string(event_flag_id) + " not found.");
        return -1;
    }
    SoftwareTimer* new_timer = new SoftwareTimer(next_timer_id++, name, period_ticks, event_flag_id, event_flags_to_set, this);
    all_timers.push_back(new_timer);
    log("Timer created (event flag): ID=" + std::to_string(new_timer->id) +
        ", Name='" + new_timer->name + "', Period=" + std::to_string(new_timer->period_ticks) +
        ", Sets flags 0x" + std::hex + event_flags_to_set + std::dec + " on EF ID " + std::to_string(event_flag_id) +
        (new_timer->is_periodic ? " (Periodic)" : " (One-shot)"));
    return new_timer->id;
}

void RTOSKernel::startTimer(int timer_id) {
    SoftwareTimer* timer = findTimerById(timer_id);
    if (timer == nullptr) {
        log("ERROR: Timer with ID " + std::to_string(timer_id) + " not found.");
        return;
    }
    if (!timer->is_running) {
        timer->start(current_tick);
        log("Timer '" + timer->name + "' (ID:" + std::to_string(timer_id) + ") started. Expires at tick " + std::to_string(timer->expiry_tick) + ".");
    } else {
        log("Timer '" + timer->name + "' (ID:" + std::to_string(timer_id) + ") is already running.");
    }
}

void RTOSKernel::stopTimer(int timer_id) {
    SoftwareTimer* timer = findTimerById(timer_id);
    if (timer == nullptr) {
        log("ERROR: Timer with ID " + std::to_string(timer_id) + " not found.");
        return;
    }
    if (timer->is_running) {
        timer->stop();
        log("Timer '" + timer->name + "' (ID:" + std::to_string(timer_id) + ") stopped.");
    } else {
        log("Timer '" + timer->name + "' (ID:" + std::to_string(timer_id) + ") is not running.");
    }
}

void RTOSKernel::resetTimer(int timer_id) {
    SoftwareTimer* timer = findTimerById(timer_id);
    if (timer == nullptr) {
        log("ERROR: Timer with ID " + std::to_string(timer_id) + " not found.");
        return;
    }
    timer->stop(); // Stop first
    timer->start(current_tick); // Then restart from current tick
    log("Timer '" + timer->name + "' (ID:" + std::to_string(timer_id) + ") reset and started. Expires at tick " + std::to_string(timer->expiry_tick) + ".");
}


// Handle tick increment and unblocking delayed tasks
void RTOSKernel::handleTick() {
    current_tick++;
    log("System Tick: " + std::to_string(current_tick));

    // 1. Handle delayed tasks
    for (auto it = delayed_queue.begin(); it != delayed_queue.end(); ) {
        if ((*it)->delay_until_tick <= current_tick) {
            unblockTask(*it); // Use helper to unblock
            it = delayed_queue.erase(it); // Remove from delayed queue
        } else {
            ++it;
        }
    }

    // 2. Handle periodic tasks
    handlePeriodicTasks();

    // 3. Handle software timers
    handleSoftwareTimers();
}

// Handle periodic tasks: check if their next_run_tick has arrived
void RTOSKernel::handlePeriodicTasks() {
    for (TCB* task : all_tasks) {
        // Only consider periodic tasks that are not currently blocked or delayed
        if (task->is_periodic && task->state != BLOCKED && task->state != DELAYED) {
            if (current_tick >= task->next_run_tick) {
                log("Periodic Task '" + task->name + "' (ID:" + std::to_string(task->id) + ") scheduled to run at tick " + std::to_string(current_tick) + ".");
                unblockTask(task); // Move to READY state
                task->next_run_tick = current_tick + task->period_ticks; // Schedule next run
            }
        }
    }
}

// Handle software timers: check if they have expired
void RTOSKernel::handleSoftwareTimers() {
    for (SoftwareTimer* timer : all_timers) {
        timer->check_and_expire(current_tick);
    }
}


// Scheduler: Selects the next task to execute
void RTOSKernel::scheduler() {
    // Clean up ready_queue: remove tasks that are no longer READY (e.g., became BLOCKED or DELAYED)
    ready_queue.erase(std::remove_if(ready_queue.begin(), ready_queue.end(), 
                                      [](TCB* task){ return task->state != READY; }),
                      ready_queue.end());
    sortReadyQueue(); // Ensure it's sorted after potential removals

    TCB* next_task = nullptr;

    if (!ready_queue.empty()) {
        // Select the highest priority task (first in sorted ready_queue)
        next_task = ready_queue.front();
    } else {
        // If no other tasks are ready, run the Idle Task (which is always task ID 0)
        next_task = findTaskById(0); // Assuming Idle Task is always the first created (ID 0)
        if (next_task == nullptr || next_task->name != "Idle Task") {
            // Fallback for safety, though Idle Task should always be ID 0
            for(TCB* task : all_tasks) {
                if (task->name == "Idle Task") {
                    next_task = task;
                    break;
                }
            }
        }
        if (next_task != nullptr) {
            // Ensure Idle Task is marked as READY if it somehow isn't
            if (next_task->state != READY) {
                next_task->state = READY;
                // Add to ready queue if it's not there, then sort
                bool found_in_ready = false;
                for(TCB* t : ready_queue) {
                    if (t == next_task) {
                        found_in_ready = true;
                        break;
                    }
                }
                if (!found_in_ready) {
                    ready_queue.push_back(next_task);
                    sortReadyQueue();
                }
            }
            log("No other tasks are ready. Selecting Idle Task.");
        } else {
            log("ERROR: No tasks available, and Idle Task not found!");
            return; // Critical error, no task to run
        }
    }

    // Perform a context switch if no task is running, or if the current task is not the highest priority
    // or if the current task has changed state (e.g., became blocked/delayed)
    if (current_task == nullptr || current_task->state != RUNNING || current_task != next_task) {
        contextSwitch(next_task);
    } else {
        // Only log if the current task is NOT the idle task to avoid spamming
        if (current_task->name != "Idle Task") {
            log("Current task ('" + current_task->name + "') continues execution.");
        }
    }
}

// Simulate context switching between tasks
void RTOSKernel::contextSwitch(TCB* next_task) {
    if (current_task != nullptr && current_task->state == RUNNING) {
        // Change state of previous task to READY if it was running and not blocked/delayed
        // And it's not the idle task (idle task doesn't get put back in ready queue like others)
        if (current_task->name != "Idle Task") {
            current_task->state = READY;
            log("Context switch: '" + current_task->name + "' -> READY.");
        }
    }

    // Set the new task as current and change its state to RUNNING
    current_task = next_task;
    current_task->state = RUNNING;
    // Only log if the current task is NOT the idle task to avoid spamming
    if (current_task->name != "Idle Task") {
        log("Context switch: '" + current_task->name + "' -> RUNNING.");
    }
}

// Start the RTOS simulation
void RTOSKernel::startScheduler() {
    log("RTOS Scheduler started...");

    // Main simulation loop. In a real RTOS, this would be an infinite loop,
    // with scheduler called by timers/interrupts.
    int simulation_max_ticks = 100; // Simulate for 100 ticks
    for (int i = 0; i < simulation_max_ticks; ++i) {
        log("\n--- Simulation Cycle (Tick " + std::to_string(current_tick) + ") ---");
        handleTick(); // Increment tick and handle delayed/periodic tasks/timers
        scheduler(); // Select the next task to run

        if (current_task != nullptr && current_task->state == RUNNING) {
            // Execute the selected task's function
            if (current_task->name != "Idle Task") { // Only log execution for non-idle tasks
                log("Executing task '" + current_task->name + "'...");
            }
            current_task->execute(); 
            
            // If the task is still running (didn't block or delay itself), re-queue it
            // This simulates a time slice ending and the task being put back into the ready queue
            // This applies to non-idle tasks. Idle task remains at the bottom.
            if (current_task->state == RUNNING && current_task->name != "Idle Task") { 
                // Find current_task in ready_queue (it should be at front if it was just run)
                // Use std::remove to efficiently remove and then erase
                auto it = std::remove(ready_queue.begin(), ready_queue.end(), current_task);
                ready_queue.erase(it, ready_queue.end());
                
                ready_queue.push_back(current_task); // Move to end for Round Robin effect among same priority
                sortReadyQueue(); // Re-sort to maintain priority across all tasks
            }
        }
    }
    log("RTOS Scheduler terminated after " + std::to_string(current_tick) + " ticks.");
}