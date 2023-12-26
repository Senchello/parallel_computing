#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
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
            std::cout << word << " — ";
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

std::string normalizeWord(const std::string& word) {
    std::string normalized;
    for (char ch : word) {
        if ((int(ch) >= 0) && (std::isalpha(ch))) { // Check if the character is alphabetic
            normalized += std::tolower(ch); // Convert to lowercase
        }
    }
    return normalized;
}

void processFiles(InvertedIndex& index, const std::vector<std::string>& files) {
    for (const auto& file : files) {
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
}

int main(int argc, char* argv[]) {
    InvertedIndex index;
    std::vector<std::string> files;
    std::vector<std::thread> threads;
    int numThreads = 1;//std::thread::hardware_concurrency();

    // Read directory and file arguments
    std::string directoryPath = "../aclImdb/train/pos"; // Replace with actual directory path

    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    // Distribute files among threads
    int filesPerThread = files.size() / numThreads;
    int startIndex = 0;
    for (int i = 0; i < numThreads; ++i) {
        int endIndex = startIndex + filesPerThread;
        if (i == numThreads - 1) {
            endIndex = files.size();
        }

        std::vector<std::string> subset(files.begin() + startIndex, files.begin() + endIndex);
        threads.emplace_back(processFiles, std::ref(index), subset);
        startIndex = endIndex;
    }

    // Join threads
    for (auto& t : threads) {
        t.join();
    }

    index.printIndex();
    //index.printWordInfo("white");

    return 0;
}
