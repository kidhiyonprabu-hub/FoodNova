/*
 * FoodRush – Smart Online Food Ordering System
 * database.h – SQLite database layer interface
 */
#ifndef DATABASE_H
#define DATABASE_H

#include "common.h"
#include <sqlite3.h>

#define DB_PATH "db/foodrush.db"
#define MAX_MENU_ITEMS 256
#define MAX_ORDER_ITEMS 32
#define MAX_ORDERS 1024

/* ── Data model structs ─────────────────────────────────────────────────── */

typedef struct {
    int   id;
    char  username[MAX_USERNAME_LEN];
    char  email[MAX_EMAIL_LEN];
    char  password_hash[129];   /* SHA-256 hex */
    char  full_name[MAX_NAME_LEN];
    char  phone[MAX_PHONE_LEN];
    char  address[MAX_ADDRESS_LEN];
    int   role;                  /* user_role_t */
    char  created_at[32];
} db_user_t;

typedef struct {
    int    id;
    char   name[MAX_NAME_LEN];
    char   description[MAX_DESC_LEN];
    char   category[64];
    double price;
    int    stock;
    double rating;
    int    rating_count;
    char   image_url[512];
    int    is_available;
    char   created_at[32];
} db_menu_item_t;

typedef struct {
    int    menu_item_id;
    char   item_name[MAX_NAME_LEN];
    int    quantity;
    double unit_price;
    double subtotal;
} db_order_item_t;

typedef struct {
    int              id;
    char             order_id[ORDER_ID_LEN];
    int              user_id;
    char             customer_name[MAX_NAME_LEN];
    char             customer_email[MAX_EMAIL_LEN];
    char             customer_phone[MAX_PHONE_LEN];
    char             delivery_address[MAX_ADDRESS_LEN];
    double           total_amount;
    int              status;           /* order_status_t */
    char             status_str[64];
    char             created_at[32];
    char             updated_at[32];
    db_order_item_t  items[MAX_ORDER_ITEMS];
    int              item_count;
    int              worker_thread_id;
    int              processing_time_ms;
} db_order_t;

typedef struct {
    int    id;
    char   order_id[ORDER_ID_LEN];
    int    thread_id;
    char   action[256];
    char   result[64];
    char   timestamp[32];
    int    duration_ms;
} db_proc_log_t;

/* ── Initialisation ─────────────────────────────────────────────────────── */
int  db_init(void);
void db_close(void);

/* ── Users ──────────────────────────────────────────────────────────────── */
result_t db_create_user(const char *username, const char *email,
                        const char *password_plain, const char *full_name,
                        const char *phone, const char *address, int role,
                        int *out_id);
result_t db_authenticate_user(const char *username, const char *password_plain,
                              db_user_t *out_user);
result_t db_get_user_by_id(int id, db_user_t *out_user);

/* ── Menu ───────────────────────────────────────────────────────────────── */
result_t db_get_menu_items(db_menu_item_t *items, int *count);
result_t db_get_menu_item_by_id(int id, db_menu_item_t *out);
result_t db_create_menu_item(db_menu_item_t *item, int *out_id);
result_t db_update_menu_item(db_menu_item_t *item);
result_t db_update_menu_stock(int item_id, int delta, sqlite3 *db);

/* ── Orders ─────────────────────────────────────────────────────────────── */
result_t db_create_order(db_order_t *order);
result_t db_get_orders(db_order_t *orders, int *count, int user_id);
result_t db_get_order_by_id(const char *order_id, db_order_t *out);
result_t db_update_order_status(const char *order_id, order_status_t status);

/* ── Processing logs ────────────────────────────────────────────────────── */
result_t db_log_processing(const char *order_id, int thread_id,
                           const char *action, const char *res, int dur_ms);
result_t db_get_proc_logs(db_proc_log_t *logs, int *count);

/* ── Analytics ──────────────────────────────────────────────────────────── */
typedef struct {
    int    total_orders;
    int    pending_orders;
    int    delivered_orders;
    double total_revenue;
    int    total_menu_items;
    int    low_stock_items;
    int    active_workers;
    int    queued_tasks;
    int    completed_tasks;
    int    failed_tasks;
} db_analytics_t;

result_t db_get_analytics(db_analytics_t *out);

/* ── Session token store (in-memory) ────────────────────────────────────── */
typedef struct {
    char  token[128];
    int   user_id;
    int   role;
    time_t expires;
} session_t;

void     session_store_init(void);
char    *session_create(int user_id, int role);   /* returns static token */
session_t *session_lookup(const char *token);
void     session_destroy(const char *token);

#endif /* DATABASE_H */
