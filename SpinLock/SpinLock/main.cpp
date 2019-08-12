#include <atomic>
#include <thread>
#include <iostream>
#include <memory>

class ISpinLock
{
public:
    virtual void Lock() = 0;
    virtual void Unlock() = 0;
};

class SpinLockGuard
{
public:
    SpinLockGuard(ISpinLock * lock, unsigned int count = 1) : m_Lock(lock), m_Count(count)
    {
        if (m_Lock)
        {
            for (size_t i = 0; i < m_Count; i++)
            {
                m_Lock->Lock();
            }
        }
    }
    ~SpinLockGuard()
    {
        if (m_Lock)
        {
            for (size_t i = 0; i < m_Count; i++)
            {
                m_Lock->Unlock();
            }
        }
    }
private:
    ISpinLock * m_Lock;
    unsigned int m_Count;
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
    RecusiveSpinLock() : m_RecursiveCounter(0), m_Owner(std::thread::id()) { m_Lock.clear(); }
    void Lock() override
    {
        for (volatile size_t i = 0; !TryLock(); ++i)
        {
            if (i % 100000 == 0)
            {
                std::this_thread::yield();
            }
        }
    }
    void Unlock() override
    {
        if (m_Owner.load(std::memory_order_acquire) == std::this_thread::get_id())
        {
            return;
        }
        
        if (m_RecursiveCounter <= 0)
        {
            return;
        }

        if (--m_RecursiveCounter == 0) 
        {
            m_Owner.store(std::thread::id(), std::memory_order_release);
            m_Lock.clear(std::memory_order_release);
        }
    }
private:
    bool TryLock()
    {
        if (!m_Lock.test_and_set(std::memory_order_acquire)) 
        {
            m_Owner.store(std::this_thread::get_id(), std::memory_order_release);
        }
        else 
        {
            if (m_Owner.load(std::memory_order_acquire) != std::this_thread::get_id())
            {
                return false;
            }
        }
        ++m_RecursiveCounter;
        return true;
    }
private:
    std::atomic_flag m_Lock;
    int64_t m_RecursiveCounter;
    std::atomic<std::thread::id> m_Owner;
};

int g_ShareValue = 0;
std::unique_ptr<ISpinLock> g_SpinLock(new SpinLock);
std::unique_ptr<ISpinLock> g_RecursiveSpinLock(new RecusiveSpinLock);

void UsingWithSpinLock()
{
    SpinLockGuard lockGuard(g_SpinLock.get()); // lock
    g_ShareValue++;
    //unlock when return using SpinLockGuard destructor
}

void UsingWithRecursiveSpinLock()
{
    SpinLockGuard lockGuard(g_RecursiveSpinLock.get()); // lock
    g_ShareValue++;
    //unlock when return using SpinLockGuard destructor
}

int main()
{
    /*std::thread t1(UsingWithSpinLock);
    std::thread t2(UsingWithSpinLock);*/

    std::thread t3(UsingWithRecursiveSpinLock);
    std::thread t4(UsingWithRecursiveSpinLock);

    /*t1.join(); 
    t2.join();*/
    std::cout << "shared_value = " << g_ShareValue << std::endl;

    t3.join();
    t4.join();

    std::cout << "shared_value = " << g_ShareValue << std::endl;
    
    std::cin.get();
    return 0;
}