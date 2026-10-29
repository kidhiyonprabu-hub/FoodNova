/*
 * FoodRush – Smart Online Food Ordering System
 * json_utils.h – Lightweight JSON builder and parser helpers
 */
#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include "common.h"
#include "database.h"

/* ── JSON escape ─────────────────────────────────────────────────────────── */
void json_escape(const char *src, char *dst, int dst_len);

/* ── JSON builders ───────────────────────────────────────────────────────── */
int  json_menu_item(const db_menu_item_t *m, char *buf, int buf_len);
int  json_menu_items(const db_menu_item_t *items, int count, char *buf, int buf_len);

int  json_order(const db_order_t *o, char *buf, int buf_len);
int  json_orders(const db_order_t *orders, int count, char *buf, int buf_len);

int  json_user(const db_user_t *u, char *buf, int buf_len);
int  json_analytics(const db_analytics_t *a, int active, int queued,
                    int completed, int failed, char *buf, int buf_len);

int  json_proc_logs(const db_proc_log_t *logs, int count, char *buf, int buf_len);

/* ── Tiny JSON parser helpers ────────────────────────────────────────────── */
/* Extract the string value for a key in a flat JSON object */
int  json_get_string(const char *json, const char *key, char *val, int val_len);
int  json_get_int(const char *json, const char *key, int *val);
int  json_get_double(const char *json, const char *key, double *val);

/* ── Simple wrapper to build {"status":"ok","data":{...}} responses ──────── */
int  json_ok(const char *data_json, char *buf, int buf_len);
int  json_err(const char *message, char *buf, int buf_len);

#endif /* JSON_UTILS_H */
