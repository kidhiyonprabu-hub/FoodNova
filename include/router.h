/*
 * FoodRush – Smart Online Food Ordering System
 * router.h – URL routing interface
 */
#ifndef ROUTER_H
#define ROUTER_H

#include "common.h"
#include "threadpool.h"

typedef void (*route_handler_t)(http_request_t *req, http_response_t *res,
                                const char *path_params);

typedef struct {
    char            method[16];
    char            pattern[MAX_PATH_LEN];
    route_handler_t handler;
    int             require_auth;   /* 0=none, 1=any-user, 2=admin */
} route_t;

void router_init(thread_pool_t *pool);
void router_dispatch(sock_t client, http_request_t *req);

/* Route registration helpers used by handlers */
void route_register(const char *method, const char *pattern,
                    route_handler_t handler, int require_auth);

/* Response helpers */
void respond_json(http_response_t *res, int status, const char *json_body);
void respond_error(http_response_t *res, int status, const char *msg);
void respond_file(sock_t client, const char *filepath);

/* Send the response */
void send_response(sock_t client, http_response_t *res);

#endif /* ROUTER_H */
