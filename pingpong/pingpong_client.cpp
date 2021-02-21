//#define TINYASYNC_TRACE

#include "echo_common.h"
using namespace tinyasync;

int nc = 0;
bool g_run = true;
std::chrono::seconds timeout { 0 };
size_t nsess;

Task<> start(IoContext &ctx, Session s)
{
    auto lb = allocate(&pool);
    ConstBuffer buffer = lb->buffer;
    co_await s.conn.async_send(buffer);

	co_spawn(s.read(ctx));
	co_await s.send(ctx);

	for(;s.m_run && g_run;) {
		co_await s.all_done;
	}
    
	--nc;
	printf("%d conn\n", nc);
    deallocate(&pool, lb);
}

Task<> connect_(IoContext &ctx)
{

    Endpoint endpoint(Address::Any(), 8899);
    Protocol protocol;
    std::vector<Session> sesses;
	for (size_t i = 0; i <  nsess; ++i) {
		Connection conn = co_await async_connect(ctx, protocol, endpoint);
        sesses.push_back(Session(ctx, std::move(conn), &pool));
	}

	for (size_t i = 0; i <  nsess; ++i) {
        co_spawn(start(ctx, std::move(sesses[i])));
	}
    
    co_await async_sleep(ctx, timeout);
    g_run = false;

    printf("%d connection\n", (int)nsess);
    printf("%d block size\n", (int)block_size);
    printf("%.2f M/s bytes read\n", (long long)nread_total/timeout.count()/1E6);
    printf("%.2f M/s bytes write\n", (long long)nwrite_total/timeout.count()/1E6);

    ctx.request_abort();
}


void client() {	

	IoContext ctx;
	co_spawn(connect_(ctx));
	ctx.run();
}

int main()
{
    nsess = 10;
    timeout = std::chrono::seconds(20);
    block_size = 1024;
    initialize_pool();


	client();
	return 0;
}


