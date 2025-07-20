#include "rtos_kernel.hpp"
#include <thread> // For std::this_thread::sleep_for (simulating time delays)
#include <chrono> // For std::chrono::milliseconds
#include <atomic> // For std::atomic_int (to simulate shared resource)

// Global (shared) resources, protected by synchronization primitives
std::atomic_int shared_resource_sem = 0; // Protected by semaphore
std::atomic_int shared_resource_mtx = 0; // Protected by mutex

// IDs for synchronization primitives
int shared_resource_semaphore_id = -1; 
int shared_resource_mutex_id = -1;

// --- Virtual Task Functions ---
// These functions simulate the work done by real tasks.

// Task that acquires semaphore, increments shared resource, then releases
void producer_task_function(RTOSKernel* kernel_ptr, int sem_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Producer Task] Attempting to acquire semaphore.");
    kernel_ptr->semaphoreWait(sem_id); // Wait for semaphore

    // CRITICAL SECTION START (Semaphore protected)
    kernel_ptr->log("  [Producer Task] Acquired semaphore. Modifying shared_resource_sem...");
    shared_resource_sem++; // Modify shared resource
    std::cout << "  [Producer Task] Shared resource (sem) value: " << shared_resource_sem << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->semaphoreSignal(sem_id); // Release semaphore
    kernel_ptr->log("  [Producer Task] Released semaphore.");
}

// Task that also acquires semaphore, decrements shared resource, then releases
void consumer_task_function(RTOSKernel* kernel_ptr, int sem_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Consumer Task] Attempting to acquire semaphore.");
    kernel_ptr->semaphoreWait(sem_id); // Wait for semaphore

    // CRITICAL SECTION START (Semaphore protected)
    kernel_ptr->log("  [Consumer Task] Acquired semaphore. Reading/Modifying shared_resource_sem...");
    if (shared_resource_sem > 0) {
        shared_resource_sem--;
    }
    std::cout << "  [Consumer Task] Shared resource (sem) value: " << shared_resource_sem << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->semaphoreSignal(sem_id); // Release semaphore
    kernel_ptr->log("  [Consumer Task] Released semaphore.");
}

// Task that acquires mutex, increments shared resource, then releases
void mutex_access_task_function(RTOSKernel* kernel_ptr, int mtx_id, const std::string& task_name) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [" + task_name + "] Attempting to lock mutex.");
    kernel_ptr->mutexLock(mtx_id); // Lock mutex

    // CRITICAL SECTION START (Mutex protected)
    kernel_ptr->log("  [" + task_name + "] Locked mutex. Modifying shared_resource_mtx...");
    shared_resource_mtx++; // Modify shared resource
    std::cout << "  [" + task_name + "] Shared resource (mtx) value: " << shared_resource_mtx << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->mutexUnlock(mtx_id); // Unlock mutex
    kernel_ptr->log("  [" + task_name + "] Unlocked mutex.");
}


// A simple background task that doesn't use synchronization primitives
void background_task_function(RTOSKernel* kernel_ptr) {
    std::cout << "  [Background Task] Running independently. Current Tick: " << kernel_ptr->getCurrentTick() << std::endl;
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
    RTOSKernel kernel; 

    // Create a semaphore to protect shared_resource_sem
    shared_resource_semaphore_id = kernel.createSemaphore("SharedResourceSem", 1); 

    // Create a mutex to protect shared_resource_mtx
    shared_resource_mutex_id = kernel.createMutex("SharedResourceMutex");

    // Create tasks with different names, priorities, and functions
    // Semaphore-using tasks
    kernel.createTask("Producer Task 1", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); // High priority
    kernel.createTask("Consumer Task 1", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); // Medium-high priority
    
    // Mutex-using tasks
    kernel.createTask("Mutex Task A", 9, [&]() { mutex_access_task_function(&kernel, shared_resource_mutex_id, "Mutex Task A"); }); // High priority
    kernel.createTask("Mutex Task B", 9, [&]() { mutex_access_task_function(&kernel, shared_resource_mutex_id, "Mutex Task B"); }); // High priority

    // Background and Delay tasks
    kernel.createTask("Background Task", 5, [&]() { background_task_function(&kernel); });                         // Medium priority
    kernel.createTask("Delay Task 1", 7, [&]() { delay_task_function(&kernel, 5); });                                // Medium priority, delays for 5 ticks
    
    kernel.createTask("Producer Task 2", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); // High priority
    kernel.createTask("Consumer Task 2", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); // Medium-high priority
    kernel.createTask("Delay Task 2", 6, [&]() { delay_task_function(&kernel, 10); });                               // Medium priority, delays for 10 ticks


    // Start the RTOS scheduler simulation
    kernel.startScheduler();

    return 0;
}