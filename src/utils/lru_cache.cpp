#include "utils/lru_cache.h"

// template<typename T1, typename T2>
LRUCache::LRUCache(int capacity)
{
    this->capacity = capacity;
    this->head = new Node();
    this->tail = new Node();
}

// template<typename T1, typename T2>
void LRUCache::updateCache(Node*& cur) {
    if(cur == this->head->next) return;
    if(cur == this->tail->next) {
        cur->prev->next = nullptr;
        this->tail->next = cur->prev;
    }
    else {
        cur->prev->next = cur->next;
        cur->next->prev = cur->prev;
    }
    this->insertNode(cur);
}

// template<typename T1, typename T2>
void LRUCache::insertNode(Node*& node) {
    this->head->next->prev = node;
    node->next = this->head->next;
    this->head->next = node;
    node->prev = nullptr;
}

// template<typename T1, typename T2>
const char* LRUCache::get(std::string key) {
    cachelocker.lock();
     // 1、cache里没有该元素
    if(this->m.find(key) == this->m.end()) {
        cachelocker.unlock();
        return "";
    }
    // 2、cache里有该元素，需要更新cache
    else {
        Node* cur = this->m[key];
        updateCache(cur);
        cachelocker.unlock();
        return cur->val;
    }
}

long LRUCache::getValSize(std::string key)
{
    return m[key]->len;
}

// template<typename T1, typename T2>
void LRUCache::put(std::string key, const char* value, long len) {
    cachelocker.lock();
    // 1、cache为空
    if(!this->m.size()) {
        Node* node = new Node(value, key, len);
        this->head->next = node;
        this->tail->next = node;
        this->m[key] = node;
    }
    else {
        // 2、cache不为空，且找到该元素，则更新cache
        if(this->m.find(key) != this->m.end()) {
            this->m[key]->val = value;
            updateCache(this->m[key]);
        }
        // 3、cache不为空且不满，且未找到该元素，则插入
        else {
            if(this->m.size() < this->capacity) {
                Node* node = new Node(value, key, len);
                this->m[key] = node;
                insertNode(node);
            }
            // 4、cache已满，且未找到该元素，需要淘汰最近最久未使用元素，然后插入
            else {
                Node* node = new Node(value, key, len);
                Node* t = this->tail->next;
                if(t == this->head->next) {
                    this->head->next = node;
                    this->tail->next = node;
                }
                else {
                    t->prev->next = nullptr;
                    this->tail->next = t->prev;
                    this->insertNode(node);
                }
                // 删除tail
                this->m.erase(t->index);
                delete t;
                this->m[key] = node;
            }
        }
    }
    cachelocker.unlock();
}