#include "net/TcpServer.h"
#include "Log/Logger.h"
#include "net/ThreadSwitcher.h"
#include <signal.h>

using namespace std::placeholders;

TcpServer::TcpServer(EventLoop* loop, const std::string& ip, uint16_t port, uint16_t conn_time_out)
    : loop_(loop),
    acceptor_(new Acceptor(loop, ip, port)),
    thread_pool_(std::make_unique<EventLoopThreadPool>(loop)),
    wheel_size_(conn_time_out)
{
    // 如果对端关闭了Socket，发送数据时对端会返回RST，本端继续往改Socket写数据，操作系统会发送SIGPIPE信号，默认情况下程序会终止
    signal(SIGPIPE, SIG_IGN);

    // 绑定新连接到来的处理函数
    acceptor_->SetNewConnectionCallback(std::bind(&TcpServer::NewConnection, this, _1, _2));
    
    // 初始化时间轮的大小
    wheel_.resize(wheel_size_);
}

TcpServer::~TcpServer()
{
    // 1. 设置标志位，防止析构过程中有新的连接进来
    //    或者在 Acceptor 中停止监听

    // 2. 遍历所有连接，通知它们进入 Shutdown 流程
    for (auto& item : connections_)
    {
        TcpConnectionPtr conn = item.second;

        // 关键点：将关闭任务投递到该连接所属的 IO 线程
        ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ShutDown);
    }

    // 3. 此时不能直接退出，需要等待连接真正完成关闭逻辑。
    //    Muduo 的做法是利用 Loop 的退出机制，但在简单的 TcpServer 中，
    //    你可以简单地让 EventLoop 等待一小会儿，或者通过引用计数判断。
}

void TcpServer::Start(int thread_num) 
{
    thread_pool_->SetThreadNum(thread_num);
    thread_pool_->Start();
    acceptor_->Listen();

    // 开始时间轮
    loop_->RunEvery(1.0, [this]() 
        {
        this->Tick(); // 时间轮，监测所有连接的超时
        });
}

void TcpServer::Tick()
{
    wheel_curr_ = (wheel_curr_ + 1) % wheel_.size();                                    // 挪动步长

    //for (const auto& entry : wheel_[wheel_curr_]) 
    //{
    //    LOG_INFO << "Tick at " << wheel_curr_
    //        <<  "entry use_count=" << entry.use_count();
    //}

    wheel_[wheel_curr_].clear();
}

void TcpServer::RefreshEntry(TcpConnection::EntryPtr entry)
{
    int target = (wheel_curr_ + wheel_.size() - 1) % wheel_.size();
    loop_->RunInLoop([this, target, entry]()
        {
            wheel_[target].insert(entry);
        });
}

void TcpServer::NewConnection(int sockfd, const std::string& peerAddr) 
{
    LOG_INFO << "TcpServer::NewConnection - new connection from " << peerAddr << "sockfd = " << sockfd;

    // 不再使用主线程的loop_
    EventLoop* io_loop = thread_pool_->GetNextLoop();
    EventLoop* loop = (io_loop != nullptr) ? io_loop : loop_;

    // 1. 创建 TcpConnection 对象
    auto conn = std::make_shared<TcpConnection>(loop, sockfd);

    // 2. 挪移到时间轮中管理超时
    TcpConnection::EntryPtr entry = std::make_shared<TcpConnection::Entry>(conn);
    conn->SetEntry(entry);
    RefreshEntry(entry);

    std::weak_ptr<TcpConnection::Entry> weak_entry = entry;

    // 3. 为连接设置消息回调
    auto wrapped_message_cb = [this, weak_entry](const TcpConnectionPtr& conn, Buffer* buf)
        {
            // 1. 续命：将 Entry 移到时间轮的最新桶
            auto entry = weak_entry.lock();
            if (entry)
            {
                RefreshEntry(entry);
            }

            // 2. 执行用户原始回调
            if (this->message_cb_) 
            {
                this->message_cb_(conn, buf);
            }
        };

    conn->SetMessageCallback(wrapped_message_cb);
    conn->SetConnectionCallback(connection_callback_);
    conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

    // 4. 将连接加入 map 管理
    connections_[sockfd] = std::move(conn);

    // 5. 向Epoll注册连接
    ThreadSwitcher::Run(loop, connections_[sockfd], &TcpConnection::ConnectEstablished);
}

void TcpServer::RemoveConnection(const std::shared_ptr<TcpConnection>& conn)
{
    ThreadSwitcher::Run(loop_, this, &TcpServer::RemoveConnectionInLoop, conn);
}

void TcpServer::RemoveConnectionInLoop(const std::shared_ptr<TcpConnection>& conn)
{
    loop_->AssertInLoopThread();

    // 1. 将最后的销毁动作放入队列
    // 注意：这里 bind 再次持有了 conn 的副本，引用计数+1，保证了 ConnectDestroyed 执行时对象不被析构
    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    // 2. 从 map 中删除，此时引用计数减 1
    size_t n = connections_.erase(conn->fd());
    LOG_INFO << "TcpServer::RemoveConnectionInLoop for fd=" << conn->fd() 
        << " thread id = " << std::this_thread::get_id() << " connections_.size() = " << connections_.size();
}