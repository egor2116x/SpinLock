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
    SpinLockGuard(ISpinLock * lock);
    ~SpinLockGuard();
private:
    ISpinLock * m_Lock;
};

class SpinLock : public ISpinLock
{
public:
    SpinLock();
    void Lock() override;
    void Unlock() override;
private:
    std::atomic_flag m_Lock;
};

class RecusiveSpinLock : public ISpinLock
{
public:
    RecusiveSpinLock();
    RecusiveSpinLock(RecusiveSpinLock const&) = delete;
    RecusiveSpinLock& operator=(const RecusiveSpinLock &) = delete;
    void Lock() override;
    void Unlock() override;
private:
    int Trylock();
private:
    SpinLock m_Lock;
    std::atomic<size_t> m_LockCount;
    std::atomic<size_t> m_Owner;
    std::hash<std::thread::id> m_Thread_hasher;
};

template<typename T>
class ThreadSafeList
{
public:
    void PushBack(const T & value);
    void TryPopBack();
    T WaitAndPopBack();
    bool Empty() const;
    size_t Size() const;
private:
    mutable std::mutex m_Mtx;
    std::list<T> m_Data;
    std::condition_variable m_CondVar;
};

class ThreadSafeListTest
{
public:
    ThreadSafeListTest();
    void TestSafeList();
    void PrintListInfo();
private:
    void ThreadPushImpl();
    void ThreadPopImpl();
private:
    ThreadSafeList<size_t> m_SafeList;
    std::mutex m_Mtx;
    bool m_Stop;
};

class SpinLockTest
{
public:
    void TestSpinLock();
private:
    void ThreadAddData();
    void ThreadTriedReadPopData();
private:
    std::list<size_t> m_Data;
    std::mutex m_Mtx;
    SpinLock m_Lock;
    const size_t COUNT_ITERS = 1000;
};

class RecursiveSpinLockTest
{
public:
    void TestRecursiveSpinLock();
private:
    void ThreadAddData();
    void ThreadTriedReadPopData();
private:
    std::list<size_t> m_Data;
    std::mutex m_Mtx;
    RecusiveSpinLock m_Lock;
    const size_t COUNT_ITERS = 1000;
};

// MAIN -----------------------------------------------------------
int main()
{
    std::wcout << "Start ThreadSafeListTest..." << std::endl;
    ThreadSafeListTest test;
    test.TestSafeList();
    std::wcout << "Finish ThreadSafeListTest..." << std::endl;
    std::wcout << std::endl;

    std::wcout << "Start SpinLockTest..." << std::endl;
    SpinLockTest test2;
    test2.TestSpinLock();
    std::wcout << "Finish SpinLockTest..." << std::endl;
    std::wcout << std::endl;

    std::wcout << "Start RecursiveSpinLockTest..." << std::endl;
    RecursiveSpinLockTest test3;
    test3.TestRecursiveSpinLock();
    std::wcout << "Finish RecursiveSpinLockTest..." << std::endl;
    std::wcout << std::endl;

    std::wcout << "Finish test...";
    std::wcin.get();
    return 0;
}
// ------------------------------------------------------------------

SpinLockGuard::SpinLockGuard(ISpinLock * lock) : m_Lock(lock)
{
    if (m_Lock)
    {
        m_Lock->Lock();
    }
}
SpinLockGuard::~SpinLockGuard()
{
    if (m_Lock)
    {
        m_Lock->Unlock();
    }
}

SpinLock::SpinLock() { m_Lock.clear(); }
void SpinLock::Lock()
{
    while (m_Lock.test_and_set(std::memory_order_acquire));
}
void SpinLock::Unlock()
{
    m_Lock.clear(std::memory_order_release);
}

RecusiveSpinLock::RecusiveSpinLock() : m_LockCount(0)
                                        , m_Owner() {}
void RecusiveSpinLock::Lock()
{
    while (Trylock() != 0);
}
void RecusiveSpinLock::Unlock()
{
    m_Lock.Lock();
    if (--m_LockCount <= 0)
    {
        m_LockCount = 0;
    }
    m_Lock.Unlock();
}

int RecusiveSpinLock::Trylock() {
    m_Lock.Lock();
    if (m_LockCount <= 0) 
    {
        m_Owner = m_Thread_hasher(std::this_thread::get_id());
        m_LockCount = 1;
        m_Lock.Unlock();
        return 0;
    }

    if (m_Owner == m_Thread_hasher(std::this_thread::get_id())) 
    {

        m_LockCount++;
        m_Lock.Unlock();
        return 0;
    }

    m_Lock.Unlock();
    return 1;
}

template<typename T>
void ThreadSafeList<T>::PushBack(const T & value)
{
    std::lock_guard<std::mutex> lock(m_Mtx);
    m_Data.push_back(value);
    m_CondVar.notify_one();
}

template<typename T>
void ThreadSafeList<T>::TryPopBack()
{
    std::lock_guard<std::mutex> lock(m_Mtx);
    if (m_Data.empty())
    {
        return;
    }
    m_Data.pop_back();
}

template<typename T>
T ThreadSafeList<T>::WaitAndPopBack()
{
    std::unique_lock<std::mutex> lock(m_Mtx);
    m_CondVar.wait(lock, [this] {return !m_Data.empty(); });
    auto value = m_Data.back();
    m_Data.pop_back();
    return std::move(value);
}

template<typename T>
bool ThreadSafeList<T>::Empty() const
{
    std::lock_guard<std::mutex> lock(m_Mtx);
    return m_Data.empty();
}

template<typename T>
size_t ThreadSafeList<T>::Size() const
{
    std::lock_guard<std::mutex> lock(m_Mtx);
    return m_Data.size();
}

ThreadSafeListTest::ThreadSafeListTest() : m_Stop(false) {}

void ThreadSafeListTest::TestSafeList()
{
    std::list<std::thread> l;
    std::thread t(&ThreadSafeListTest::ThreadPushImpl, this);
    l.push_back(std::move(t));
    std::thread t1(&ThreadSafeListTest::ThreadPopImpl, this);
    l.push_back(std::move(t1));

    for (auto & tr : l)
    {
        tr.join();
    }
}
void ThreadSafeListTest::PrintListInfo()
{
    std::wcout << L"List size: " << m_SafeList.Size();
}

void ThreadSafeListTest::ThreadPushImpl()
{
    const size_t COUNT_ITERS = 1000;
    std::hash<std::thread::id> hasher;
    for (size_t i = 0; i < COUNT_ITERS; i++)
    {
        m_Mtx.lock();
        std::wcout << L"Push Thread id: " << std::this_thread::get_id() << L" value: " << i << std::endl;
        m_Mtx.unlock();
        m_SafeList.PushBack(i);
    }
    m_Mtx.lock();
    std::wcout << L"Thread id: " << std::this_thread::get_id() << L" WAS PUSHED: " << COUNT_ITERS << std::endl;
    m_Mtx.unlock();
    m_Stop = true;
}

void ThreadSafeListTest::ThreadPopImpl()
{
    size_t count = 0;
    while (!m_Stop || !m_SafeList.Empty())
    {
        auto value = m_SafeList.WaitAndPopBack();
        m_Mtx.lock();
        std::wcout << L"Pop Thread id: " << std::this_thread::get_id() << L" value: " << value << std::endl;
        m_Mtx.unlock();
        ++count;
    }
    m_Mtx.lock();
    std::wcout << L"Thread id: " << std::this_thread::get_id() << L" WAS POPPED: " << count << std::endl;
    m_Mtx.unlock();
}

void SpinLockTest::TestSpinLock()
{
    std::list<std::thread> l;
    std::thread t(&SpinLockTest::ThreadAddData, this);
    l.push_back(std::move(t));
    std::thread t1(&SpinLockTest::ThreadTriedReadPopData, this);
    l.push_back(std::move(t1));

    for (auto & tr : l)
    {
        tr.join();
    }
}

void SpinLockTest::ThreadAddData()
{
    std::hash<std::thread::id> hasher;
    for (size_t i = 0; i < COUNT_ITERS; i++)
    {
        m_Mtx.lock();
        std::wcout << L"Push Thread id: " << std::this_thread::get_id() << L" value: " << i << std::endl;
        m_Mtx.unlock();

        m_Lock.Lock();
        m_Data.push_back(i);
        m_Lock.Unlock();
    }
}

void SpinLockTest::ThreadTriedReadPopData()
{
    size_t count = 0;
    while (count < COUNT_ITERS)
    {
        size_t value = 0;
        if (!m_Data.empty())
        {
            m_Lock.Lock();
            value = m_Data.back();
            m_Data.pop_back();
            m_Lock.Unlock();

            m_Mtx.lock();
            std::wcout << L"Pop Thread id: " << std::this_thread::get_id() << L" value: " << value << std::endl;
            m_Mtx.unlock();
            ++count;
        }
    }
    m_Mtx.lock();
    std::wcout << L"Thread id: " << std::this_thread::get_id() << L" WAS POPPED: " << count << std::endl;
    m_Mtx.unlock();
}

void RecursiveSpinLockTest::TestRecursiveSpinLock()
{
    std::list<std::thread> l;
    std::thread t(&RecursiveSpinLockTest::ThreadAddData, this);
    l.push_back(std::move(t));
    std::thread t1(&RecursiveSpinLockTest::ThreadTriedReadPopData, this);
    l.push_back(std::move(t1));

    for (auto & tr : l)
    {
        tr.join();
    }
}

void RecursiveSpinLockTest::ThreadAddData()
{
    const size_t COUNT_ITERS = 1000;
    std::hash<std::thread::id> hasher;
    for (size_t i = 0; i < COUNT_ITERS; i++)
    {
        m_Mtx.lock();
        std::wcout << L"Push Thread id: " << std::this_thread::get_id() << L" value: " << i << std::endl;
        m_Mtx.unlock();

        m_Lock.Lock();
        m_Data.push_back(i);
        m_Lock.Unlock();
    }
}

void RecursiveSpinLockTest::ThreadTriedReadPopData()
{
    size_t count = 0;
    while (count < COUNT_ITERS)
    {
        size_t value = 0;
        if (!m_Data.empty())
        {
            m_Lock.Lock();
            value = m_Data.back();
            m_Data.pop_back();
            m_Lock.Unlock();

            m_Mtx.lock();
            std::wcout << L"Pop Thread id: " << std::this_thread::get_id() << L" value: " << value << std::endl;
            m_Mtx.unlock();
            ++count;
        }
    }
    m_Mtx.lock();
    std::wcout << L"Thread id: " << std::this_thread::get_id() << L" WAS POPPED: " << count << std::endl;
    m_Mtx.unlock();
}