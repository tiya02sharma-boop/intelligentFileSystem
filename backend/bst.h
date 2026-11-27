#pragma once

#include "fs_types.hpp"
#include <functional>
#include <string>
#include <vector>

class BSTNode {
public:
    std::string key;
    std::vector<Node*> items;
    BSTNode* left;
    BSTNode* right;

    BSTNode(const std::string& key, Node* item);
};

class NameBST {
private:
    BSTNode* root;
    size_t keyCount;

    BSTNode* removeKey(BSTNode* node, const std::string& key);
    void findPrefix(BSTNode* node, const std::string& prefix,
                    std::vector<Node*>& results, long long& comparisons) const;
    static int heightOf(const BSTNode* node);
    static void destroy(BSTNode* node);

public:
    NameBST();
    ~NameBST();
    NameBST(const NameBST&) = delete;
    NameBST& operator=(const NameBST&) = delete;

    size_t keys() const;
    int height() const;
    void insert(const std::string& key, Node* item);
    const BSTNode* find(const std::string& key, long long* comparisons = nullptr) const;
    void remove(const std::string& key, Node* item);
    std::vector<Node*> prefix(const std::string& text, long long& comparisons) const;
    void inorder(const std::function<void(const BSTNode*)>& visit) const;
};

// ------------------------------------------------------------------ Second BST: File Size Index
// Singly linked list of files sharing the exact same size (handles duplicate sizes)
struct FileListNode {
    Node* file;
    FileListNode* next;
    FileListNode(Node* f, FileListNode* nx = nullptr) : file(f), next(nx) {}
};

class SizeBSTNode {
public:
    long long size;             // Key: file size in bytes
    FileListNode* files;        // Linked list of files with this exact size
    SizeBSTNode* left;
    SizeBSTNode* right;

    SizeBSTNode(long long size, Node* file);
    ~SizeBSTNode();
};

class SizeBST {
private:
    SizeBSTNode* root;
    size_t keyCount;
    size_t fileCount;

    SizeBSTNode* removeKey(SizeBSTNode* node, long long size);
    void findRange(SizeBSTNode* node, long long minSize, long long maxSize,
                   std::vector<Node*>& results, long long& comparisons) const;
    static int heightOf(const SizeBSTNode* node);
    static void destroy(SizeBSTNode* node);

public:
    SizeBST();
    ~SizeBST();
    SizeBST(const SizeBST&) = delete;
    SizeBST& operator=(const SizeBST&) = delete;

    size_t keys() const;
    size_t totalFiles() const;
    int height() const;
    void insert(long long size, Node* file);
    const SizeBSTNode* find(long long size, long long* comparisons = nullptr) const;
    void remove(long long size, Node* file);
    void updateSize(Node* file, long long oldSize, long long newSize);
    std::vector<Node*> exact(long long size, long long& comparisons) const;
    std::vector<Node*> range(long long minSize, long long maxSize, long long& comparisons) const;
    void inorder(const std::function<void(const SizeBSTNode*)>& visit) const;
};
