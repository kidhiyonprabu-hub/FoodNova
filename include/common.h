/*
 * FoodRush – Smart Online Food Ordering System
 * common.h – Shared types, constants and utility macros
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* Platform-specific socket includes */
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib,"ws2_32")
  typedef SOCKET sock_t;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCK(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCK (-1)
  #define CLOSE_SOCK(s) close(s)
#endif

/* Server configuration */
#define SERVER_PORT       8080
#define MAX_CLIENTS       256
#define READ_BUF_SIZE     65536
#define MAX_PATH_LEN      512
#define MAX_HEADER_LEN    4096
#define MAX_BODY_LEN      131072

/* Application limits */
#define MAX_USERNAME_LEN  64
#define MAX_PASSWORD_LEN  128
#define MAX_EMAIL_LEN     128
#define MAX_NAME_LEN      256
#define MAX_DESC_LEN      1024
#define MAX_ADDRESS_LEN   512
#define MAX_PHONE_LEN     20
#define MAX_JSON_LEN      131072
#define ORDER_ID_LEN      32

/* Thread pool configuration */
#define WORKER_THREADS    8
#define TASK_QUEUE_SIZE   128

/* HTTP methods */
#define METHOD_GET    "GET"
#define METHOD_POST   "POST"
#define METHOD_PUT    "PUT"
#define METHOD_DELETE "DELETE"
#define METHOD_OPTIONS "OPTIONS"

/* Content types */
#define CT_JSON  "application/json"
#define CT_HTML  "text/html"
#define CT_CSS   "text/css"
#define CT_JS    "application/javascript"
#define CT_PNG   "image/png"
#define CT_JPG   "image/jpeg"
#define CT_ICO   "image/x-icon"
#define CT_SVG   "image/svg+xml"
#define CT_WOFF2 "font/woff2"

/* Simple logging */
#define LOG_INFO(fmt,...) do { \
    time_t _t = time(NULL); \
    struct tm *_tm = localtime(&_t); \
    char _ts[32]; strftime(_ts,sizeof(_ts),"%Y-%m-%d %H:%M:%S",_tm); \
    fprintf(stdout,"[%s][INFO ][TID:%lu] " fmt "\n",_ts,(unsigned long)pthread_self(),##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

#define LOG_ERR(fmt,...) do { \
    time_t _t = time(NULL); \
    struct tm *_tm = localtime(&_t); \
    char _ts[32]; strftime(_ts,sizeof(_ts),"%Y-%m-%d %H:%M:%S",_tm); \
    fprintf(stderr,"[%s][ERROR][TID:%lu] " fmt "\n",_ts,(unsigned long)pthread_self(),##__VA_ARGS__); \
    fflush(stderr); \
} while(0)

/* HTTP request structure */
typedef struct {
    char method[16];
    char path[MAX_PATH_LEN];
    char query[MAX_PATH_LEN];
    char body[MAX_BODY_LEN];
    int  body_len;
    char content_type[128];
    char auth_token[256];
    char user_agent[256];
} http_request_t;

/* HTTP response structure */
typedef struct {
    int  status;
    char content_type[64];
    char body[MAX_JSON_LEN];
    int  body_len;
} http_response_t;

/* Generic result */
typedef enum {
    RESULT_OK = 0,
    RESULT_ERR = 1,
    RESULT_NOT_FOUND = 2,
    RESULT_UNAUTHORIZED = 3,
    RESULT_CONFLICT = 4,
    RESULT_INSUFFICIENT_STOCK = 5
} result_t;

/* User role */
typedef enum {
    ROLE_CUSTOMER = 0,
    ROLE_ADMIN    = 1
} user_role_t;

/* Order status */
typedef enum {
    ORDER_PENDING        = 0,
    ORDER_CONFIRMED      = 1,
    ORDER_PREPARING      = 2,
    ORDER_OUT_DELIVERY   = 3,
    ORDER_DELIVERED      = 4,
    ORDER_CANCELLED      = 5
} order_status_t;

static inline const char *order_status_str(order_status_t s) {
    switch (s) {
        case ORDER_PENDING:      return "Pending";
        case ORDER_CONFIRMED:    return "Confirmed";
        case ORDER_PREPARING:    return "Preparing";
        case ORDER_OUT_DELIVERY: return "Out for Delivery";
        case ORDER_DELIVERED:    return "Delivered";
        case ORDER_CANCELLED:    return "Cancelled";
        default:                 return "Unknown";
    }
}

/* Safe string copy */
#define SAFE_STRNCPY(dst, src, sz) do { \
    strncpy((dst),(src),(sz)-1); (dst)[(sz)-1]='\0'; \
} while(0)

#endif /* COMMON_H */
