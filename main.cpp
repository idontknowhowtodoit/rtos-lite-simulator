#include "rtos_kernel.hpp"
#include <thread> // For std::this_thread::sleep_for (simulating time delays)
#include <chrono> // For std::chrono::milliseconds
#include <atomic> // For std::atomic_int (to simulate shared resource)

// Global (shared) resource, protected by a semaphore
std::atomic_int shared_resource = 0;
// We will get the semaphore ID from main() and pass it to tasks via lambda capture
int shared_resource_semaphore_id = -1; 

// --- Virtual Task Functions ---
// These functions simulate the work done by real tasks.

// Task that acquires semaphore, increments shared resource, then releases
void producer_task_function(RTOSKernel* kernel_ptr, int sem_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Producer Task] Attempting to acquire semaphore.");
    kernel_ptr->semaphoreWait(sem_id); // Wait for semaphore

    // CRITICAL SECTION START
    kernel_ptr->log("  [Producer Task] Acquired semaphore. Modifying shared resource...");
    shared_resource++; // Modify shared resource
    std::cout << "  [Producer Task] Shared resource value: " << shared_resource << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->semaphoreSignal(sem_id); // Release semaphore
    kernel_ptr->log("  [Producer Task] Released semaphore.");
}

// Task that also acquires semaphore, decrements shared resource, then releases
void consumer_task_function(RTOSKernel* kernel_ptr, int sem_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Consumer Task] Attempting to acquire semaphore.");
    kernel_ptr->semaphoreWait(sem_id); // Wait for semaphore

    // CRITICAL SECTION START
    kernel_ptr->log("  [Consumer Task] Acquired semaphore. Reading/Modifying shared resource...");
    if (shared_resource > 0) {
        shared_resource--;
    }
    std::cout << "  [Consumer Task] Shared resource value: " << shared_resource << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->semaphoreSignal(sem_id); // Release semaphore
    kernel_ptr->log("  [Consumer Task] Released semaphore.");
}

// A simple background task that doesn't use semaphores
void background_task_function() {
    std::cout << "  [Background Task] Running independently. Current Tick: " << kernel.getCurrentTick() << std::endl;
}

// Task that uses the delay function
void delay_task_function(RTOSKernel* kernel_ptr, unsigned long delay_ticks) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Delay Task] Going to delay for " + std::to_string(delay_ticks) + " ticks. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
    kernel_ptr->delay(delay_ticks); // Call the kernel's delay function

    // This part of the code will only execute AFTER the delay has expired
    kernel_ptr->log("  [Delay Task] Delay finished. Resumed execution. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
}


int main() {
    // Create an RTOS Kernel instance
    RTOSKernel kernel; // Make kernel non-const to pass its pointer

    // Create a semaphore to protect the shared_resource
    // Initial count 1 for a binary semaphore (mutex-like behavior)
    shared_resource_semaphore_id = kernel.createSemaphore("SharedResourceSem", 1); 

    // Create tasks with different names, priorities, and functions
    // Pass kernel pointer and semaphore ID to task functions using lambdas
    kernel.createTask("Producer Task 1", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); // High priority
    kernel.createTask("Consumer Task 1", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); // Medium-high priority
    kernel.createTask("Background Task", 5, background_task_function);                                             // Medium priority
    kernel.createTask("Delay Task 1", 7, [&]() { delay_task_function(&kernel, 5); });                                // Medium priority, delays for 5 ticks
    kernel.createTask("Producer Task 2", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); // High priority
    kernel.createTask("Consumer Task 2", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); // Medium-high priority
    kernel.createTask("Delay Task 2", 6, [&]() { delay_task_function(&kernel, 10); });                               // Medium priority, delays for 10 ticks


    // Start the RTOS scheduler simulation
    kernel.startScheduler();

    return 0;
}