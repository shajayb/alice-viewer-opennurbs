#ifndef ALICE_MEMORY_H
#define ALICE_MEMORY_H

#include <cstdint>
#include <cstdlib>

namespace Alice
{

struct LinearArena
{
    uint8_t* memory;
    size_t capacity;
    size_t offset;

    void init(size_t sizeInBytes)
    {
        memory = (uint8_t*)malloc(sizeInBytes);
        capacity = sizeInBytes;
        offset = 0;
    }

    void* allocate(size_t size)
    {
        if (offset + size > capacity)
        {
            return nullptr;
        }
        void* ptr = memory + offset;
        offset += size;
        return ptr;
    }

    void reset()
    {
        offset = 0;
    }
};

template<typename T>
struct Buffer
{
    T* data;
    size_t count;
    size_t capacity;

    void init(LinearArena& arena, size_t maxElements)
    {
        data = (T*)arena.allocate(sizeof(T) * maxElements);
        count = 0;
        capacity = maxElements;
    }

    void push_back(const T& item)
    {
        if (count < capacity)
        {
            data[count++] = item;
        }
    }

    void clear()
    {
        count = 0;
    }

    T* begin() { return data; }
    T* end() { return data + count; }
    const T* begin() const { return data; }
    const T* end() const { return data + count; }
    size_t size() const { return count; }
    T& operator[](size_t index) { return data[index]; }
    const T& operator[](size_t index) const { return data[index]; }
};

} // namespace Alice

#endif
