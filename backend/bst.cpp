#include "bst.h"
#include <algorithm>

BSTNode::BSTNode(const std::string& name, Node* item)
    : key(name), items{item}, left(nullptr), right(nullptr) {}

NameBST::NameBST() : root(nullptr), keyCount(0) {}
NameBST::~NameBST() { destroy(root); }

size_t NameBST::keys() const { return keyCount; }
int NameBST::height() const { return heightOf(root); }

void NameBST::insert(const std::string& key, Node* item) {
    if (!root) {
        root = new BSTNode(key, item);
        ++keyCount;
        return;
    }

    BSTNode* current = root;
    BSTNode* parent = nullptr;
    while (current) {
        parent = current;
        if (key == current->key) { current->items.push_back(item); return; }
        current = key < current->key ? current->left : current->right;
    }

    BSTNode* added = new BSTNode(key, item);
    if (key < parent->key) parent->left = added;
    else parent->right = added;
    ++keyCount;
}

const BSTNode* NameBST::find(const std::string& key, long long* comparisons) const {
    const BSTNode* node = root;
    while (node) {
        if (comparisons) ++*comparisons;
        if (key == node->key) return node;
        node = key < node->key ? node->left : node->right;
    }
    return nullptr;
}

void NameBST::remove(const std::string& key, Node* item) {
    const BSTNode* found = find(key);
    if (!found) return;
    BSTNode* node = const_cast<BSTNode*>(found);
    auto& items = node->items;
    items.erase(std::remove(items.begin(), items.end(), item), items.end());
    if (items.empty()) root = removeKey(root, key);
}

BSTNode* NameBST::removeKey(BSTNode* node, const std::string& key) {
    if (!node) return nullptr;
    if (key < node->key) node->left = removeKey(node->left, key);
    else if (key > node->key) node->right = removeKey(node->right, key);
    else {
        if (!node->left) { BSTNode* next = node->right; delete node; --keyCount; return next; }
        if (!node->right) { BSTNode* next = node->left; delete node; --keyCount; return next; }
        BSTNode* next = node->right;
        while (next->left) next = next->left;
        node->key = next->key;
        node->items = next->items;
        node->right = removeKey(node->right, next->key);
    }
    return node;
}

void NameBST::findPrefix(BSTNode* node, const std::string& prefix,
                         std::vector<Node*>& results, long long& comparisons) const {
    if (!node) return;
    ++comparisons;
    int order = node->key.compare(0, prefix.size(), prefix);
    if (order >= 0) findPrefix(node->left, prefix, results, comparisons);
    if (order == 0) results.insert(results.end(), node->items.begin(), node->items.end());
    if (order <= 0) findPrefix(node->right, prefix, results, comparisons);
}

std::vector<Node*> NameBST::prefix(const std::string& text, long long& comparisons) const {
    std::vector<Node*> results;
    findPrefix(root, text, results, comparisons);
    return results;
}

int NameBST::heightOf(const BSTNode* node) {
    if (!node) return 0;
    return 1 + std::max(heightOf(node->left), heightOf(node->right));
}

void NameBST::inorder(const std::function<void(const BSTNode*)>& visit) const {
    std::vector<const BSTNode*> stack;
    const BSTNode* node = root;
    while (node || !stack.empty()) {
        while (node) { stack.push_back(node); node = node->left; }
        node = stack.back();
        stack.pop_back();
        visit(node);
        node = node->right;
    }
}

void NameBST::destroy(BSTNode* node) {
    if (!node) return;
    std::vector<BSTNode*> stack{node};
    while (!stack.empty()) {
        BSTNode* current = stack.back();
        stack.pop_back();
        if (current->left) stack.push_back(current->left);
        if (current->right) stack.push_back(current->right);
        delete current;
    }
}

// ------------------------------------------------------------------ Second BST: File Size Index
SizeBSTNode::SizeBSTNode(long long s, Node* file)
    : size(s), files(new FileListNode(file)), left(nullptr), right(nullptr) {}

SizeBSTNode::~SizeBSTNode() {
    for (FileListNode* cur = files; cur;) {
        FileListNode* nx = cur->next;
        delete cur;
        cur = nx;
    }
}

SizeBST::SizeBST() : root(nullptr), keyCount(0), fileCount(0) {}
SizeBST::~SizeBST() { destroy(root); }

size_t SizeBST::keys() const { return keyCount; }
size_t SizeBST::totalFiles() const { return fileCount; }
int SizeBST::height() const { return heightOf(root); }

void SizeBST::insert(long long size, Node* file) {
    if (!root) {
        root = new SizeBSTNode(size, file);
        ++keyCount;
        ++fileCount;
        return;
    }

    SizeBSTNode* current = root;
    SizeBSTNode* parent = nullptr;
    while (current) {
        parent = current;
        if (size == current->size) {
            // Duplicate size: prepend to the linked list of files at this node
            current->files = new FileListNode(file, current->files);
            ++fileCount;
            return;
        }
        current = size < current->size ? current->left : current->right;
    }

    SizeBSTNode* added = new SizeBSTNode(size, file);
    if (size < parent->size) parent->left = added;
    else parent->right = added;
    ++keyCount;
    ++fileCount;
}

const SizeBSTNode* SizeBST::find(long long size, long long* comparisons) const {
    const SizeBSTNode* node = root;
    while (node) {
        if (comparisons) ++*comparisons;
        if (size == node->size) return node;
        node = size < node->size ? node->left : node->right;
    }
    return nullptr;
}

std::vector<Node*> SizeBST::exact(long long size, long long& comparisons) const {
    std::vector<Node*> out;
    const SizeBSTNode* node = find(size, &comparisons);
    if (node) {
        for (FileListNode* cur = node->files; cur; cur = cur->next) {
            out.push_back(cur->file);
        }
    }
    return out;
}

void SizeBST::remove(long long size, Node* file) {
    const SizeBSTNode* found = find(size);
    if (!found) return;
    SizeBSTNode* node = const_cast<SizeBSTNode*>(found);

    FileListNode* prev = nullptr;
    for (FileListNode* cur = node->files; cur; prev = cur, cur = cur->next) {
        if (cur->file == file) {
            if (prev) prev->next = cur->next;
            else node->files = cur->next;
            delete cur;
            --fileCount;
            break;
        }
    }

    if (!node->files) {
        root = removeKey(root, size);
    }
}

SizeBSTNode* SizeBST::removeKey(SizeBSTNode* node, long long size) {
    if (!node) return nullptr;
    if (size < node->size) node->left = removeKey(node->left, size);
    else if (size > node->size) node->right = removeKey(node->right, size);
    else {
        if (!node->left) {
            SizeBSTNode* next = node->right;
            node->right = nullptr;
            delete node;
            --keyCount;
            return next;
        }
        if (!node->right) {
            SizeBSTNode* next = node->left;
            node->left = nullptr;
            delete node;
            --keyCount;
            return next;
        }
        // Two children: find in-order successor (minimum in right subtree)
        SizeBSTNode* next = node->right;
        while (next->left) next = next->left;
        node->size = next->size;
        // Swap file list to preserve ownership
        std::swap(node->files, next->files);
        node->right = removeKey(node->right, next->size);
    }
    return node;
}

void SizeBST::updateSize(Node* file, long long oldSize, long long newSize) {
    remove(oldSize, file);
    insert(newSize, file);
}

// Range query with subtree pruning
void SizeBST::findRange(SizeBSTNode* node, long long minSize, long long maxSize,
                        std::vector<Node*>& results, long long& comparisons) const {
    if (!node) return;
    ++comparisons;

    // Prune left subtree if current node's size <= minSize (all left keys are < node->size <= minSize)
    if (node->size > minSize) {
        findRange(node->left, minSize, maxSize, results, comparisons);
    }

    // Current node falls in range [minSize, maxSize]
    if (node->size >= minSize && node->size <= maxSize) {
        for (FileListNode* cur = node->files; cur; cur = cur->next) {
            results.push_back(cur->file);
        }
    }

    // Prune right subtree if current node's size >= maxSize (all right keys are > node->size >= maxSize)
    if (node->size < maxSize) {
        findRange(node->right, minSize, maxSize, results, comparisons);
    }
}

std::vector<Node*> SizeBST::range(long long minSize, long long maxSize, long long& comparisons) const {
    std::vector<Node*> results;
    findRange(root, minSize, maxSize, results, comparisons);
    return results;
}

int SizeBST::heightOf(const SizeBSTNode* node) {
    if (!node) return 0;
    return 1 + std::max(heightOf(node->left), heightOf(node->right));
}

void SizeBST::inorder(const std::function<void(const SizeBSTNode*)>& visit) const {
    std::vector<const SizeBSTNode*> stack;
    const SizeBSTNode* node = root;
    while (node || !stack.empty()) {
        while (node) { stack.push_back(node); node = node->left; }
        node = stack.back();
        stack.pop_back();
        visit(node);
        node = node->right;
    }
}

void SizeBST::destroy(SizeBSTNode* node) {
    if (!node) return;
    std::vector<SizeBSTNode*> stack{node};
    while (!stack.empty()) {
        SizeBSTNode* current = stack.back();
        stack.pop_back();
        if (current->left) stack.push_back(current->left);
        if (current->right) stack.push_back(current->right);
        delete current;
    }
}
