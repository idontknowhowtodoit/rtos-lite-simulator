#include "rtos_kernel.hpp"
#include <algorithm> // For std::sort

RTOSKernel::RTOSKernel() : current_task(nullptr), next_task_id(0) {
    log("RTOS Kernel initialized.");
}

RTOSKernel::~RTOSKernel() {
    // Deallocate dynamically created TCB objects
    for (TCB* task : tasks) {
        delete task;
    }
    log("RTOS Kernel terminated and resources freed.");
}

void RTOSKernel::log(const std::string& message) {
    std::cout << "[RTOS Log] " << message << std::endl;
}

// Create and add a task to the kernel's task list and ready queue
void RTOSKernel::createTask(const std::string& name, int priority, std::function<void()> task_func) {
    TCB* new_tcb = new TCB(next_task_id++, name, priority, task_func);
    tasks.push_back(new_tcb);
    ready_queue.push_back(new_tcb);

    // Sort ready_queue by priority (desc) then by ID (asc) for stability
    std::sort(ready_queue.begin(), ready_queue.end(), [](TCB* a, TCB* b) {
        if (a->priority != b->priority) {
            return a->priority > b->priority; // Higher priority (larger number) comes first
        }
        return a->id < b->id; // If priorities are equal, sort by ID (creation order)
    });

    log("Task created: ID=" + std::to_string(new_tcb->id) +
        ", Name='" + new_tcb->name +
        "', Priority=" + std::to_string(new_tcb->priority));
}

// Scheduler: Selects the next task to execute
void RTOSKernel::scheduler() {
    if (ready_queue.empty()) {
        log("No tasks are ready to run. Scheduler is idle.");
        // In a real RTOS, an idle task would run or the system would enter low-power mode.
        return;
    }

    // Select the highest priority task (first in sorted ready_queue)
    TCB* next_task = ready_queue.front();

    // Perform a context switch if no task is running, or if the current task is not the highest priority
    if (current_task == nullptr || current_task->state != RUNNING || current_task != next_task) {
        contextSwitch(next_task);
    } else {
        log("Current task ('" + current_task->name + "') continues execution.");
    }
}

// Simulate context switching between tasks
void RTOSKernel::contextSwitch(TCB* next_task) {
    if (current_task != nullptr) {
        // Change state of previous task to READY (conceptually saving context)
        if (current_task->state == RUNNING) {
             current_task->state = READY;
             log("Context switch: '" + current_task->name + "' -> READY.");
        }
    }

    // Set the new task as current and change its state to RUNNING (conceptually restoring context)
    current_task = next_task;
    current_task->state = RUNNING;
    log("Context switch: '" + current_task->name + "' -> RUNNING.");
}

// Start the RTOS simulation
void RTOSKernel::startScheduler() {
    log("RTOS Scheduler started...");

    // Main simulation loop (in a real RTOS, this would be an infinite loop,
    // with scheduler called by timers/interrupts)
    int simulation_cycles = 10; // Number of simulation cycles to run
    for (int i = 0; i < simulation_cycles; ++i) {
        log("\n--- Simulation Cycle " + std::to_string(i + 1) + " ---");
        scheduler(); // Select the next task to run

        if (current_task != nullptr && current_task->state == RUNNING) {
            // Execute the selected task's function (simulating task execution)
            log("Executing task '" + current_task->name + "'...");
            current_task->task_function(); 
            
            // Re-queue the current task if it's not blocked, simulating a time slice or yielding
            if (!ready_queue.empty() && ready_queue.front() == current_task) {
                TCB* moved_task = ready_queue.front();
                ready_queue.erase(ready_queue.begin());
                // Only re-add if the task did not block itself
                if(moved_task->state != BLOCKED) { 
                    ready_queue.push_back(moved_task);
                    // Re-sort to maintain priority order after re-queuing
                    std::sort(ready_queue.begin(), ready_queue.end(), [](TCB* a, TCB* b) {
                        if (a->priority != b->priority) {
                            return a->priority > b->priority;
                        }
                        return a->id < b->id;
                    });
                }
            }
        }
    }
    log("RTOS Scheduler terminated.");
}