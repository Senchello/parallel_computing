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
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

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
        std::cout << ss.str() << std::endl << ss.str().size() << std::endl;
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

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, InvertedIndex& index)
        : socket_(std::move(socket)), index_(index) {}

    void start() {
        doRead();
    }

private:
    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
    InvertedIndex& index_;

    void doRead() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    std::string word(data_, length);
                    // Ensure we only read the word up to the newline
                    auto endOfWord = word.find('\n');
                    if (endOfWord != std::string::npos) {
                        word.erase(endOfWord);
                    }
                    // Now we get the word info and initiate an asynchronous write
                    auto info = std::make_shared<std::string>(index_.getWordInfo(word));
                    doWrite(info);
                }
                else {
                    // Handle the error, for example, by logging or cleaning up the session
                }
            });
    }

    void doWrite(std::shared_ptr<std::string> msg) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(*msg),
            [this, self, msg](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    // If you want to read again, uncomment the next line
                    doRead();
                }
                else {
                    // Handle the error, for example, by logging or cleaning up the session
                }
            });
    }
};


class Server {
public:
    Server(asio::io_context& io_context, short port, InvertedIndex& index, ThreadPool& pool)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), index_(index), pool_(pool) {
        doAccept();
    }

private:
    void doAccept() {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                pool_.enqueue([s = std::make_shared<Session>(std::move(socket), index_)]() { s->start(); });
            }
            doAccept();
            });
    }

    tcp::acceptor acceptor_;
    InvertedIndex& index_;
    ThreadPool& pool_;
};


int main(int argc, char* argv[]) {
    try {
        asio::io_context io_context;
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

        ThreadPool cliPool(4);  // Adjust the number of threads as needed

        Server server(io_context, 1234 /* port */, index, cliPool);
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}