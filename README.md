# FoodRush – Smart Online Food Ordering System
> **Micro Project – 15: Develop an Online Food Ordering System using Multi-threading Model**  
> **Course Outcome Focus:** CO1 (OS Architecture & Threads), CO4 (Memory & Security Mechanisms), CO5 (I/O, Sockets & Concurrency)

---

## 🚀 Quick Start: How to Run the Project

### Option 1: One-Click Run (Recommended)
Simply double-click or run the included launcher in terminal:
```cmd
run.bat
```
This automatically configures the environment PATH, verifies/builds the binary if needed, opens your default web browser to `http://localhost:8080`, and starts the FoodRush server.

---

### Option 2: Build & Run from Command Line

#### 1. Compile the C Backend
```cmd
build.bat
```
This compiles all C modules (`main.c`, `threadpool.c`, `database.c`, `json_utils.c`, `router.c`, `handlers.c`) using GCC and produces `foodrush.exe`.

#### 2. Start the Server
```cmd
set PATH=C:\msys64\ucrt64\bin;%PATH%
foodrush.exe
```

#### 3. Open in Browser
Navigate to:
```
http://localhost:8080
```

---

## 🔑 Default Login Credentials

| Role | Username | Password | Privileges |
| :--- | :--- | :--- | :--- |
| **Admin** | `admin` | `admin123` | Full dashboard, stock management, order stress simulator |
| **Customer** | `john` | `password123` | Cart, placing orders, real-time live thread tracking |

*(You can also use the One-Click Demo buttons inside the Sign-In dialog)*

## 🍽️ Using the App

1. Run `run.bat` and open `http://localhost:8080`.
2. Browse the menu. Use the category tabs, search box, and sort menu to find food.
3. Select **Add to Cart**, open **Cart**, adjust quantities, then choose **Checkout**.
4. Enter your name, phone, email, and delivery address, then place the order.
5. In **Live Order Processing Pipeline**, choose **Track** to view the order status and delivery progress. The tracker refreshes from the backend every 4 seconds. Close any open dashboard/modal first if it covers the order list.
6. Once an order is delivered, **Remove** hides it from this browser's order list; it does not delete the database record.

Every seeded menu item has its own locally stored SVG food illustration under `frontend/images/food/`. Menu and cart images therefore work without external image services or an internet connection.

### What “OS Threads” Does

The **OS Threads** button opens a backend monitoring dashboard. FoodRush creates 8 worker threads and a bounded queue of 128 tasks. When an order is placed, it is added to that queue; a worker processes it and stores it in SQLite. The dashboard shows worker/queue activity and recent processing logs. It is for monitoring the Micro Project's multi-threading model, not for placing or tracking a delivery.

The tracker map/rider illustration is a UI simulation, not GPS. The current backend confirms an order after processing; later kitchen, rider, and delivered stages only appear if the backend order status is updated. This project does not have a real rider location feed.

---

## 🏗️ Architecture & OS Concepts (CO Mapping)

```
       [ HTTP Clients / Browsers ]
                   │
                   ▼  (TCP Sockets - Winsock2 / BSD)
         ┌───────────────────┐
         │   HTTP Dispatcher │
         │    (router.c)     │
         └─────────┬─────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
 ┌───────────────┐     ┌──────────────┐
 │ Static Files  │     │  REST APIs   │
 │ (HTML/CSS/JS) │     │  (/api/...)  │
 └───────────────┘     └──────┬───────┘
                              │
               [ Bounded Circular Task Queue ]
                              │ (pthread_mutex_t + pthread_cond_t)
                              ▼
        ┌───────────────────────────────────────────┐
        │        POSIX Thread Pool (8 Workers)      │
        │  [Worker-1] [Worker-2] ... [Worker-8]     │
        └─────────────────────┬─────────────────────┘
                              │
                     (SQLite3 C API WAL)
                              ▼
                     [ foodrush.db (Disk) ]
```

### 1. CO1: Operating System Architecture & Concurrency
- **Multi-threading Model:** Dedicated worker thread pool (`src/threadpool.c`) using POSIX threads (`pthread_create`, `pthread_join`).
- **Work Queue:** Synchronized circular buffer with producer-consumer architecture.
- **Hardware Interaction:** Non-blocking multi-core dispatch across 8 worker threads.

### 2. CO4: Virtual Memory, Synchronization & Security
- **Thread Synchronization:** `pthread_mutex_t` and `pthread_cond_t` for race-condition-free state updates.
- **Directory Traversal Protection:** Canonical path sanitization preventing `..` escapes.
- **Session Security:** Cryptographic token generation for authenticated session state.

### 3. CO5: I/O Sockets & Database Virtualization
- **Network I/O:** Pure C TCP socket server (`bind`, `listen`, `accept`, `send`, `recv`).
- **Database Engine:** Embedded SQLite3 accessed through native C bindings with WAL (Write-Ahead Logging) concurrency.

---

## 📡 REST API Endpoints

- **`GET /`** – Serve web interface (`frontend/index.html`)
- **`GET /api/menu`** – Fetch categorized dishes and current stock
- **`POST /api/orders`** – Submit order; enqueues task to worker thread pool
- **`GET /api/orders`** – Query order pipeline and thread assignments; the open tracker polls for backend status updates every 4 seconds
- **`GET /api/analytics`** – Real-time thread pool performance telemetry
- **`GET /api/logs`** – Live execution event stream from the worker pool
- **`POST /api/simulate`** – Concurrency stress test (spawns 10–50 parallel orders)
- **`POST /api/auth/login`** – User and Admin authentication
