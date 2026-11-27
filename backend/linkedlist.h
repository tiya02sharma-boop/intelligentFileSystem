#pragma once

#include "fs_types.hpp"
#include <string>

class LinkedList {
public:
    static Node* find(Node* directory, const std::string& name, long long* steps = nullptr);
    static void insert(Node* directory, Node* item);
    static void remove(Node* item);
};

class SortedStringList {
private:
    struct Item { std::string value; Item* next; };
    Item* first;

public:
    SortedStringList();
    ~SortedStringList();
    void insert(const std::string& value);
    bool contains(const std::string& value) const;
    void remove(const std::string& value);
};
