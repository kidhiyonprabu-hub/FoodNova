/*
 * FoodRush – Smart Online Food Ordering System
 * router.c – HTTP request parsing, routing and file serving
 */

#include "router.h"
#include "handlers.h"
#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define stat _stat
#define open _open
#define read _read
#define close _close
#define O_RDONLY _O_RDONLY
#define O_BINARY _O_BINARY
#endif

/* ── Route table ─────────────────────────────────────────────────────────── */
#define MAX_ROUTES 64
static route_t g_routes[MAX_ROUTES];
static int     g_route_count = 0;

void route_register(const char *method, const char *pattern,
                    route_handler_t handler, int require_auth)
{
    if (g_route_count >= MAX_ROUTES) return;
    route_t *r = &g_routes[g_route_count++];
    SAFE_STRNCPY(r->method,  method,  sizeof(r->method));
    SAFE_STRNCPY(r->pattern, pattern, sizeof(r->pattern));
    r->handler      = handler;
    r->require_auth = require_auth;
}

/* ── Response helpers ────────────────────────────────────────────────────── */
void respond_json(http_response_t *res, int status, const char *body)
{
    res->status = status;
    SAFE_STRNCPY(res->content_type, CT_JSON, sizeof(res->content_type));
    SAFE_STRNCPY(res->body, body, sizeof(res->body));
    res->body_len = (int)strlen(res->body);
}

void respond_error(http_response_t *res, int status, const char *msg)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"%s\"}", msg);
    respond_json(res, status, buf);
}

static const char *status_text(int code)
{
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

void send_response(sock_t client, http_response_t *res)
{
    char header[MAX_HEADER_LEN];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n",
        res->status, status_text(res->status),
        res->content_type, res->body_len);

#ifdef _WIN32
    send(client, header, hlen, 0);
    if (res->body_len > 0)
        send(client, res->body, res->body_len, 0);
#else
    write(client, header, hlen);
    if (res->body_len > 0)
        write(client, res->body, res->body_len);
#endif
}

/* ── File serving ────────────────────────────────────────────────────────── */
static const char *mime_for_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return CT_HTML;
    if (strcmp(dot,".html")==0) return CT_HTML;
    if (strcmp(dot,".css") ==0) return CT_CSS;
    if (strcmp(dot,".js")  ==0) return CT_JS;
    if (strcmp(dot,".png") ==0) return CT_PNG;
    if (strcmp(dot,".jpg") ==0 || strcmp(dot,".jpeg")==0) return CT_JPG;
    if (strcmp(dot,".ico") ==0) return CT_ICO;
    if (strcmp(dot,".svg") ==0) return CT_SVG;
    return CT_HTML;
}

void respond_file(sock_t client, const char *filepath)
{
    struct stat st;
    if (stat(filepath, &st) != 0) {
        http_response_t res = {0};
        respond_error(&res, 404, "File not found");
        send_response(client, &res);
        return;
    }

#ifdef _WIN32
    int fd = open(filepath, O_RDONLY | O_BINARY);
#else
    int fd = open(filepath, O_RDONLY);
#endif
    if (fd < 0) {
        http_response_t res = {0};
        respond_error(&res, 500, "Cannot open file");
        send_response(client, &res);
        return;
    }

    const char *mime = mime_for_path(filepath);
    const char *cache_control =
        (strcmp(mime, CT_HTML) == 0 || strcmp(mime, CT_CSS) == 0 ||
         strcmp(mime, CT_JS) == 0)
            ? "no-cache, must-revalidate"
            : "public, max-age=3600";
    char header[MAX_HEADER_LEN];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Cache-Control: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, (long)st.st_size, cache_control);

#ifdef _WIN32
    send(client, header, hlen, 0);
#else
    write(client, header, hlen);
#endif

    char fbuf[8192];
    int  n;
    while ((n = read(fd, fbuf, sizeof(fbuf))) > 0) {
#ifdef _WIN32
        send(client, fbuf, n, 0);
#else
        write(client, fbuf, n);
#endif
    }
    close(fd);
}

/* ── Request parser ──────────────────────────────────────────────────────── */
static void parse_request(const char *raw, http_request_t *req)
{
    memset(req, 0, sizeof(*req));
    const char *p = raw;

    /* Method */
    const char *sp = strchr(p, ' ');
    if (!sp) return;
    int mlen = (int)(sp - p);
    if (mlen >= (int)sizeof(req->method)) mlen = sizeof(req->method)-1;
    strncpy(req->method, p, mlen);
    req->method[mlen] = '\0';
    p = sp + 1;

    /* Path + query */
    sp = strchr(p, ' ');
    if (!sp) return;
    int plen = (int)(sp - p);
    char full_path[MAX_PATH_LEN];
    if (plen >= MAX_PATH_LEN) plen = MAX_PATH_LEN-1;
    strncpy(full_path, p, plen);
    full_path[plen] = '\0';

    char *qm = strchr(full_path, '?');
    if (qm) {
        *qm = '\0';
        SAFE_STRNCPY(req->query, qm+1, sizeof(req->query));
    }
    SAFE_STRNCPY(req->path, full_path, sizeof(req->path));
    p = sp + 1;

    /* Headers */
    while (*p) {
        const char *eol = strstr(p, "\r\n");
        if (!eol || eol == p) {
            if (eol) p = eol + 2; /* end of headers */
            break;
        }
        /* Authorization */
        if (strncasecmp(p, "Authorization:", 14) == 0) {
            const char *v = p + 14;
            while (*v == ' ') v++;
            int vlen = (int)(eol - v);
            if (vlen >= (int)sizeof(req->auth_token)) vlen = sizeof(req->auth_token)-1;
            strncpy(req->auth_token, v, vlen);
            req->auth_token[vlen] = '\0';
        }
        /* Content-Type */
        if (strncasecmp(p, "Content-Type:", 13) == 0) {
            const char *v = p + 13;
            while (*v == ' ') v++;
            int vlen = (int)(eol - v);
            if (vlen >= (int)sizeof(req->content_type)) vlen = sizeof(req->content_type)-1;
            strncpy(req->content_type, v, vlen);
            req->content_type[vlen] = '\0';
        }
        p = eol + 2;
    }

    /* Body – after blank line */
    const char *body_start = strstr(raw, "\r\n\r\n");
    int sep_len = 4;
    if (!body_start) {
        body_start = strstr(raw, "\n\n");
        sep_len = 2;
    }
    if (body_start) {
        body_start += sep_len;
        int blen = (int)strlen(body_start);
        if (blen >= MAX_BODY_LEN) blen = MAX_BODY_LEN-1;
        strncpy(req->body, body_start, blen);
        req->body[blen] = '\0';
        req->body_len = blen;
    }
}

/* ── URL decode ──────────────────────────────────────────────────────────── */
static void url_decode(const char *src, char *dst, int len)
{
    int j = 0;
    for (int i = 0; src[i] && j < len-1; i++) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char h[3] = {src[i+1], src[i+2], '\0'};
            dst[j++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

/* ── Route matching ──────────────────────────────────────────────────────── */
/*
 * Patterns ending with '/' match any path with that prefix.
 * Exact patterns match exactly.
 * Path param = the portion after the pattern prefix.
 */
static int match_route(const route_t *r, const char *method, const char *path,
                       char *param_out, int param_len)
{
    if (strcmp(r->method, method) != 0) return 0;
    int plen = (int)strlen(r->pattern);
    if (r->pattern[plen-1] == '/') {
        /* prefix match */
        if (strncmp(path, r->pattern, plen) == 0) {
            SAFE_STRNCPY(param_out, path + plen, param_len);
            return 1;
        }
    } else {
        if (strcmp(path, r->pattern) == 0) {
            if (param_out) param_out[0] = '\0';
            return 1;
        }
    }
    return 0;
}

/* ── Dispatcher ──────────────────────────────────────────────────────────── */
void router_dispatch(sock_t client, http_request_t *req)
{
    /* CORS preflight */
    if (strcmp(req->method, "OPTIONS") == 0) {
        http_response_t res = {0};
        res.status = 204;
        SAFE_STRNCPY(res.content_type, CT_JSON, sizeof(res.content_type));
        send_response(client, &res);
        return;
    }

    /* API routes */
    if (strncmp(req->path, "/api/", 5) == 0) {
        char param[MAX_PATH_LEN] = "";
        for (int i = 0; i < g_route_count; i++) {
            if (match_route(&g_routes[i], req->method, req->path,
                            param, sizeof(param))) {
                http_response_t res = {0};
                g_routes[i].handler(req, &res, *param ? param : NULL);
                send_response(client, &res);
                return;
            }
        }
        http_response_t res = {0};
        respond_error(&res, 404, "API endpoint not found");
        send_response(client, &res);
        return;
    }

    /* Static file serving from frontend/ */
    char filepath[MAX_PATH_LEN];
    const char *path = req->path;

    /* Decode URL */
    char decoded[MAX_PATH_LEN];
    url_decode(path, decoded, sizeof(decoded));

    /* Map / -> index.html */
    if (strcmp(decoded, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "frontend/index.html");
    } else {
        /* Strip leading / */
        const char *rel = decoded + 1;
        snprintf(filepath, sizeof(filepath), "frontend/%s", rel);
    }

    /* Security: reject paths containing .. */
    if (strstr(filepath, "..")) {
        http_response_t res = {0};
        respond_error(&res, 403, "Forbidden");
        send_response(client, &res);
        return;
    }

    respond_file(client, filepath);
}

/* ── Connection handler (called from thread) ─────────────────────────────── */
typedef struct {
    sock_t client;
} conn_arg_t;

static void handle_connection(void *arg)
{
    conn_arg_t *ca = (conn_arg_t *)arg;
    sock_t client  = ca->client;
    free(ca);

    char buf[READ_BUF_SIZE];
    int total_bytes = 0;
    int content_len = 0;
    int header_end_offset = -1;

#ifdef _WIN32
    DWORD timeout = 4000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv = {4, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    while (total_bytes < (int)sizeof(buf) - 1) {
#ifdef _WIN32
        int n = recv(client, buf + total_bytes, sizeof(buf) - 1 - total_bytes, 0);
#else
        int n = (int)read(client, buf + total_bytes, sizeof(buf) - 1 - total_bytes);
#endif
        if (n <= 0) break;
        total_bytes += n;
        buf[total_bytes] = '\0';

        /* Check for header boundary */
        if (header_end_offset < 0) {
            char *hdr_end = strstr(buf, "\r\n\r\n");
            int sep_len = 4;
            if (!hdr_end) {
                hdr_end = strstr(buf, "\n\n");
                sep_len = 2;
            }
            if (hdr_end) {
                header_end_offset = (int)(hdr_end - buf) + sep_len;

                /* Parse Content-Length if present */
                char *cl = strstr(buf, "Content-Length:");
                if (!cl) cl = strstr(buf, "content-length:");
                if (!cl) cl = strstr(buf, "Content-length:");
                if (cl && cl < hdr_end) {
                    cl += 15;
                    while (*cl == ' ' || *cl == '\t') cl++;
                    content_len = atoi(cl);
                }
            }
        }

        /* If headers are received, check if the full body is received */
        if (header_end_offset >= 0) {
            int body_received = total_bytes - header_end_offset;
            if (body_received >= content_len) {
                break;
            }
        }
    }

    if (total_bytes <= 0) { CLOSE_SOCK(client); return; }
    buf[total_bytes] = '\0';

    http_request_t req;
    parse_request(buf, &req);

    if (!*req.method) { CLOSE_SOCK(client); return; }

    LOG_INFO("%s %s", req.method, req.path);
    router_dispatch(client, &req);
    CLOSE_SOCK(client);
}

static thread_pool_t *g_http_pool = NULL;

void router_init(thread_pool_t *pool)
{
    g_http_pool = pool;
}

/* Called by main accept loop */
void router_handle_client(sock_t client)
{
    conn_arg_t *ca = (conn_arg_t *)malloc(sizeof(conn_arg_t));
    if (!ca) { CLOSE_SOCK(client); return; }
    ca->client = client;
    if (g_http_pool && pool_submit(g_http_pool, handle_connection, ca) == 0)
        return;
    /* Fall back to direct handling */
    handle_connection(ca);
}
