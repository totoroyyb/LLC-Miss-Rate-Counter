#include <iostream>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <thread>

#define NUM_ELEMENTS 10000000 // Number of double elements to allocate

int main() {
    std::srand(std::time(nullptr));  // Seed random number generator

    // Infinite loop to continuously use memory and CPU
    while (true) {
        std::vector<double> data(NUM_ELEMENTS);

        // Fill the vector with random doubles
        for (double &value : data) {
            value = static_cast<double>(std::rand()) / RAND_MAX;
        }

        // Process the data in some way: here, we calculate the average
        double sum = 0.0;
        for (const double &value : data) {
            sum += value;
        }
        double average = sum / data.size();

        // Output the average - this also confirms the program is running
        std::cout << "Average of array elements: " << average << std::endl;

        // Simulate some delay (comment out if real intensive usage is needed)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0; // This line will never be reached
}
