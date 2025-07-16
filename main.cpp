#include "rtos_kernel.hpp"
#include <thread> // For std::this_thread::sleep_for (simulating time delays)
#include <chrono> // For std::chrono::milliseconds

// --- Virtual Task Functions ---
// These functions simulate the work done by real tasks.
void task1_function() {
    std::cout << "  [Task 1] Hello from Task 1! (High Priority)" << std::endl;
    // In a real task, this would involve sensor readings, calculations, etc.
    // std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Simulate work
}

void task2_function() {
    std::cout << "  [Task 2] Task 2 is running. (Medium Priority)" << std::endl;
    // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work
}

void task3_function() {
    std::cout << "  [Task 3] This is Task 3. (Low Priority)" << std::endl;
    // std::this_thread::sleep_for(std::chrono::milliseconds(70)); // Simulate work
}

void task4_function() {
    std::cout << "  [Task 4] Another Medium Priority Task." << std::endl;
    // std::this_thread::sleep_for(std::chrono::milliseconds(80)); // Simulate work
}

int main() {
    // Create an RTOS Kernel instance
    RTOSKernel kernel;

    // Create tasks with different names, priorities, and functions
    // Higher priority value means higher priority.
    kernel.createTask("Task A", 10, task1_function); // High priority
    kernel.createTask("Task B", 5, task2_function);  // Medium priority
    kernel.createTask("Task C", 1, task3_function);  // Low priority
    kernel.createTask("Task D", 5, task4_function);  // Another medium priority task

    // Start the RTOS scheduler simulation
    kernel.startScheduler();

    return 0;
}