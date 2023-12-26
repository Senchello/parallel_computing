#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

struct WordPosition {
    std::string filename;
    std::vector<int> positions;

    WordPosition(const std::string& file, int position)
        : filename(file), positions(1, position) {} // Initialize vector with one element
};

class InvertedIndex {
public:
    void add(const std::string& word, const std::string& filename, int position) {
        std::lock_guard<std::mutex> guard(mutex_);
        bool exists = false;
        for (WordPosition wpos : index_[word]) {
            if (filename == wpos.filename) {
                wpos.positions.push_back(position);
                exists = true;
                break;
            }
        }
        if (!exists) {
            WordPosition wpos_new(filename, position);
            index_[word].push_back(wpos_new);
        }
    }

    void printIndex() const {
        for (const auto& pair : index_) {
            std::cout << pair.first << " - ";
            for (const auto& wp : pair.second) {
                std::cout << "\t" << wp.filename << " [";
                for (int pos : wp.positions) {
                    std::cout << pos << " ";
                }
                std::cout << "];" << std::endl;
            }
            std::cout << std::endl;
        }
    }

    void printWordInfo(const std::string& word) const {
        auto it = index_.find(word);
        if (it != index_.end()) {
            std::cout << word << " - ";
            for (const auto& wp : it->second) {
                std::cout << "\t" << wp.filename << " [";
                for (int pos : wp.positions) {
                    std::cout << pos << " ";
                }
                std::cout << "];" << std::endl;
            }
            std::cout << std::endl;
        }
        else {
            std::cout << "Word '" << word << "' not found in index." << std::endl;
        }
    }

private:
    std::unordered_map<std::string, std::vector<WordPosition>> index_;
    mutable std::mutex mutex_;
};

class ThreadPool {
public:
    ThreadPool(size_t numThreads) {
        start(numThreads);
    }

    ~ThreadPool() {
        stop();
    }

    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::move(task));
        }
        condition.notify_one();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stopFlag;

    void start(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([=] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [=] { return stopFlag || !tasks.empty(); });
                        if (stopFlag && tasks.empty())
                            break;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    task();
                }
                });
        }
    }

    void stop() noexcept {
        stopFlag.store(true);
        condition.notify_all();
        for (auto& thread : workers) {
            if (thread.joinable())
                thread.join();
        }
    }
};

std::string normalizeWord(const std::string& word) {
    std::string normalized;
    for (char ch : word) {
        if ((int(ch) >= 0) && (std::isalpha(ch))) { // Check if the character is alphabetic
            normalized += std::tolower(ch); // Convert to lowercase
        }
    }
    return normalized;
}


void processFile(InvertedIndex& index, const std::string& file) {
    std::ifstream inFile(file);
    std::string word;
    int position = 0;
    while (inFile >> word) {
        std::string normalizedWord = normalizeWord(word);
        if (!normalizedWord.empty()) {
            index.add(normalizedWord, file, position);
        }
        ++position;
    }
}


int main(int argc, char* argv[]) {
    InvertedIndex index;
    std::vector<std::string> files;
    std::vector<std::thread> threads;
    int numThreads = 4;//std::thread::hardware_concurrency();

    // Read directory and file arguments
    std::string directoryPath = "../aclImdb/train/pos"; // Replace with actual directory path

    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    // Distribute files among threads
    ThreadPool pool(numThreads);

    for (const auto& file : files) {
        pool.enqueue([&index, file] { processFile(index, file); });
    }

    //pool.enqueue([&index] { index.printIndex(); });
    pool.enqueue([&index] { index.printWordInfo("their"); });

    return 0;
}
