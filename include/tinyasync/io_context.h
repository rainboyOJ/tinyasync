#ifndef TINYASYNC_IOCONTEXT_H
#define TINYASYNC_IOCONTEXT_H

namespace tinyasync
{

#if defined(_WIN32)
    struct IoEvent {
        DWORD transfered_bytes;
        union {
            void* user_data_per_handle;
            ULONG_PTR key;
        };
    };
#elif defined(__unix__)

    struct IoEvent : epoll_event
    {
    };

    std::string ioe2str(epoll_event& evt)
    {
        std::string str;
        str += ((evt.events & EPOLLIN) ? "EPOLLIN " : "");;
        str += ((evt.events & EPOLLPRI) ? "EPOLLPRI " : "");
        str += ((evt.events & EPOLLOUT) ? "EPOLLOUT " : "");
        str += ((evt.events & EPOLLRDNORM) ? "EPOLLRDNORM " : "");
        str += ((evt.events & EPOLLRDBAND) ? "EPOLLRDBAND " : "");
        str += ((evt.events & EPOLLWRBAND) ? "EPOLLWRBAND " : "");
        str += ((evt.events & EPOLLMSG) ? "EPOLLMSG " : "");
        str += ((evt.events & EPOLLERR) ? "EPOLLERR " : "");
        str += ((evt.events & EPOLLHUP) ? "EPOLLHUP " : "");
        str += ((evt.events & EPOLLRDHUP) ? "EPOLLRDHUP " : "");
        str += ((evt.events & EPOLLEXCLUSIVE) ? "EPOLLEXCLUSIVE " : "");
        str += ((evt.events & EPOLLWAKEUP) ? "EPOLLWAKEUP " : "");
        str += ((evt.events & EPOLLONESHOT) ? "EPOLLONESHOT " : "");
        str += ((evt.events & EPOLLET) ? "EPOLLET " : "");
        return str;
    }

#endif

    struct Callback
    {

        // we don't use virtual table for two reasons
        //     1. virtual function let Callback to be non-standard_layout, though we have solution without offsetof using inherit
        //     2. we have only one function ptr, thus ... we can save a memory load without virtual functions table
        using CallbackPtr = void (*)(Callback *self, IoEvent &);

        CallbackPtr m_callback;

        void callback(IoEvent &evt)
        {
            this->m_callback(this, evt);
        }

#ifdef _WIN32
        OVERLAPPED m_overlapped;

        static Callback *from_overlapped(OVERLAPPED *o)
        {
            constexpr std::size_t offset = offsetof(Callback, m_overlapped);
            Callback *callback = reinterpret_cast<Callback *>((reinterpret_cast<char *>(o) - offset));
            return callback;
        }
#endif
    };

    static constexpr std::size_t Callback_size = sizeof(Callback);
    static_assert(std::is_standard_layout_v<Callback>);

    struct CallbackImplBase : Callback
    {

        // implicit null is not allowed
        CallbackImplBase(std::nullptr_t)
        {
            m_callback = nullptr;
        }

        template <class SubclassCallback>
        CallbackImplBase(SubclassCallback *)
        {
            (void)static_cast<SubclassCallback *>(this);
            //memset(&m_overlapped, 0, sizeof(m_overlapped));
            m_callback = &invoke_impl_callback<SubclassCallback>;
        }

        template <class SubclassCallback>
        static void invoke_impl_callback(Callback *this_, IoEvent &evt)
        {
            // invoke subclass' on_callback method
            SubclassCallback *subclass_this = static_cast<SubclassCallback *>(this_);
            subclass_this->on_callback(evt);
        }
    };
    static constexpr std::size_t CallbackImplBase_size = sizeof(CallbackImplBase);
    static_assert(std::is_standard_layout_v<CallbackImplBase>);

    class PostTask
    {
    public:
        // your callback
        using callback_type = void (*)(PostTask *);

        void set_callback(callback_type ptr) {
            m_callback = ptr;
        }
        callback_type get_callback() {
            return m_callback;
        }
    private:
        friend class IoCtxBase;
        // use internally
        ListNode m_node;
        callback_type m_callback;


    };


    // ----- begin time_queue
    using MS = std::chrono::milliseconds;
    using Clock = std::chrono::high_resolution_clock;
    using TimeStamp = Clock::time_point;

    struct timeNode {

        timeNode * m_next;
        timeNode * m_prev;
        PostTask * m_post_task; // 指向PostTask的指针
        TimeStamp m_expire; // 超时点
                            //
        TimeStamp get_expire_time() const { return m_expire; }

        timeNode() {
            init();
        }

        void init(){
            m_next = this;
            m_prev = this;
        }

        // @return
        //  true 删除后队列为空
        bool remove_self() {
            auto prev = this->m_prev;
            auto next = this->m_next;
            next->m_prev = prev;
            prev->m_next = next;
            return prev == next;
        }

        // 在当前结点后面添加一个节点
        void push(timeNode * node){
            auto next = m_next;
            node->m_next = next;
            node->m_prev = this;
            this->m_next = node;
            next->m_prev = node;
        }

        bool is_expire(TimeStamp ts) //是否超时
        {
            return m_expire < ts;
        }
    };



    template<std::size_t miliseconds>
    class timeQueue {

        private:
            std::size_t m_size; // 项目数量
            static constexpr auto _duration_ = MS(miliseconds);
            timeNode m_head;
        public:

            timeQueue() {
                m_head.init();
            }

            //加入队列
            void push(timeNode * node) {
                node->m_expire = Clock::now() + _duration_;
                back()->push(node);
            }

            void pop() {
                if( ! empty() ){
                    front()->remove_self();
                }
            }

            timeNode * front(){
                return m_head.m_next;
            }

            timeNode * back() {
                return m_head.m_prev;
            }

            bool empty() {
                return m_head.m_next == &m_head;
            }
    };

     // end __time_queue

    class IoCtxBase
    {
    protected:
        static PostTask *from_node_to_post_task(ListNode *node) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
            return (PostTask *)((char *)node - offsetof(PostTask, m_node));
#pragma GCC diagnostic pop
        }

        static ListNode *get_node(PostTask *posttask) {
            return &posttask->m_node;
        }
        
    public:
        virtual void run() = 0;
        virtual void post_task(PostTask *) = 0;
        virtual void request_abort() = 0;
        virtual void post_time_out(timeNode * ) = 0; // 加入时间检查点
        virtual ~IoCtxBase() {}

        // avoid using virtual functions ...
        NativeHandle m_epoll_handle = NULL_HANDLE;
        std::pmr::memory_resource *m_memory_resource;

        NativeHandle event_poll_handle()
        {
            return m_epoll_handle;
        }
    };

    class IoContext
    {
        std::unique_ptr<IoCtxBase> m_ctx;

    public:

        template <bool multiple_thread = true>
        IoContext(std::integral_constant<bool, multiple_thread> = std::true_type());

        IoCtxBase *get_io_ctx_base() {
            return m_ctx.get();
        }

        void run()
        {
            auto *ctx = m_ctx.get();
            ctx->run();
        }

        void post_task(PostTask *task)
        {
            auto *ctx = m_ctx.get();
            ctx->post_task(task);
        }
        
        void request_abort()
        {
            auto *ctx = m_ctx.get();
            ctx->request_abort();
        }

        std::pmr::memory_resource *get_memory_resource_for_task()
        {
            auto *ctx = m_ctx.get();
            return ctx->m_memory_resource;
        }

        NativeHandle event_poll_handle()
        {
            auto *ctx = m_ctx.get();
            return ctx->m_epoll_handle;
        }
    };



    struct SingleThreadTrait
    {
        using spinlock_type = NaitveLock;
        static constexpr bool multiple_thread = false;
        static std::pmr::memory_resource *get_memory_resource() {
            return std::pmr::get_default_resource();
        }
    };

    struct MultiThreadTrait
    {
        using spinlock_type = DefaultSpinLock;
        static constexpr bool multiple_thread = true;
        static std::pmr::memory_resource *get_memory_resource() {
            return get_default_resource();
        }
    };

    template <class CtxTrait>
    class IoCtx : public IoCtxBase
    {
        IoCtx(IoCtx &&r) = delete;
        IoCtx &operator=(IoCtx &&r) = delete;

        NativeHandle m_wakeup_handle = NULL_HANDLE;

        typename CtxTrait::spinlock_type m_que_lock;

        std::size_t m_thread_waiting = 0;
        std::size_t m_task_queue_size = 0;
        Queue m_task_queue;

        //最多30秒的等待,超时
        timeQueue<10 * 1000> m_time_queue;
        bool m_abort_requested = false;
        static const bool k_multiple_thread = CtxTrait::multiple_thread;

        void wakeup_a_thread();
    public:
        IoCtx();
        void post_task(PostTask *callback) override;
        void post_time_out(timeNode *) override;
        void request_abort() override;
        void run() override;
        ~IoCtx() override;
    };


    template <bool multiple_thread>
    IoContext::IoContext(std::integral_constant<bool, multiple_thread>)
    {
        // if you needn't multiple thread
        // you don't have to link with e.g. pthread library
        if constexpr (multiple_thread)
        {
            m_ctx = std::make_unique<IoCtx<MultiThreadTrait>>();
        }
        else
        {
            m_ctx = std::make_unique<IoCtx<SingleThreadTrait>>();
        }
    }

    template <class T>
    IoCtx<T>::IoCtx()
    {

        TINYASYNC_GUARD("IoContext.IoContext(): ");
        TINYASYNC_LOG("Ctx at %p", this);

        m_memory_resource = T::get_memory_resource();
#ifdef _WIN32

        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != NO_ERROR)
        {
            throw_WASError("WSAStartup failed with error ", iResult);
        }

        int num_thread = 1;
        m_native_handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, num_thread);
        if (m_native_handle == NULL)
        {
            throw_LastError("can't create event poll");
        }

#elif defined(__unix__)

        auto fd = epoll_create1(EPOLL_CLOEXEC);
        if (fd == -1)
        {
            throw_errno("IoContext().IoContext(): can't create epoll");
        }
        m_epoll_handle = fd;
        TINYASYNC_LOG("event poll created %s", handle_c_str(m_epoll_handle));

        fd = eventfd(1, EFD_NONBLOCK);
        if (fd == -1)
        {
            throw_errno("IoContext().IoContext(): can't create eventfd");
        }
        m_wakeup_handle = fd;
        TINYASYNC_LOG("wakeup handle created %s", handle_c_str(m_wakeup_handle));

        epoll_event evt;
        evt.data.ptr = (void *)1;
        evt.events = EPOLLIN | EPOLLONESHOT;
        if(epoll_ctl(m_epoll_handle, EPOLL_CTL_ADD, m_wakeup_handle, &evt) < 0) {
            std::string err =  format("can't set wakeup event %s (epoll %s)", handle_c_str(m_wakeup_handle), handle_c_str(m_epoll_handle));
            TINYASYNC_LOG(err.c_str());
            throw_errno(err);
        }


#endif
    }

    template <class T>
    IoCtx<T>::~IoCtx()
    {
#ifdef _WIN32

        WSACleanup();
#elif defined(__unix__)

        if (m_wakeup_handle)
        {
            ::epoll_ctl(m_epoll_handle, EPOLL_CTL_DEL, m_wakeup_handle, NULL);
            close_handle(m_wakeup_handle);
        }
        close_handle(m_epoll_handle);
#endif
    }

    template <class T>
    void IoCtx<T>::wakeup_a_thread()
    {
        epoll_event evt;
        evt.data.ptr = (void *)1;
        evt.events = EPOLLIN | EPOLLONESHOT;
        // not thread safe by standard but currently OK
        if(epoll_ctl(m_epoll_handle, EPOLL_CTL_MOD, m_wakeup_handle, &evt) < 0) {
            std::string err =  format("can't set wakeup event %s (epoll %s)", handle_c_str(m_wakeup_handle), handle_c_str(m_epoll_handle));
            TINYASYNC_LOG(err.c_str());
            throw_errno(err);
        }

    }


    template <class T>
    void IoCtx<T>::post_task(PostTask *task)
    {

        TINYASYNC_GUARD("post_task(): ");
        if constexpr (k_multiple_thread)
        {
            m_que_lock.lock();
            m_task_queue.push(get_node(task));
            m_task_queue_size += 1;
            auto thread_wating = m_thread_waiting;
            m_que_lock.unlock();

            if (thread_wating > 0)
            {
                TINYASYNC_LOG("has thread waiting event %s (epoll %s)", handle_c_str(m_wakeup_handle), handle_c_str(m_epoll_handle));
                wakeup_a_thread();
            } else {
                TINYASYNC_LOG("no thread waiting");
            }
        }
        else
        {
            m_task_queue.push(get_node(task));
        }
    }

    template <class T>
    void IoCtx<T>::post_time_out(timeNode * tnode)
    {
        m_time_queue.push(tnode);
    }

    template <class T>
    void IoCtx<T>::request_abort()
    {
        if constexpr(k_multiple_thread) {
            m_que_lock.lock();
            m_abort_requested = true;
            auto thread_waiting = m_thread_waiting;
            m_que_lock.unlock();
            if(thread_waiting) {
                wakeup_a_thread();
            }
        } else {
            m_abort_requested = true;
        }
    }

    template <class T>
    void IoCtx<T>::run()
    {

        Callback *const CallbackGuard = (Callback *)8;

        TINYASYNC_GUARD("IoContex::run(): ");
        int const maxevents = 5;

        for (;;)
        {
            printf("running... \n");

            if constexpr (k_multiple_thread)
            {
                m_que_lock.lock();
            }

            // 检查时间队列
            auto time_node = m_time_queue.front();
            auto now_time = Clock::now();
            while ( m_time_queue.empty() == false ) {
                auto time_node = m_time_queue.front();
// #if __USE_ASYNC_UTILS__
#if 0
                printf("time time_node addr  = %p \n",time_node);
                printf("time time_node is_expire = %d\n",time_node->is_expire(now_time));
                printf("now_time : %lld\n",std::chrono::duration_cast<std::chrono::nanoseconds>(now_time.time_since_epoch()).count());
                printf("time node time : %lld\n",std::chrono::duration_cast<std::chrono::nanoseconds>(time_node->get_expire_time().time_since_epoch()).count());

                printf("seconds %lld \n",
std::chrono::duration_cast<std::chrono::seconds>( (now_time - time_node->get_expire_time()) ).count()
                        );

#endif
                if(!time_node->is_expire(now_time)) break;
                m_time_queue.pop();

                //创建Task
                post_task(time_node->m_post_task);
                time_node->remove_self();
            }

            auto node = m_task_queue.pop();
            bool abort_requested = m_abort_requested;

            if (abort_requested)
                TINYASYNC_UNLIKELY
                {
                    if constexpr (k_multiple_thread)
                    {
                        m_que_lock.unlock();
                    }
                    // very rude ...
                    // TODO: clean up more carefully
                    break;
                }

            if (node)
            {
                // we have task to do
                if constexpr (k_multiple_thread)
                {
                    m_task_queue_size -= 1;
                    m_que_lock.unlock();
                }

                PostTask *task = from_node_to_post_task(node);
                try
                {
                    auto callback = task->get_callback();
                    callback(task);
                }
                catch (...)
                {
                    terminate_with_unhandled_exception();
                }
            }
            else
            {
                // no task
                // blocking by epoll_wait
                if constexpr (k_multiple_thread)
                {
                    m_thread_waiting += 1;
                    m_que_lock.unlock();
                }


#ifdef _WIN32

                IoEvent evt;
                OVERLAPPED *overlapped;

                if (::GetQueuedCompletionStatus(m_native_handle,
                                                &evt.transfered_bytes,
                                                &evt.key,
                                                &overlapped, INFINITE) == 0)
                {
                    throw_LastError("GetQueuedCompletionStatus failed");
                }
                TINYASYNC_LOG("Get one event");
                Callback *callback = Callback::from_overlapped(overlapped);
                callback->callback(evt);

#elif defined(__unix__)

                IoEvent events[maxevents];
                const auto epfd = this->event_poll_handle();
                // int const timeout = -1; // indefinitely
                int const timeout = 1000; // 1000ms

                TINYASYNC_LOG("waiting event ... handle = %s", handle_c_str(epfd));
                int nfds = epoll_wait(epfd, (epoll_event *)events, maxevents, timeout);
                TINYASYNC_LOG("epoll wakeup handle = %s", handle_c_str(epfd));

                if constexpr (k_multiple_thread)
                {
                    m_que_lock.lock();
                    m_thread_waiting -= 1;
                    const auto task_queue_size = m_task_queue_size;
                    m_que_lock.unlock();
                    TINYASYNC_LOG("task_queue_size %d\n", task_queue_size);

                    if (nfds == -1)
                    {
                        throw_errno("epoll_wait error");
                    }

                    // let's have a overview of event
                    size_t effective_event = 0;
                    size_t wakeup_event = 0;
                    for (auto i = 0; i < nfds; ++i)
                    {
                        auto &evt = events[i];
                        auto callback = (Callback *)evt.data.ptr;
                        if (callback < CallbackGuard)
                        {
                            wakeup_event = 1;
                            ++i;
                            for (; i < nfds; ++i)
                            {
                                auto &evt = events[i];
                                auto callback = (Callback *)evt.data.ptr;
                                if (callback < CallbackGuard)
                                {
                                    //
                                }
                                else
                                {
                                    effective_event = 1;
                                    break;
                                }
                            }
                            break;
                        }
                        else
                        {
                            effective_event = 1;
                            ++i;
                            for (; i < nfds; ++i)
                            {
                                auto &evt = events[i];
                                auto callback = (Callback *)evt.data.ptr;
                                if (callback < CallbackGuard)
                                {
                                    wakeup_event = 1;
                                    break;
                                }
                            }
                            break;
                        }
                    }

                    if(m_thread_waiting) {
                        if(m_abort_requested) {
                            wakeup_a_thread();

                        } else if(wakeup_event && (task_queue_size + effective_event > 1)) {
                            // this is an wakeup event
                            // it means we may need to wakeup thread
                            // this is thread is to deal with effective_event
                            wakeup_a_thread();
                        }

                    }
                }

                for (auto i = 0; i < nfds; ++i)
                {
                    auto &evt = events[i];
                    TINYASYNC_LOG("event %d of %d", i, nfds);
                    TINYASYNC_LOG("event = %x (%s)", evt.events, ioe2str(evt).c_str());
                    auto callback = (Callback *)evt.data.ptr;
                    if (callback >= CallbackGuard)
                    {
                        TINYASYNC_LOG("invoke callback");
                        try
                        {
                            callback->callback(evt);
                        }
                        catch (...)
                        {
                            terminate_with_unhandled_exception();
                        }
                    }
                }
#endif

            } // if(node) ... else
        }     // for
    }         // run

} // namespace tinyasync

#endif
