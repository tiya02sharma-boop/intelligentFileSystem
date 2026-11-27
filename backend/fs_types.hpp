#pragma once

#include <string>
#include <utility>

// One file or directory in the filesystem tree.
// Siblings are connected through next and kept in name order.
struct Node {
    std::string name;
    bool isDir;
    long long size;
    Node* parent;
    Node* firstChild;
    Node* next;

    Node(std::string name, bool isDir, long long size, Node* parent)
        : name(std::move(name)), isDir(isDir), size(size), parent(parent),
          firstChild(nullptr), next(nullptr) {}
};

struct Stats {
    long long files = 0;
    long long dirs = 0;
    long long size = 0;
    int depth = 0;
};

struct TItem {
    const Node* n;
    int depth;
};
