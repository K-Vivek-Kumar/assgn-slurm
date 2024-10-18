/*
* Code written by: K Vivek Kumar (CS21BTECH11026)
*/

// Libraries required
#include <iostream>
#include <pthread.h>
#include <atomic>
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <mutex>
#include <algorithm>
#include <iomanip>
#include <stdexcept>

using namespace std;

// Files involved in input and output
string inputFileName = "inp-params.txt";
string outputFileName = "out.txt";

// Returns random wait time in milliseconds whose mean is given
int getExponentialTime(double mean) {
    random_device rd;
    mt19937 gen(rd());
    exponential_distribution<> d(1.0 / mean);
    return static_cast<int>(d(gen) * 1000);
}

// Constants for having a log messages buffer size
const long long MAX_LOG_SIZE = 100000;

// Buffer definition
class LogEntry {
public:
    chrono::system_clock::time_point timestamp;
    string message;
};

// Output buffer for log messages
LogEntry outputBuffer[MAX_LOG_SIZE];
long long bufferIndex = 0;
mutex bufferMutex;

// Formatting the timestamp
string formatTimestamp(chrono::system_clock::time_point timestamp) {
    auto tt = chrono::system_clock::to_time_t(timestamp);
    auto milliseconds = chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;

    // Using stringstream to format the time correctly
    stringstream ss;
    tm local_tm;
    localtime_r(&tt, &local_tm);
    ss << setfill('0') << setw(2) << local_tm.tm_hour << ":"
       << setw(2) << local_tm.tm_min << ":"
       << setw(2) << local_tm.tm_sec << ":"
       << setw(3) << milliseconds.count();
    return ss.str();
}

// Function to write log messages to the buffer with a timestamp
void logMessage(const string& message) {
    lock_guard<mutex> guard(bufferMutex);
    if (bufferIndex < MAX_LOG_SIZE) {
        outputBuffer[bufferIndex].timestamp = chrono::system_clock::now();
        outputBuffer[bufferIndex].message = message;
        bufferIndex++;
    }
}

// Pointer to the MRMW-Snap Registers
atomic<int>* registers;
atomic<bool> term(false);

// ThreadData for putting the thread's data for the writer and snapshot threads
class ThreadData {
public:
    int id;
    double mu_w;
    double mu_s;
    int k;
    int M;

    ThreadData(int id, double mu_w, double mu_s, int k, int M) {
        this->id = id;
        this->mu_w = mu_w;
        this->mu_s = mu_s;
        this->k = k;
        this->M = M;
    }
};

// Writer thread function
void* writer(void* arg) {
    // Extracting out the data from the arg
    ThreadData* args = (ThreadData*) arg;
    int id = args->id;
    double mu_w = args->mu_w;
    int M = args->M;

    while (!term) {
        int v = rand() % 100;  // Random value to write
        int l = rand() % M;  // Random place to write

        // update(l, v)
        registers[l].store(v, memory_order_relaxed);

        // Writing the log message
        stringstream ss;
        ss << "Thread " << id << " wrote " << v << " on location " << l << " at " 
           << formatTimestamp(chrono::system_clock::now()) << "\n";
        
        // Pushing it correctly into the log message buffers
        logMessage(ss.str());

        // Sleep for some random writer thread's time value
        usleep(getExponentialTime(mu_w) * 1000);
    }

    return nullptr;
}

// Obstruction-Free Snapshot function
int* obstructionFreeSnapshot(int M) {
    int* snapshot = new int[M];
    for (int i = 0; i < M; ++i) {
        // Attempt to read the value from the register
        snapshot[i] = registers[i].load(memory_order_acquire);
    }
    return snapshot;
}

// Snapshot thread function
void* snapshotCollector(void* arg) {
    // Extracting out the data from the arg
    ThreadData* args = (ThreadData*) arg;
    int id = args->id;
    double mu_s = args->mu_s;
    int k = args->k;
    int M = args->M;

    for (int i = 0; i < k; ++i) {
        auto beginCollect = chrono::high_resolution_clock::now();

        // Calling the snapshot collecting function
        int* snapshot = obstructionFreeSnapshot(M);

        auto endCollect = chrono::high_resolution_clock::now();
        chrono::duration<double> timeCollect = endCollect - beginCollect;

        // Writing a log message
        stringstream ss;
        ss << "Thread " << id << "'s snapshot: ";
        for (int j = 0; j < M; ++j) {
            ss << "L" << j + 1 << "-" << snapshot[j] << " ";
        }
        ss << "which finished in " << timeCollect.count() << " seconds at "
           << formatTimestamp(chrono::system_clock::now()) << "\n";
        logMessage(ss.str());

        // Sleep for some random snapshot thread's time value
        usleep(getExponentialTime(mu_s) * 1000);

        // Immediately freeing the snapshot collected
        delete[] snapshot;
    }

    return nullptr;
}

// Main Function Execution
int main() {
    // Input parameters
    int nw, ns, M, k;
    double mu_w, mu_s;

    try {
        // Reading parameters from the file
        ifstream inputFile(inputFileName);

        if (!inputFile) {
            throw runtime_error("Unable to open input file");
        }

        inputFile >> nw >> ns >> M >> mu_w >> mu_s >> k;

        if (inputFile.fail()) {
            throw runtime_error("Error reading parameters from file");
        }

        inputFile.close();
    } catch (const exception& e) {
        cerr << e.what() << '\n';
        return 1;
    }

    // Assign the size of M registers and assign the MRMW-Snap pointer to this.
    registers = new atomic<int>[M];
    for (int i = 0; i < M; ++i) {
        registers[i].store(0);
    }

    // Initializing the writer threads and snapshot threads array.
    pthread_t writerThreads[nw], snapshotThreads[ns];

    // Assigning the threads data
    ThreadData* writerArgs[nw];
    ThreadData* snapshotArgs[ns];

    // Start time
    auto startTime = chrono::high_resolution_clock::now();

    // Creating the writer threads data with the initial values
    for (int i = 0; i < nw; ++i) {
        writerArgs[i] = new ThreadData(i, mu_w, 0, 0, M);
        pthread_create(&writerThreads[i], nullptr, writer, writerArgs[i]);
    }

    // Creating the snapshot threads data with the initial values
    for (int i = 0; i < ns; ++i) {
        snapshotArgs[i] = new ThreadData(i, 0, mu_s, k, M);
        pthread_create(&snapshotThreads[i], nullptr, snapshotCollector, snapshotArgs[i]);
    }

    // Waiting for snapshot threads to finish
    for (int i = 0; i < ns; ++i) {
        pthread_join(snapshotThreads[i], nullptr);
    }

    // Inform all the writer threads that they have to terminate
    term.store(true);

    // Wait for writer threads to finish
    for (int i = 0; i < nw; ++i) {
        pthread_join(writerThreads[i], nullptr);
    }

    // End time
    auto endTime = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = endTime - startTime;

    // Time taken
    cout << duration.count() << endl;

    // Sort the buffer by timestamp
    sort(outputBuffer, outputBuffer + bufferIndex,
        [](const LogEntry& a, const LogEntry& b) {
            return a.timestamp < b.timestamp;
        });

    // Write sorted output to out.txt
    ofstream outputFile(outputFileName);
    for (long long i = 0; i < bufferIndex; ++i) {
        outputFile << outputBuffer[i].message;
    }
    outputFile.close();

    // Cleanup the dynamically allocated arrays
    for (int i = 0; i < nw; ++i) {
        delete writerArgs[i];
    }
    for (int i = 0; i < ns; i++) {
        delete snapshotArgs[i];
    }
    delete[] registers;

    return 0;
}
