#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>
#include <filesystem>
#include <chrono>
#include <unordered_map>

namespace fs = std::filesystem;

struct WordPosition {
    std::vector<std::pair<std::string, std::vector<int>>> occurences;
    std::mutex indexmut;
};

class InvertedIndex {
public:
    void add(const std::string& word, const std::string& filename, int position) {
        std::unique_lock<std::mutex> locklock(mutex_);
        auto& wpos = index_[word];
        locklock.unlock();

        // Lock for specific word position
        std::lock_guard<std::mutex> guard(wpos.indexmut);

        // Search and add occurrence
        auto it = std::find_if(wpos.occurences.begin(), wpos.occurences.end(),
            [&filename](const auto& occ) { return occ.first == filename; });
        if (it != wpos.occurences.end()) {
            it->second.push_back(position);
        }
        else {
            wpos.occurences.emplace_back(filename, std::vector<int>{position});
        }
    }

    void printIndex() const {
        for (const auto& pair : index_) {
            std::cout << pair.first << " - ";
            for (const auto& wp : pair.second.occurences) {
                std::cout << "\t" << wp.first << " [";
                for (int pos : wp.second) {
                    std::cout << pos << " ";
                }
                std::cout << "];" << std::endl;
            }
            std::cout << std::endl;
        }
    }

    void printWordInfo(const std::string& word) {
        if (index_.count(word) > 0) {
            std::cout << word << " - ";
            for (const auto& wp : index_[word].occurences) {
                std::cout << "\t" << wp.first << " [";
                for (int pos : wp.second) {
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

    std::string getWordInfo(const std::string& word) {
        std::stringstream ss;
        if (index_.count(word) > 0) {
            ss << word << " - ";
            for (const auto& wp : index_[word].occurences) {
                ss << "\t" << wp.first << " [";
                for (int pos : wp.second) {
                    ss << pos << " ";
                }
                ss << "];" << std::endl;
            }
        }
        else {
            ss << "Word '" << word << "' not found in index." << std::endl;
        }
        return ss.str();
    }

private:
    std::unordered_map<std::string, WordPosition> index_;
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
        if ((int(ch) >= 0) && (std::isalpha(ch))) {
            normalized += std::tolower(ch);
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

void scan(std::string dirPath, std::vector<std::string>* files) {
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            files->push_back(entry.path().string());
        }
    }
}


int main(int argc, char* argv[]) {
    InvertedIndex index;
    std::vector<std::string> files;
    std::vector<std::thread> threads;
    int numThreads = 16;

    scan("../aclImdb/train/pos", &files);
    scan("../aclImdb/train/neg", &files);
    scan("../aclImdb/test/pos", &files);
    scan("../aclImdb/test/neg", &files);
    scan("../aclImdb/train/unsup", &files);


    auto startTime = std::chrono::high_resolution_clock::now();
    ThreadPool pool(numThreads);

    for (const auto& file : files) {
        pool.enqueue([&index, file] { processFile(index, file); });
    }

    pool.~ThreadPool(); // Explicitly call the destructor to wait for tasks to complete.

    // Stop the timer
    auto endTime = std::chrono::high_resolution_clock::now();

    // Calculate the duration
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    //pool.enqueue([&index] { index.printIndex(); });
    //pool.enqueue([&index] { index.printWordInfo("their"); });

    //index.printIndex();
    //index.printWordInfo("their");

    std::cout << "Time taken to create inverted index with " << numThreads << " threads: " << duration.count() << " milliseconds" << std::endl;

    return 0;
}