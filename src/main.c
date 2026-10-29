/*
 * FoodRush – Smart Online Food Ordering System
 * main.c – Entry point: server bootstrap, accept loop
 */

#include "common.h"
#include "database.h"
#include "threadpool.h"
#include "router.h"
#include "handlers.h"

#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib,"ws2_32.lib")
#endif

/* Forward declaration from router.c */
void router_handle_client(sock_t client);

static volatile int g_running = 1;
static thread_pool_t *g_worker_pool = NULL;

static void shutdown_handler(int sig)
{
    (void)sig;
    LOG_INFO("Shutdown signal received – cleaning up …");
    g_running = 0;
}

int main(void)
{
    LOG_INFO("==========================================================");
    LOG_INFO("  FoodRush – Smart Autonomous Food Ordering System v1.0");
    LOG_INFO("  High-Performance Low-Latency Concurrency Platform");
    LOG_INFO("==========================================================");

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        LOG_ERR("WSAStartup failed");
        return 1;
    }
#endif

    signal(SIGINT,  shutdown_handler);
    signal(SIGTERM, shutdown_handler);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    /* ── Initialise database ─────────────────────────────────────────── */
    if (db_init() != 0) {
        LOG_ERR("Database initialisation failed");
        return 1;
    }
    session_store_init();

    /* ── Create order-processing worker pool ─────────────────────────── */
    g_worker_pool = pool_create(WORKER_THREADS, TASK_QUEUE_SIZE);
    if (!g_worker_pool) {
        LOG_ERR("Failed to create worker pool");
        return 1;
    }

    /* ── Set up HTTP routing ──────────────────────────────────────────── */
    /* We use the same pool for both HTTP connections and order processing */
    router_init(g_worker_pool);
    handlers_register_all(g_worker_pool);

    /* ── Create server socket ─────────────────────────────────────────── */
    sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCK) {
        LOG_ERR("socket() failed");
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("bind() failed on port %d", SERVER_PORT);
        CLOSE_SOCK(server);
        return 1;
    }

    if (listen(server, MAX_CLIENTS) < 0) {
        LOG_ERR("listen() failed");
        CLOSE_SOCK(server);
        return 1;
    }

    LOG_INFO("Server listening on http://localhost:%d", SERVER_PORT);
    LOG_INFO("Open http://localhost:%d in your browser", SERVER_PORT);
    LOG_INFO("Admin login: username=admin  password=admin123");
    LOG_INFO("Demo  login: username=john   password=password123");
    LOG_INFO("Press Ctrl+C to stop");
    LOG_INFO("----------------------------------------------------------");

    /* ── Accept loop ──────────────────────────────────────────────────── */
    while (g_running) {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        sock_t client = accept(server,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client == INVALID_SOCK) {
            if (g_running) LOG_ERR("accept() failed");
            break;
        }
        router_handle_client(client);
    }

    /* ── Graceful shutdown ────────────────────────────────────────────── */
    CLOSE_SOCK(server);
    LOG_INFO("Waiting for worker threads to finish …");
    if (g_worker_pool) {
        pool_wait(g_worker_pool);
        pool_destroy(g_worker_pool);
    }
    db_close();
#ifdef _WIN32
    WSACleanup();
#endif
    LOG_INFO("FoodRush server stopped. Goodbye!");
    return 0;
}
