#include "rtos_kernel.hpp"
#include <algorithm> // For std::sort
#include <stdexcept> // For std::runtime_error

RTOSKernel::RTOSKernel() : current_task(nullptr), next_task_id(0), next_semaphore_id(0) {
    log("RTOS Kernel initialized.");
}

RTOSKernel::~RTOSKernel() {
    // Deallocate dynamically created TCB objects
    for (TCB* task : tasks) {
        delete task;
    }
    tasks.clear(); // Clear the vector after deletion

    // Deallocate dynamically created Semaphore objects
    for (Semaphore* sem : semaphores) {
        delete sem;
    }
    semaphores.clear(); // Clear the vector after deletion

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

// Create and add a task to the kernel's task list and ready queue
void RTOSKernel::createTask(const std::string& name, int priority, std::function<void()> task_func) {
    TCB* new_tcb = new TCB(next_task_id++, name, priority, task_func);
    tasks.push_back(new_tcb);
    ready_queue.push_back(new_tcb);

    sortReadyQueue(); // Sort the ready queue after adding a new task

    log("Task created: ID=" + std::to_string(new_tcb->id) +
        ", Name='" + new_tcb->name +
        "', Priority=" + std::to_string(new_tcb->priority));
}

// Create a new semaphore
int RTOSKernel::createSemaphore(const std::string& name, int initial_count) {
    if (initial_count < 0) {
        throw std::runtime_error("Semaphore initial count cannot be negative.");
    }
    Semaphore* new_sem = new Semaphore(next_semaphore_id++, name, initial_count);
    semaphores.push_back(new_sem);
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

    Semaphore* sem = nullptr;
    for (Semaphore* s : semaphores) {
        if (s->id == semaphore_id) {
            sem = s;
            break;
        }
    }

    if (sem == nullptr) {
        log("ERROR: Semaphore with ID " + std::to_string(semaphore_id) + " not found.");
        return;
    }

    log("Task '" + current_task->name + "' trying to acquire semaphore '" + sem->name + "'. Count: " + std::to_string(sem->count));

    if (sem->count > 0) {
        // Semaphore is available, acquire it
        sem->count--;
        log("Task '" + current_task->name + "' acquired semaphore '" + sem->name + "'. New Count: " + std::to_string(sem->count));
    } else {
        // Semaphore is not available, block the current task
        log("Task '" + current_task->name + "' is blocked, waiting for semaphore '" + sem->name + "'.");
        current_task->state = BLOCKED;
        current_task->waiting_on_semaphore_id = semaphore_id; // Record which semaphore it's waiting on
        sem->waiting_queue.push_back(current_task);
        
        // Remove from ready queue if it was there (it usually would be if it's currently running)
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

    Semaphore* sem = nullptr;
    for (Semaphore* s : semaphores) {
        if (s->id == semaphore_id) {
            sem = s;
            break;
        }
    }

    if (sem == nullptr) {
        log("ERROR: Semaphore with ID " + std::to_string(semaphore_id) + " not found.");
        return;
    }

    sem->count++;
    log("Task '" + current_task->name + "' released semaphore '" + sem->name + "'. New Count: " + std::to_string(sem->count));

    // Check if there are tasks waiting for this semaphore
    if (!sem->waiting_queue.empty()) {
        // Unblock the first task in the waiting queue (FIFO)
        TCB* unblocked_task = sem->waiting_queue.front();
        sem->waiting_queue.pop_front();

        unblocked_task->state = READY;
        unblocked_task->waiting_on_semaphore_id = -1; // No longer waiting
        ready_queue.push_back(unblocked_task); // Add back to ready queue

        log("Task '" + unblocked_task->name + "' unblocked and moved to READY state.");
        sortReadyQueue(); // Re-sort the ready queue as a new task is ready
    }
}

// Scheduler: Selects the next task to execute
void RTOSKernel::scheduler() {
    // Remove any tasks that might have finished or become blocked from the front of the ready queue
    while (!ready_queue.empty() && (ready_queue.front()->state != READY)) {
        ready_queue.erase(ready_queue.begin()); // Remove if not ready (e.g., just blocked)
        sortReadyQueue(); // Re-sort after removal
    }

    if (ready_queue.empty()) {
        log("No tasks are ready to run. Scheduler is idle.");
        return;
    }

    // Select the highest priority task (first in sorted ready_queue)
    TCB* next_task = ready_queue.front();

    // Perform a context switch if no task is running, or if the current task is not the highest priority
    // or if the current task has completed its time slice (implicitly handled by re-queuing)
    // or if the current task became blocked and scheduler was called.
    if (current_task == nullptr || current_task->state != RUNNING || current_task != next_task) {
        contextSwitch(next_task);
    } else {
        log("Current task ('" + current_task->name + "') continues execution.");
    }
}

// Simulate context switching between tasks
void RTOSKernel::contextSwitch(TCB* next_task) {
    if (current_task != nullptr && current_task->state == RUNNING) {
        // Change state of previous task to READY if it was running and not blocked
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
    int simulation_cycles = 20; // Increased cycles to better observe semaphore behavior
    for (int i = 0; i < simulation_cycles; ++i) {
        log("\n--- Simulation Cycle " + std::to_string(i + 1) + " ---");
        scheduler(); // Select the next task to run

        if (current_task != nullptr && current_task->state == RUNNING) {
            // Execute the selected task's function
            log("Executing task '" + current_task->name + "'...");
            current_task->task_function(); 
            
            // If the task is still running (didn't block itself), re-queue it
            // This simulates a time slice ending and the task being put back into the ready queue
            if (current_task->state == RUNNING) { 
                // Find current_task in ready_queue (it should be at front if it was just run)
                for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
                    if (*it == current_task) {
                        ready_queue.erase(it);
                        break;
                    }
                }
                ready_queue.push_back(current_task); // Move to end for Round Robin effect among same priority
                sortReadyQueue(); // Re-sort to maintain priority across all tasks
            }
        }
    }
    log("RTOS Scheduler terminated.");
}