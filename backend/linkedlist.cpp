#include "linkedlist.h"

Node* LinkedList::find(Node* directory, const std::string& name, long long* steps) {
    for (Node* item = directory->firstChild; item; item = item->next) {
        if (steps) ++*steps;
        if (item->name == name) return item;
        if (item->name > name) return nullptr;
    }
    return nullptr;
}

void LinkedList::insert(Node* directory, Node* item) {
    Node* current = directory->firstChild;
    Node* previous = nullptr;

    while (current && current->name < item->name) {
        previous = current;
        current = current->next;
    }

    item->next = current;
    if (previous) previous->next = item;
    else directory->firstChild = item;
    item->parent = directory;
}

void LinkedList::remove(Node* item) {
    if (!item || !item->parent) return;
    Node* current = item->parent->firstChild;
    Node* previous = nullptr;
    while (current && current != item) {
        previous = current;
        current = current->next;
    }
    if (!current) return;
    if (previous) previous->next = item->next;
    else item->parent->firstChild = item->next;
    item->next = nullptr;
}

SortedStringList::SortedStringList() : first(nullptr) {}
SortedStringList::~SortedStringList() {
    while (first) { Item* old = first; first = first->next; delete old; }
}

void SortedStringList::insert(const std::string& value) {
    Item* current = first;
    Item* previous = nullptr;
    while (current && current->value < value) {
        previous = current;
        current = current->next;
    }
    Item* item = new Item{value, current};
    if (previous) previous->next = item;
    else first = item;
}

bool SortedStringList::contains(const std::string& value) const {
    for (Item* item = first; item; item = item->next) {
        if (item->value == value) return true;
        if (item->value > value) return false;
    }
    return false;
}

void SortedStringList::remove(const std::string& value) {
    Item* current = first;
    Item* previous = nullptr;
    while (current && current->value != value) {
        previous = current;
        current = current->next;
    }
    if (!current) return;
    if (previous) previous->next = current->next;
    else first = current->next;
    delete current;
}
