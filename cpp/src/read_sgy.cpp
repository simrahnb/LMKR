#include <iostream>
#include <fstream> // for reading files 

int main() {
    // Open the SEG-Y file in binary mode
    std::ifstream file("../data/1x1.sgy", std::ios::binary);

    // Check if file opened successfully 
    if (!file.is_open()) {
        std::cerr << "Error: Could not open SEG-Y file.\n";
        return 1; // return with error code 
    }

    // 1: Read the 3200-byte Text Header === 
    char textHeader[3200]; 
    file.read(textHeader, 3200); // read first 3200 bytes into array

    std::cout << "=== Text Header (first 200 characters) ===\n";
    std::cout.write(textHeader, 200); // print only first 200 chars
    std::cout << "\n\n"; 

    // 2: Read the 400-byte Binary Header === 
    unsigned char binaryHeader[400]; 
    file.read(reinterpret_cast<char*>(binaryHeader), 400);

    int sampleInterval = (binaryHeader[16] << 8) | binaryHeader[17];

    int numSamples = (binaryHeader[20] << 8) | binaryHeader[21];

    std::cout << "=== Binary header Info ===\n";
    std::cout << "Sample Interval (microseconds): " << sampleInterval << "\n";
    std::cout << "Samples per Trace: " << numSamples << "\n";

    // === STEP 3: Close File ===
    file.close();

    return 0;

}