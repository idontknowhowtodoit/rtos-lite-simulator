#include "rtos_kernel.hpp"
#include <thread> 
#include <chrono> 
#include <atomic> 

// Global (shared) resources, protected by synchronization primitives
std::atomic_int shared_resource_sem = 0; // Protected by semaphore
std::atomic_int shared_resource_mtx = 0; // Protected by mutex

// IDs for synchronization primitives
int shared_resource_semaphore_id = -1; 
int shared_resource_mutex_id = -1;
int communication_queue_id = -1; // For message queue

// --- Virtual Task Functions ---
// These functions simulate the work done by real tasks.

// Task that acquires semaphore, increments shared resource, then releases
void producer_task_function(RTOSKernel* kernel_ptr, int sem_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; 

    kernel_ptr->log("  [Producer Task] Attempting to acquire semaphore.");
    kernel_ptr->semaphoreWait(sem_id); 

    // CRITICAL SECTION START (Semaphore protected)
    kernel_ptr->log("  [Producer Task] Acquired semaphore. Modifying shared_resource_sem...");
    shared_resource_sem++; 
    std::cout << "  [Producer Task] Shared resource (sem) value: " << shared_resource_sem << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->semaphoreSignal(sem_id); 
    kernel_ptr->log("  [Producer Task] Released semaphore.");
}

// Task that also acquires semaphore, decrements shared resource, then releases
void consumer_task_function(RTOSKernel* kernel_ptr, int sem_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; 

    kernel_ptr->log("  [Consumer Task] Attempting to acquire semaphore.");
    kernel_ptr->semaphoreWait(sem_id); 

    // CRITICAL SECTION START (Semaphore protected)
    kernel_ptr->log("  [Consumer Task] Acquired semaphore. Reading/Modifying shared_resource_sem...");
    if (shared_resource_sem > 0) {
        shared_resource_sem--;
    }
    std::cout << "  [Consumer Task] Shared resource (sem) value: " << shared_resource_sem << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->semaphoreSignal(sem_id); 
    kernel_ptr->log("  [Consumer Task] Released semaphore.");
}

// Task that acquires mutex, increments shared resource, then releases
void mutex_access_task_function(RTOSKernel* kernel_ptr, int mtx_id, const std::string& task_name) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; 

    kernel_ptr->log("  [" + task_name + "] Attempting to lock mutex.");
    kernel_ptr->mutexLock(mtx_id); 

    // CRITICAL SECTION START (Mutex protected)
    kernel_ptr->log("  [" + task_name + "] Locked mutex. Modifying shared_resource_mtx...");
    shared_resource_mtx++; 
    std::cout << "  [" + task_name + "] Shared resource (mtx) value: " << shared_resource_mtx << std::endl;
    // CRITICAL SECTION END

    kernel_ptr->mutexUnlock(mtx_id); 
    kernel_ptr->log("  [" + task_name + "] Unlocked mutex.");
}

// Task that sends messages to a message queue
void message_sender_task_function(RTOSKernel* kernel_ptr, int mq_id, int message_start) {
    if (kernel_ptr->getCurrentTask() == nullptr) return;

    kernel_ptr->log("  [Sender Task] Preparing to send messages.");
    for (int i = 0; i < 3; ++i) { // Try to send 3 messages
        int msg_to_send = message_start + i;
        kernel_ptr->log("  [Sender Task] Sending message: " + std::to_string(msg_to_send));
        kernel_ptr->messageQueueSend(mq_id, msg_to_send);
        kernel_ptr->delay(2); // Simulate some work/delay between sends
    }
    kernel_ptr->log("  [Sender Task] Finished sending messages.");
}

// Task that receives messages from a message queue
void message_receiver_task_function(RTOSKernel* kernel_ptr, int mq_id) {
    if (kernel_ptr->getCurrentTask() == nullptr) return;

    kernel_ptr->log("  [Receiver Task] Preparing to receive messages.");
    for (int i = 0; i < 3; ++i) { // Try to receive 3 messages
        int received_msg = -1;
        bool success = kernel_ptr->messageQueueReceive(mq_id, received_msg);
        if (success) {
            kernel_ptr->log("  [Receiver Task] Received message: " + std::to_string(received_msg));
        } else {
            kernel_ptr->log("  [Receiver Task] No message received (blocked or queue empty).");
        }
        kernel_ptr->delay(3); // Simulate some work/delay between receives
    }
    kernel_ptr->log("  [Receiver Task] Finished receiving messages.");
}


// A simple background task that doesn't use synchronization primitives
void background_task_function(RTOSKernel* kernel_ptr) {
    std::cout << "  [Background Task] Running independently. Current Tick: " << kernel_ptr->getCurrentTick() << std::endl;
}

// Task that uses the delay function
void delay_task_function(RTOSKernel* kernel_ptr, unsigned long delay_ticks) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; 

    kernel_ptr->log("  [Delay Task] Going to delay for " + std::to_string(delay_ticks) + " ticks. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
    kernel_ptr->delay(delay_ticks); 

    // This part of the code will only execute AFTER the delay has expired
    kernel_ptr->log("  [Delay Task] Delay finished. Resumed execution. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
}


int main() {
    // Create an RTOS Kernel instance
    RTOSKernel kernel; 

    // Create synchronization primitives
    shared_resource_semaphore_id = kernel.createSemaphore("SharedResourceSem", 1); 
    shared_resource_mutex_id = kernel.createMutex("SharedResourceMutex");
    // Create a message queue with a capacity of 2 messages
    communication_queue_id = kernel.createMessageQueue("CommQueue", 2); 

    // Create tasks with different names, priorities, and functions
    // Semaphore-using tasks
    kernel.createTask("Producer Task 1", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); 
    kernel.createTask("Consumer Task 1", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); 
    
    // Mutex-using tasks
    kernel.createTask("Mutex Task A", 9, [&]() { mutex_access_task_function(&kernel, shared_resource_mutex_id, "Mutex Task A"); }); 
    kernel.createTask("Mutex Task B", 9, [&]() { mutex_access_task_function(&kernel, shared_resource_mutex_id, "Mutex Task B"); }); 

    // Message Queue tasks
    kernel.createTask("Msg Sender", 7, [&]() { message_sender_task_function(&kernel, communication_queue_id, 100); }); // Sends 100, 101, 102
    kernel.createTask("Msg Receiver", 6, [&]() { message_receiver_task_function(&kernel, communication_queue_id); }); // Receives 3 messages

    // Background and Delay tasks
    kernel.createTask("Background Task", 5, [&]() { background_task_function(&kernel); });                         
    kernel.createTask("Delay Task 1", 7, [&]() { delay_task_function(&kernel, 5); });                                
    
    kernel.createTask("Producer Task 2", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); 
    kernel.createTask("Consumer Task 2", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); 
    kernel.createTask("Delay Task 2", 6, [&]() { delay_task_function(&kernel, 10); });                               


    // Start the RTOS scheduler simulation
    kernel.startScheduler();

    return 0;
}