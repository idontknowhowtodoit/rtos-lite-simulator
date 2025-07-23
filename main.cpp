#include "rtos_kernel.hpp"
#include <thread> // For std::this_thread::sleep_for (simulating time delays)
#include <chrono> // For std::chrono::milliseconds
#include <atomic> // For std::atomic_int (to simulate shared resource)
#include <sstream> // For std::ostringstream

// Global (shared) resources, protected by synchronization primitives
std::atomic_int shared_resource_sem = 0; // Protected by semaphore
std::atomic_int shared_resource_mtx = 0; // Protected by mutex
std::atomic_int timer_callback_count = 0; // For timer callback demo

// IDs for synchronization primitives
int shared_resource_semaphore_id = -1; 
int shared_resource_mutex_id = -1;
int event_flag_group_id = -1; // ID for the event flag group
int message_queue_id = -1; // ID for the message queue
int callback_timer_id = -1; // ID for the callback timer
int event_timer_id = -1;    // ID for the event-setting timer

// Define specific event flags as bitmasks
const unsigned int EVENT_FLAG_A = 0x01; // Bit 0
const unsigned int EVENT_FLAG_B = 0x02; // Bit 1
const unsigned int EVENT_FLAG_C = 0x04; // Bit 2
const unsigned int EVENT_FLAG_D = 0x08; // Bit 3
const unsigned int EVENT_FLAG_TIMER_DONE = 0x10; // Bit 4 for timer event

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

// Task that waits for specific event flags (WAIT_ALL mode)
void event_wait_all_task_function(RTOSKernel* kernel_ptr, int ef_id, unsigned int flags_to_wait) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Event Wait ALL Task] Waiting for flags 0x" + std::hex + flags_to_wait + std::dec + " (WAIT_ALL).");
    bool success = kernel_ptr->eventFlagWait(ef_id, flags_to_wait, WAIT_ALL);
    
    if (success) {
        kernel_ptr->log("  [Event Wait ALL Task] Successfully received all required flags! Continuing execution.");
    } else {
        // This path is taken if the task was blocked. It will resume here when unblocked.
        kernel_ptr->log("  [Event Wait ALL Task] Resumed after waiting for flags. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
    }
}

// Task that waits for specific event flags (WAIT_ANY mode)
void event_wait_any_task_function(RTOSKernel* kernel_ptr, int ef_id, unsigned int flags_to_wait) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Event Wait ANY Task] Waiting for flags 0x" + std::hex + flags_to_wait + std::dec + " (WAIT_ANY).");
    bool success = kernel_ptr->eventFlagWait(ef_id, flags_to_wait, WAIT_ANY);
    
    if (success) {
        kernel_ptr->log("  [Event Wait ANY Task] Successfully received any required flag! Continuing execution.");
    } else {
        // This path is taken if the task was blocked. It will resume here when unblocked.
        kernel_ptr->log("  [Event Wait ANY Task] Resumed after waiting for flags. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
    }
}

// Task that sets event flags after a delay
void event_setter_task_function(RTOSKernel* kernel_ptr, int ef_id, unsigned int flags_to_set, unsigned long delay_ticks) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Event Setter Task] Will set flags 0x" + std::hex + flags_to_set + std::dec + " after " + std::to_string(delay_ticks) + " ticks.");
    kernel_ptr->delay(delay_ticks); // Delay before setting flags
    
    kernel_ptr->eventFlagSet(ef_id, flags_to_set);
    kernel_ptr->log("  [Event Setter Task] Flags 0x" + std::hex + flags_to_set + std::dec + " set. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
}

// Task that clears event flags after a delay
void event_clearer_task_function(RTOSKernel* kernel_ptr, int ef_id, unsigned int flags_to_clear, unsigned long delay_ticks) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    kernel_ptr->log("  [Event Clearer Task] Will clear flags 0x" + std::hex + flags_to_clear + std::dec + " after " + std::to_string(delay_ticks) + " ticks.");
    kernel_ptr->delay(delay_ticks); // Delay before clearing flags
    
    kernel_ptr->eventFlagClear(ef_id, flags_to_clear);
    kernel_ptr->log("  [Event Clearer Task] Flags 0x" + std::hex + flags_to_clear + std::dec + " cleared. Current Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
}

// Task that sends messages to the message queue
void message_sender_task_function(RTOSKernel* kernel_ptr, int mq_id, int num_messages, unsigned long delay_between_sends) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    for (int i = 0; i < num_messages; ++i) {
        std::ostringstream oss;
        oss << "Message " << i << " from " << kernel_ptr->getCurrentTask()->name;
        std::string message = oss.str();

        kernel_ptr->log("  [Message Sender Task] Trying to send: '" + message + "'");
        bool sent = kernel_ptr->messageQueueSend(mq_id, message);
        if (sent) {
            kernel_ptr->log("  [Message Sender Task] Successfully sent: '" + message + "'");
        } else {
            kernel_ptr->log("  [Message Sender Task] Blocked while sending message. Will retry when unblocked.");
            // Task was blocked, it will resume from here when unblocked.
            // We need to re-attempt sending the same message.
            // For simplicity in this simulation, we'll just break and let the scheduler pick another task.
            // In a real RTOS, the task would typically retry the send operation.
            break; 
        }
        if (delay_between_sends > 0) {
            kernel_ptr->delay(delay_between_sends);
        }
    }
    kernel_ptr->log("  [Message Sender Task] Finished sending messages.");
}

// Task that receives messages from the message queue
void message_receiver_task_function(RTOSKernel* kernel_ptr, int mq_id, int num_messages) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check

    for (int i = 0; i < num_messages; ++i) {
        std::string received_message;
        kernel_ptr->log("  [Message Receiver Task] Trying to receive message.");
        bool received = kernel_ptr->messageQueueReceive(mq_id, received_message);
        if (received) {
            kernel_ptr->log("  [Message Receiver Task] Successfully received: '" + received_message + "'");
        } else {
            kernel_ptr->log("  [Message Receiver Task] Blocked while receiving message. Will retry when unblocked.");
            // Task was blocked, it will resume from here when unblocked.
            // For simplicity, we'll just break.
            break;
        }
    }
    kernel_ptr->log("  [Message Receiver Task] Finished receiving messages.");
}

// A task that just executes periodically
void periodic_task_function(RTOSKernel* kernel_ptr) {
    if (kernel_ptr->getCurrentTask() == nullptr) return; // Safety check
    kernel_ptr->log("  [" + kernel_ptr->getCurrentTask()->name + "] is running periodically at Tick: " + std::to_string(kernel_ptr->getCurrentTick()));
}

// Callback function for a software timer
void my_timer_callback() {
    timer_callback_count++;
    std::cout << "[RTOS Log] *** Timer Callback executed! Count: " << timer_callback_count << " ***" << std::endl;
}


int main() {
    // Create an RTOS Kernel instance
    RTOSKernel kernel; 

    // Create synchronization primitives
    shared_resource_semaphore_id = kernel.createSemaphore("SharedResourceSem", 1); 
    shared_resource_mutex_id = kernel.createMutex("SharedResourceMutex");
    event_flag_group_id = kernel.createEventFlag("MyEventFlags");
    message_queue_id = kernel.createMessageQueue("MyMessages", 3); // Max 3 messages in queue

    // Create tasks
    // Semaphore-using tasks
    kernel.createTask("Producer Task 1", 10, [&]() { producer_task_function(&kernel, shared_resource_semaphore_id); }); 
    kernel.createTask("Consumer Task 1", 8, [&]() { consumer_task_function(&kernel, shared_resource_semaphore_id); }); 
    
    // Mutex-using tasks
    kernel.createTask("Mutex Task A", 9, [&]() { mutex_access_task_function(&kernel, shared_resource_mutex_id, "Mutex Task A"); }); 
    kernel.createTask("Mutex Task B", 9, [&]() { mutex_access_task_function(&kernel, shared_resource_mutex_id, "Mutex Task B"); }); 

    // Event Flag tasks
    // This task waits for A and B (both must be set)
    kernel.createTask("Event Wait ALL", 7, [&]() { event_wait_all_task_function(&kernel, event_flag_group_id, EVENT_FLAG_A | EVENT_FLAG_B); }); 
    // This task waits for C or D (either can be set)
    kernel.createTask("Event Wait ANY", 7, [&]() { event_wait_any_task_function(&kernel, event_flag_group_id, EVENT_FLAG_C | EVENT_FLAG_D); }); 
    // This task sets flag A after 3 ticks
    kernel.createTask("Event Setter A", 6, [&]() { event_setter_task_function(&kernel, event_flag_group_id, EVENT_FLAG_A, 3); }); 
    // This task sets flag B after 7 ticks
    kernel.createTask("Event Setter B", 6, [&]() { event_setter_task_function(&kernel, event_flag_group_id, EVENT_FLAG_B, 7); }); 
    // This task sets flag C after 5 ticks
    kernel.createTask("Event Setter C", 6, [&]() { event_setter_task_function(&kernel, event_flag_group_id, EVENT_FLAG_C, 5); }); 
    // This task clears flag A after 15 ticks (after Event Wait ALL might have consumed it)
    kernel.createTask("Event Clearer A", 5, [&]() { event_clearer_task_function(&kernel, event_flag_group_id, EVENT_FLAG_A, 15); }); 

    // Message Queue tasks
    kernel.createTask("MQ Sender 1", 8, [&]() { message_sender_task_function(&kernel, message_queue_id, 5, 2); }); // Send 5 messages, 2 ticks delay
    kernel.createTask("MQ Receiver 1", 8, [&]() { message_receiver_task_function(&kernel, message_queue_id, 5); }); // Receive 5 messages

    // Periodic tasks
    // Task that runs every 10 ticks
    kernel.createTask("Periodic Task 10T", 4, [&]() { periodic_task_function(&kernel); }, 10);
    // Task that runs every 15 ticks
    kernel.createTask("Periodic Task 15T", 4, [&]() { periodic_task_function(&kernel); }, 15);

    // Software Timers
    // One-shot timer that calls a callback after 8 ticks
    callback_timer_id = kernel.createTimer("Callback Timer", 8, my_timer_callback);
    // Periodic timer that sets EVENT_FLAG_TIMER_DONE every 12 ticks
    event_timer_id = kernel.createTimer("Event Flag Timer", 12, event_flag_group_id, EVENT_FLAG_TIMER_DONE);

    // Task that waits for the event flag set by the timer
    kernel.createTask("Timer Event Waiter", 7, [&]() { 
        if (kernel.getCurrentTask() == nullptr) return;
        kernel.log("  [Timer Event Waiter] Waiting for EVENT_FLAG_TIMER_DONE from timer.");
        kernel.eventFlagWait(event_flag_group_id, EVENT_FLAG_TIMER_DONE, WAIT_ANY);
        kernel.log("  [Timer Event Waiter] EVENT_FLAG_TIMER_DONE received! Continuing execution.");
    });


    // Background and Delay tasks
    kernel.createTask("Background Task", 5, [&]() { background_task_function(&kernel); });                         
    kernel.createTask("Delay Task 1", 7, [&]() { delay_task_function(&kernel, 5); });                                
    kernel.createTask("Delay Task 2", 6, [&]() { delay_task_function(&kernel, 10); });                               


    // Start some timers initially
    kernel.startTimer(callback_timer_id);
    kernel.startTimer(event_timer_id);

    // Start the RTOS scheduler simulation
    kernel.startScheduler();

    return 0;
}