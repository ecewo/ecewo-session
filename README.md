# ecewo-session

The session management system provides secure server-side storage for user data. Each user receives a unique, cryptographically-secure session ID sent via HTTP cookies.

## Table of Contents

1. [Setup](#setup)
2. [API Reference](#api-reference)
3. [Examples](#examples)

## Setup

```c
// main.c
#include "ecewo.h"
#include "ecewo-session.h"
#include <stdio.h>

void cleanup_app(void) {
    session_cleanup(); // Cleanup session system
}

int main(void) {
    server_init();

    // Initialize session system
    if (!session_init()) {
        fprintf(stderr, "Failed to initialize session system!\n");
        return 1;
    }

    server_atexit(cleanup_app);
    server_listen(3000);
    server_run();
    return 0;
}
```

> [!NOTE]
> Session module depends on [ecewo-cookie](https://github.com/ecewo/ecewo-cookie). You need both modules to use sessions.

## API Reference

### `session_init()`

Initializes the session system with background cleanup timer.
```c
int session_init(void)
```

**Returns:** `1` on success, `0` on error  
**Note:** Must be called once at program startup before any session operations

**Example:**
```c
if (!session_init()) {
    fprintf(stderr, "CRITICAL: Session initialization failed\n");
    return 1;
}
```

---

### `session_cleanup()`

Stops cleanup timer and frees all sessions.
```c
void session_cleanup(void)
```

**Note:** Should be called in `server_atexit()` callback

**Example:**
```c
void cleanup_app(void) {
    session_cleanup();
    // ... other cleanup
}

server_atexit(cleanup_app);
```

---

### `session_create()`

Creates a new session.
```c
Session *session_create(int max_age)
```

**Parameters:**
- `max_age` - Session validity duration in seconds

**Returns:** Session pointer or `NULL` on error

**Example:**
```c
// Create session with 1 hour (3600 seconds) validity
Session *sess = session_create(3600);
if (!sess) {
    send_text(res, INTERNAL_SERVER_ERROR, "Failed to create session");
    return;
}
```

---

### `session_find()`

Finds an active session by ID with automatic expiry check.
```c
Session *session_find(const char *id)
```

**Parameters:**
- `id` - Session ID to search for

**Returns:** Session pointer or `NULL` if not found/expired

**Features:**
- Automatically renews session expiry on access (sliding window)
- Returns `NULL` for expired sessions

**Example:**
```c
const char *session_id = "A7x9KmN2..."; // From cookie
Session *sess = session_find(session_id);
if (!sess) {
    send_text(res, UNAUTHORIZED, "Session expired or invalid");
    return;
}
```

---

### `session_regenerate()`

Generates new session ID while preserving data and expiry.
```c
int session_regenerate(Session *sess)
```

**Parameters:**
- `sess` - Session to regenerate

**Returns:** `1` on success, `0` on error

**Use case:** Prevent session fixation attacks after authentication

**Example:**
```c
void handle_login(Req *req, Res *res) {
    // ... authenticate user ...
    
    Session *sess = session_get(req);
    if (sess) {
        // Regenerate ID after login for security
        if (!session_regenerate(sess)) {
            send_text(res, INTERNAL_SERVER_ERROR, "Session regeneration failed");
            return;
        }
        session_send(res, sess, NULL);
    }
    
    send_text(res, OK, "Logged in successfully");
}
```

---

### `session_value_set()`

Stores a key-value pair in the session.
```c
int session_value_set(Session *sess, const char *key, const char *value)
```

**Parameters:**
- `sess` - Target session
- `key` - Key name (automatically URL-encoded)
- `value` - Value to store (automatically URL-encoded)

**Returns:** `1` on success, `0` on error

**Features:**
- Overwrites if key already exists
- Enforces 4KB total data limit per session
- Thread-safe

**Example:**
```c
Session *sess = session_create(3600);

session_value_set(sess, "user_id", "12345");
session_value_set(sess, "username", "john_doe");
session_value_set(sess, "email", "john@example.com");
session_value_set(sess, "role", "admin");

// Special characters are handled automatically
session_value_set(sess, "note", "Hello, World! 你好");
```

---

### `session_value_get()`

Retrieves a value from the session.
```c
char *session_value_get(Session *sess, const char *key, Arena *arena)
```

**Parameters:**
- `sess` - Source session
- `key` - Key to retrieve
- `arena` - Arena allocator for memory management, or `NULL` for `malloc`

**Returns:** Decoded value or `NULL` if not found

**Memory management:**
- If `arena` is provided: Returns arena-allocated string (no manual free needed)
- If `arena` is `NULL`: Returns malloc'd string (**caller must free**)

**Example:**
```c
// Using arena (recommended)
void handle_profile(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Not logged in");
        return;
    }
    
    // Arena-allocated - automatically freed with request
    char *username = session_value_get(sess, "username", req->arena);
    if (username) {
        char *response = arena_sprintf(req->arena, "Welcome, %s!", username);
        send_text(res, OK, response);
    }
}

// Using malloc (requires manual free)
void handle_profile(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Not logged in");
        return;
    }
    
    char *username = session_value_get(sess, "username", NULL);
    if (username) {
        printf("Username: %s\n", username);
        free(username);  // Must free
    }
    
    send_text(res, OK, "OK!");
}
```

---

### `session_value_remove()`

Removes a key-value pair from the session.
```c
int session_value_remove(Session *sess, const char *key)
```

**Parameters:**
- `sess` - Target session
- `key` - Key to remove

**Returns:** `1` on success, `0` on error

**Example:**
```c
void handle_remove_cart_item(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Unauthorized");
        return;
    }
    
    const char *item_id = get_query(req, "item");
    session_value_remove(sess, item_id);
    
    send_text(res, OK, "Item removed from cart");
}
```

---

### `session_get()`

Extracts session from request cookie.
```c
Session *session_get(Req *req)
```

**Parameters:**
- `req` - HTTP request object

**Returns:** Session pointer or `NULL` if no valid session

**Example:**
```c
void handle_protected_route(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Authentication required");
        return;
    }
    
    char *role = session_value_get(sess, "role", req->arena);
    if (role && strcmp(role, "admin") == 0) {
        send_text(res, OK, "Admin access granted");
    } else {
        send_text(res, FORBIDDEN, "Admin access required");
    }
}
```

---

### `session_send()`

Sends session cookie to client.
```c
void session_send(Res *res, Session *sess, Cookie *options)
```

**Parameters:**
- `res` - HTTP response object
- `sess` - Session to send
- `options` - Cookie options (or `NULL` for defaults)

**Default cookie settings:**
- `max_age` - Calculated from session expiry
- `path` - `/`
- `same_site` - `Lax`
- `http_only` - `true`
- `secure` - `false` (set to `true` for production HTTPS)

**Example:**
```c
void handle_login(Req *req, Res *res) {
    // User authentication...

    Session *sess = session_create(7200); // 2 hours
    session_value_set(sess, "user_id", "12345");
    session_value_set(sess, "username", "john_doe");

    Cookie options = {
        .path = "/",
        .domain = NULL,          // Same domain only
        .same_site = "Strict",   // CSRF protection
        .http_only = true,       // XSS protection
        .secure = true           // HTTPS only (production)
    };

    session_send(res, sess, &options);
    send_text(res, OK, "Login successful");
}
```

---

### `session_destroy()`

Destroys session on both server and client.
```c
void session_destroy(Res *res, Session *sess, Cookie *options)
```

**Parameters:**
- `res` - HTTP response object
- `sess` - Session to destroy
- `options` - Cookie options (or `NULL` for defaults)

**Actions:**
1. Sends expired cookie to client (max_age=0)
2. Frees session data from server memory

**Example:**
```c
void handle_logout(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (sess) {
        session_destroy(res, sess, NULL);
    }
    
    send_text(res, OK, "Logged out successfully");
}
```

---

### `session_free()`

Frees a session from server memory only (no cookie sent).
```c
void session_free(Session *sess)
```

**Parameters:**
- `sess` - Session to free

**Use case:** Backend session cleanup (e.g., after database persistence)

**Example:**
```c
void save_session_to_database(Session *sess) {
    // Serialize session data
    char *user_id = session_value_get(sess, "user_id", NULL);
    // ... save to database ...
    free(user_id);
    
    // Free from memory after persisting
    session_free(sess);
}
```

---

### `session_print_all()`

Prints all active sessions to console (debugging only).
```c
void session_print_all(void)
```

**Output format:**
```
=== Sessions ===
[#00] id=A7x9KmN2..., expires in 3540s
      username = john_doe
      user_id = 12345
      logged_in = true
[#01] id=B2q8RtM5..., expires in 7123s
      cart_item_101 = 2
      cart_item_205 = 1
================
```

**Example:**
```c
#ifdef ECEWO_DEBUG
void handle_debug_sessions(Req *req, Res *res) {
    session_print_all();
    send_text(res, OK, "Sessions printed to console");
}
#endif
```

## Examples

### Complete Authentication Flow
```c
#include "ecewo.h"
#include "ecewo-session.h"
#include "ecewo-cookie.h"

void handle_login(Req *req, Res *res) {
    const char *username = get_query(req, "username");
    const char *password = get_query(req, "password");
    
    // Validate credentials (example only)
    if (!username || !password || 
        strcmp(username, "admin") != 0 || 
        strcmp(password, "secret") != 0) {
        send_text(res, UNAUTHORIZED, "Invalid credentials");
        return;
    }
    
    // Create session
    Session *sess = session_create(3600); // 1 hour
    if (!sess) {
        send_text(res, INTERNAL_SERVER_ERROR, "Session creation failed");
        return;
    }
    
    session_value_set(sess, "user_id", "1");
    session_value_set(sess, "username", username);
    session_value_set(sess, "role", "admin");
    
    Cookie options = {
        .path = "/",
        .same_site = "Strict",
        .http_only = true,
        .secure = true  // HTTPS in production
    };
    
    session_send(res, sess, &options);
    send_text(res, OK, "Login successful");
}

void handle_dashboard(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Please log in");
        return;
    }
    
    char *username = session_value_get(sess, "username", req->arena);
    char *response = arena_sprintf(req->arena, 
        "<h1>Welcome, %s!</h1>", username);
    
    send_html(res, OK, response);
}

void handle_logout(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (sess) {
        session_destroy(res, sess, NULL);
    }
    
    send_text(res, OK, "Logged out");
}

int main(void) {
    server_init();
    
    if (!session_init()) {
        fprintf(stderr, "Session init failed\n");
        return 1;
    }
    
    server_atexit(session_cleanup);
    
    get("/login", handle_login, NULL);
    get("/dashboard", handle_dashboard, NULL);
    get("/logout", handle_logout, NULL);
    
    server_listen(3000);
    server_run();
    return 0;
}
```

### Shopping Cart with Sessions
```c
void handle_add_to_cart(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        // Create anonymous cart session
        sess = session_create(86400); // 24 hours
        if (!sess) {
            send_text(res, INTERNAL_SERVER_ERROR, "Failed to create cart");
            return;
        }
    }
    
    const char *item_id = get_query(req, "item");
    const char *quantity = get_query(req, "qty");
    
    if (!item_id || !quantity) {
        send_text(res, BAD_REQUEST, "Missing item or quantity");
        return;
    }
    
    // Store cart item
    char key[64];
    snprintf(key, sizeof(key), "cart_%s", item_id);
    session_value_set(sess, key, quantity);
    
    session_send(res, sess, NULL);
    send_text(res, OK, "Item added to cart");
}

void handle_view_cart(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_json(res, OK, "{\"items\":[]}");
        return;
    }
    
    // In real app, iterate through cart items from session
    char *item1_qty = session_value_get(sess, "cart_101", req->arena);
    char *item2_qty = session_value_get(sess, "cart_205", req->arena);
    
    char *response = arena_sprintf(req->arena,
        "{\"items\":[{\"id\":101,\"qty\":%s},{\"id\":205,\"qty\":%s}]}",
        item1_qty ? item1_qty : "0",
        item2_qty ? item2_qty : "0");
    
    send_json(res, OK, response);
}
```

### Session Regeneration for Security
```c
void handle_login_with_regeneration(Req *req, Res *res) {
    // Check if already logged in
    Session *sess = session_get(req);
    if (sess) {
        // User has existing session - regenerate for security
        if (!session_regenerate(sess)) {
            send_text(res, INTERNAL_SERVER_ERROR, "Security update failed");
            return;
        }
    } else {
        // New login - create session
        sess = session_create(3600);
        if (!sess) {
            send_text(res, INTERNAL_SERVER_ERROR, "Failed to create session");
            return;
        }
    }
    
    // ... authenticate user ...
    
    session_value_set(sess, "authenticated", "true");
    session_value_set(sess, "user_id", "12345");
    
    session_send(res, sess, NULL);
    send_text(res, OK, "Login successful");
}

void handle_privilege_escalation(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Not logged in");
        return;
    }
    
    // Before granting admin privileges, regenerate session ID
    // This prevents session fixation attacks
    if (!session_regenerate(sess)) {
        send_text(res, INTERNAL_SERVER_ERROR, "Security update failed");
        return;
    }
    
    session_value_set(sess, "role", "admin");
    session_send(res, sess, NULL);
    
    send_text(res, OK, "Admin privileges granted");
}
```

### Multi-Factor Authentication with Sessions
```c
void handle_mfa_initiate(Req *req, Res *res) {
    // User provided username/password
    const char *username = get_query(req, "username");
    // ... verify credentials ...
    
    // Create temporary session for MFA flow
    Session *sess = session_create(300); // 5 minutes only
    session_value_set(sess, "mfa_pending", "true");
    session_value_set(sess, "username", username);
    
    // Send MFA code via email/SMS
    // ...
    
    session_send(res, sess, NULL);
    send_text(res, OK, "MFA code sent");
}

void handle_mfa_verify(Req *req, Res *res) {
    Session *sess = session_get(req);
    if (!sess) {
        send_text(res, UNAUTHORIZED, "Session expired");
        return;
    }
    
    char *pending = session_value_get(sess, "mfa_pending", req->arena);
    if (!pending || strcmp(pending, "true") != 0) {
        send_text(res, BAD_REQUEST, "MFA not initiated");
        return;
    }
    
    const char *code = get_query(req, "code");
    // ... verify MFA code ...
    
    // Upgrade to full session
    session_value_remove(sess, "mfa_pending");
    session_value_set(sess, "authenticated", "true");
    
    // Extend session and regenerate ID for security
    Session *new_sess = session_create(3600); // Full 1-hour session
    char *username = session_value_get(sess, "username", NULL);
    session_value_set(new_sess, "username", username);
    session_value_set(new_sess, "authenticated", "true");
    free(username);
    
    // Destroy temporary session
    session_free(sess);
    
    session_send(res, new_sess, NULL);
    send_text(res, OK, "MFA verified");
}
```

### Session Persistence to Database
```c
#include "ecewo-postgres.h"

void persist_session_to_db(Session *sess, PGpool *pool) {
    // Get session data
    char *user_id = session_value_get(sess, "user_id", NULL);
    if (!user_id) {
        session_free(sess);
        return;
    }
    
    // Create database query
    PGquery *pg = pg_query_create(pool, NULL);
    
    const char *params[] = {sess->id, user_id, "3600"};
    pg_query_queue(pg, 
        "INSERT INTO sessions (id, user_id, expires_at) VALUES ($1, $2, NOW() + INTERVAL '1 hour')",
        3, params, NULL, NULL);
    
    pg_query_exec(pg);
    
    free(user_id);
    
    // Free from memory - now in database
    session_free(sess);
}

void load_session_from_db(const char *session_id, PGpool *pool) {
    // Query database
    PGquery *pg = pg_query_create(pool, NULL);
    
    const char *params[] = {session_id};
    pg_query_queue(pg,
        "SELECT user_id, expires_at FROM sessions WHERE id = $1 AND expires_at > NOW()",
        1, params, NULL, NULL);
    
    // ... handle result and recreate session in memory ...
    
    pg_query_exec(pg);
}
```
