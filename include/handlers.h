/*
 * FoodRush – Smart Online Food Ordering System
 * handlers.h – HTTP route handler declarations
 */
#ifndef HANDLERS_H
#define HANDLERS_H

#include "common.h"
#include "router.h"
#include "threadpool.h"

/* Register all routes */
void handlers_register_all(thread_pool_t *pool);

/* Auth */
void handle_post_register(http_request_t *req, http_response_t *res, const char *pp);
void handle_post_login   (http_request_t *req, http_response_t *res, const char *pp);
void handle_post_logout  (http_request_t *req, http_response_t *res, const char *pp);
void handle_get_me       (http_request_t *req, http_response_t *res, const char *pp);

/* Menu */
void handle_get_menu     (http_request_t *req, http_response_t *res, const char *pp);
void handle_get_menu_item(http_request_t *req, http_response_t *res, const char *pp);
void handle_post_menu    (http_request_t *req, http_response_t *res, const char *pp);
void handle_put_menu_item(http_request_t *req, http_response_t *res, const char *pp);

/* Orders */
void handle_post_order   (http_request_t *req, http_response_t *res, const char *pp);
void handle_get_orders   (http_request_t *req, http_response_t *res, const char *pp);
void handle_get_order    (http_request_t *req, http_response_t *res, const char *pp);
void handle_put_order_status(http_request_t *req, http_response_t *res, const char *pp);

/* Analytics & concurrency */
void handle_get_analytics   (http_request_t *req, http_response_t *res, const char *pp);
void handle_get_proc_logs   (http_request_t *req, http_response_t *res, const char *pp);
void handle_post_simulate   (http_request_t *req, http_response_t *res, const char *pp);

extern thread_pool_t *g_pool;

#endif /* HANDLERS_H */
