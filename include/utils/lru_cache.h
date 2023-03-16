#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <unordered_map>
#include <string>
#include "lock/locker.h"

// template<typename T1, typename T2>
class Node {
public:
    Node() {
        this->next = nullptr;
        this->prev = nullptr;
    }
    Node(const char* v, std::string i, long l): val(v), index(i), len(l) {
        this->next = nullptr;
        this->prev = nullptr;
    }
    ~Node() {
        this->next = nullptr;
        this->prev = nullptr;
    }
public:
    Node* next;
    Node* prev;
    const char* val;
    long len;
    std::string index;
};

// template<typename T1, typename T2>
class LRUCache {
public:
    LRUCache(int capacity);
    void updateCache(Node*& cur);
    void insertNode(Node*& node);
    void put(std::string key, const char* value, long len);
    long getValSize(std::string key);
    const char* get(std::string key);
    
private:
    std::unordered_map<std::string, Node*> m;
    Node *head, *tail;
    locker cachelocker;
    int capacity;
};

#endif
