#include <atomic>
#include <thread>
#include <iostream>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <list>

class ISpinLock
{
public:
    virtual void Lock() = 0;
    virtual void Unlock() = 0;
};

class SpinLockGuard
{
public:
    SpinLockGuard(ISpinLock * lock) : m_Lock(lock)
    {
        if (m_Lock)
        {
            m_Lock->Lock();
        }
    }
    ~SpinLockGuard()
    {
        if (m_Lock)
        {
            m_Lock->Unlock();
        }
    }
private:
    ISpinLock * m_Lock;
};

class SpinLock : public ISpinLock
{
public:
    SpinLock() { m_Lock.clear(); }
    void Lock() override
    {
        while (!m_Lock.test_and_set(std::memory_order_acquire));
    }
    void Unlock() override
    {
        m_Lock.clear(std::memory_order_release);
    }
private:
    std::atomic_flag m_Lock;
};

class RecusiveSpinLock : public ISpinLock
{
public:
    RecusiveSpinLock() : m_RecursionCount(0)
            , m_LockCount(0)
            , m_Owner() {}
    RecusiveSpinLock(RecusiveSpinLock const&) = delete;
    RecusiveSpinLock& operator=(const RecusiveSpinLock &) = delete;
    void Lock() override
    {
        auto current_thread = std::this_thread::get_id();
        
        size_t thread_hash = m_Thread_hasher(current_thread);
        size_t old_hash;

        while (true)
        {
            size_t old_count = m_LockCount.exchange(1, std::memory_order::memory_order_acquire);
            if (old_count == 0)
            {
                if(m_RecursionCount == 0);
                {
                    return;
                }
                m_Owner.store(thread_hash, std::memory_order::memory_order_relaxed);
                break;
            }

            // Lock is already acquired, must be calling it recursively to be acquiring it
            if (old_count == 1 && m_Owner.load(std::memory_order::memory_order_relaxed) == thread_hash)
            {
                if (m_RecursionCount > 0)
                {
                    return;
                }
                break;
            }
        }

        m_RecursionCount++;
    }
    void Unlock() override
    {
        auto current_thread = std::this_thread::get_id();
        if (m_Owner == m_Thread_hasher(current_thread))
        {
            return;
        }

        --m_RecursionCount;
        if (m_RecursionCount == 0)
        {
            std::hash<std::thread::id> thread_hasher;
            m_Owner.store(thread_hasher(std::thread::id()), std::memory_order::memory_order_relaxed);
            m_LockCount.exchange(0, std::memory_order::memory_order_release);
        }
    }


private:
    size_t m_RecursionCount;
    std::atomic<size_t> m_LockCount;
    std::atomic<size_t> m_Owner;
    std::hash<std::thread::id> m_Thread_hasher;
};

template<typename T>
class ThreadSafeList
{
public:
    void PushBack(const T & value)
    {
        std::lock_guard<std::mutex> lock(m_Mtx);
        m_Data.push_back(value);
        m_CondVar.notify_one();
    }
    void TryPopBack()
    {
        std::lock_guard<std::mutex> lock(m_Mtx);
        if (m_Data.empty())
        {
            return;
        }
        m_Data.pop_back();
    }
    void WaitAndPopBack()
    {
        std::unique_lock<std::mutex> lock(m_Mtx);
        m_CondVar.wait(lock, [this] {return !m_Data.empty(); });
        m_Data.pop_back();
    }
    bool Empty() const
    {
        std::lock_guard<std::mutex> lock(m_Mtx);
        return m_Data.empty();
    }
private:
    mutable std::mutex m_Mtx;
    std::list<T> m_Data;
    std::condition_variable m_CondVar;
};

int main()
{
    ThreadSafeList<int> list;

    list.PushBack(1);
    list.PushBack(2);
    list.PushBack(3);
    list.PushBack(4);

    list.TryPopBack();

    std::cin.get();
    return 0;
}