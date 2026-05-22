# ecewo-session

The session management system provides secure server-side storage for user data. Each user receives a unique, cryptographically-secure session ID sent via HTTP cookies.

## Table of Contents

1. [Installation](#installation)
2. [Setup](#setup)
3. [API Reference](#api-reference)
4. [Examples](#examples)

## Installation

Add the plugins to your `CMakeLists.txt`:

```cmake
ecewo_add(
    cookie@v0.2.0
    session@v0.2.0
)

target_link_libraries(app PRIVATE
    ecewo::ecewo
    ecewo::cookie
    ecewo::session
)
```

> [!NOTE]
> The session plugin depends on [ecewo-cookie](https://github.com/ecewo/ecewo-cookie). You need both plugins to use sessions.

## Setup

The session store is owned by the application: it lives in the app's plugin data
and is allocated from the app arena. Initialize it once during setup, on the
event-loop thread, before `ecewo_listen()`:

```c
// main.c
#include "ecewo.h"
#include "ecewo-session.h"
#include <stdio.h>

int main(void) {
    ecewo_app_t *app = ecewo_create();
    if (!app)
        return 1;

    // Initialize the session store for this app.
    if (ecewo_session_init(app) != 0) {
        fprintf(stderr, "Failed to initialize session system!\n");
        return 1;
    }

    // ... register routes with ECEWO_GET(app, ...), etc. ...

    ecewo_listen(app, 3000);
    return 0;
}
```

Teardown is automatic: `ecewo_session_init()` registers an `ecewo_atexit()`
callback that stops the cleanup timer and frees every session during graceful
shutdown. There is no separate cleanup call.

## API Reference

### `ecewo_session_init()`

Initializes the per-app session store and starts the background cleanup timer.

```c
int ecewo_session_init(ecewo_app_t *app)
```

**Parameters:**
- `app` - The application instance

**Returns:** `0` on success, `-1` on failure
**Notes:**
- Call once during app setup, before `ecewo_listen()` / `ecewo_run()`, on the event-loop thread.
- Idempotent: a second call on the same app is a no-op.
- Teardown is automatic via `ecewo_atexit()`.
- The session store starts with a capacity of 10 sessions and doubles automatically when full.
- A background cleanup timer runs every 60 seconds to evict expired sessions.

**Example:**
```c
if (ecewo_session_init(app) != 0) {
    fprintf(stderr, "CRITICAL: Session initialization failed\n");
    return 1;
}
```

---

### `ecewo_session_create()`

Creates a new session in the given app's store.

```c
ecewo_session_t *ecewo_session_create(ecewo_app_t *app, int max_age)
```

**Parameters:**
- `app` - The application instance (use `ecewo_req_app(req)` inside a handler)
- `max_age` - Session validity duration in seconds

**Returns:** Session handle or `NULL` on failure

**Notes:**
- Session IDs are 32-character URL-safe strings (`A-Z a-z 0-9 - _`) generated from a cryptographically secure source.
- Thread-safe.

**Example:**
```c
void handle_login(ecewo_request_t *req, ecewo_response_t *res) {
    // Create a session valid for 1 hour
    ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), 3600);
    if (!sess) {
        ecewo_send_text(res, 500, "Failed to create session");
        return;
    }
    // ...
}
```

---

### `ecewo_session_find()`

Finds an active session by ID with an automatic expiry check.

```c
ecewo_session_t *ecewo_session_find(ecewo_app_t *app, const char *id)
```

**Parameters:**
- `app` - The application instance
- `id` - Session ID to search for

**Returns:** Session handle or `NULL` if not found or expired

**Notes:**
- Thread-safe.

**Example:**
```c
ecewo_session_t *found = ecewo_session_find(ecewo_req_app(req), id);
if (!found) {
    ecewo_send_text(res, 401, "Session expired or invalid");
    return;
}
```

---

### `ecewo_session_id()`

Gets the session ID string.

```c
const char *ecewo_session_id(const ecewo_session_t *sess)
```

**Parameters:**
- `sess` - Session handle

**Returns:** Session ID string or `NULL` if the session is invalid

**Example:**
```c
ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), 3600);
const char *id = ecewo_session_id(sess);
```

---

### `ecewo_session_regenerate()`

Generates a new session ID while preserving data and expiry.

```c
int ecewo_session_regenerate(ecewo_session_t *sess)
```

**Parameters:**
- `sess` - Session to regenerate

**Returns:** `0` on success, `-1` on failure

**Use case:** Prevent session fixation attacks after authentication.

**Notes:**
- Thread-safe.

**Example:**
```c
void handle_login(ecewo_request_t *req, ecewo_response_t *res) {
    // ... authenticate user ...

    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (sess) {
        // Regenerate the ID after login for security
        if (ecewo_session_regenerate(sess) != 0) {
            ecewo_send_text(res, 500, "Session regeneration failed");
            return;
        }
        ecewo_session_send(res, sess, NULL);
    }

    ecewo_send_text(res, 200, "Logged in successfully");
}
```

---

### `ecewo_session_set()`

Stores a key-value pair in the session.

```c
int ecewo_session_set(ecewo_session_t *sess, const char *key, const char *value)
```

**Parameters:**
- `sess` - Target session
- `key` - Key name (automatically URL-encoded)
- `value` - Value to store (automatically URL-encoded)

**Returns:** `0` on success, `-1` on failure

**Notes:**
- Overwrites if the key already exists.
- Enforces a 4KB total data limit per session (measured on encoded data).
- Thread-safe.

**Example:**
```c
ecewo_session_set(sess, "user_id", "12345");
ecewo_session_set(sess, "username", "john_doe");

// Special characters are handled automatically
ecewo_session_set(sess, "note", "Hello, World! 你好");
```

---

### `ecewo_session_get()`

Retrieves a value from the session.

```c
char *ecewo_session_get(ecewo_session_t *sess, const char *key, ecewo_arena_t *arena)
```

**Parameters:**
- `sess` - Source session
- `key` - Key to retrieve
- `arena` - Arena allocator for the result, or `NULL` for `malloc`

**Returns:** Decoded value or `NULL` if not found

**Memory management:**
- If `arena` is provided (e.g. `ecewo_req_arena(req)`): returns arena-allocated string, freed automatically when the response is sent.
- If `arena` is `NULL`: returns a `malloc`'d string the **caller must free**.

**Notes:**
- Thread-safe.

**Example:**
```c
void handle_profile(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_text(res, 401, "Not logged in");
        return;
    }

    // Arena-allocated - freed automatically with the request
    char *username = ecewo_session_get(sess, "username", ecewo_req_arena(req));
    if (username) {
        char *response = ecewo_sprintf(ecewo_req_arena(req), "Welcome, %s!", username);
        ecewo_send_text(res, 200, response);
    } else {
        ecewo_send_text(res, 404, "No username");
    }
}
```

---

### `ecewo_session_remove()`

Removes a key-value pair from the session.

```c
int ecewo_session_remove(ecewo_session_t *sess, const char *key)
```

**Parameters:**
- `sess` - Target session
- `key` - Key to remove

**Returns:** `0` on success, `-1` on failure

**Notes:**
- Thread-safe.

**Example:**
```c
void handle_remove_cart_item(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_text(res, 401, "Unauthorized");
        return;
    }

    const char *item_id = ecewo_query(req, "item");
    ecewo_session_remove(sess, item_id);

    ecewo_send_text(res, 200, "Item removed from cart");
}
```

---

### `ecewo_session_from_request()`

Extracts the session from the request's `session` cookie.

```c
ecewo_session_t *ecewo_session_from_request(const ecewo_request_t *req)
```

**Parameters:**
- `req` - HTTP request object

**Returns:** Session handle or `NULL` if no valid session cookie

**Example:**
```c
void handle_protected_route(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_text(res, 401, "Authentication required");
        return;
    }

    char *role = ecewo_session_get(sess, "role", ecewo_req_arena(req));
    if (role && strcmp(role, "admin") == 0) {
        ecewo_send_text(res, 200, "Admin access granted");
    } else {
        ecewo_send_text(res, 403, "Admin access required");
    }
}
```

---

### `ecewo_session_send()`

Sends the session cookie to the client.

```c
void ecewo_session_send(ecewo_response_t *res, ecewo_session_t *sess, const ecewo_cookie_options_t *options)
```

**Parameters:**
- `res` - HTTP response object
- `sess` - Session to send
- `options` - Cookie options builder (or `NULL` for secure defaults)

**Default cookie settings (when `options` is `NULL`):**
- `Max-Age` - Calculated from the session expiry
- `Path` - `/`
- `SameSite` - `Lax`
- `HttpOnly` - on
- `Secure` - off (enable for production HTTPS)

**Example:**
```c
void handle_login(ecewo_request_t *req, ecewo_response_t *res) {
    // ... authenticate user ...

    ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), 7200); // 2 hours
    ecewo_session_set(sess, "user_id", "12345");
    ecewo_session_set(sess, "username", "john_doe");

    ecewo_cookie_options_t *opts = ecewo_cookie_options_new();
    ecewo_cookie_options_set_path(opts, "/");
    ecewo_cookie_options_set_same_site(opts, ECEWO_COOKIE_SAMESITE_STRICT); // CSRF protection
    ecewo_cookie_options_set_http_only(opts, 1);                            // XSS protection
    ecewo_cookie_options_set_secure(opts, 1);                               // HTTPS only

    ecewo_session_send(res, sess, opts);
    ecewo_cookie_options_free(opts);

    ecewo_send_text(res, 200, "Login successful");
}
```

---

### `ecewo_session_destroy()`

Destroys the session on both server and client.

```c
void ecewo_session_destroy(ecewo_response_t *res, ecewo_session_t *sess, const ecewo_cookie_options_t *options)
```

**Parameters:**
- `res` - HTTP response object
- `sess` - Session to destroy
- `options` - Cookie options builder (or `NULL` for defaults)

**Actions:**
1. Sends the session cookie to the client — with `Max-Age=0` when `options` is `NULL` (so the browser deletes it immediately), or with the caller-supplied options otherwise
2. Frees the session data from server memory

> [!NOTE]
> When passing custom `options`, you are responsible for including `Max-Age=0` (or an equivalent `Expires` in the past) to ensure the browser removes the cookie.

**Example:**
```c
void handle_logout(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (sess) {
        ecewo_session_destroy(res, sess, NULL);
    }

    ecewo_send_text(res, 200, "Logged out successfully");
}
```

---

### `ecewo_session_free()`

Frees a session from server memory only (no cookie sent).

```c
void ecewo_session_free(ecewo_session_t *sess)
```

**Parameters:**
- `sess` - Session to free

**Use case:** Backend session cleanup (e.g. after database persistence).

**Notes:**
- Thread-safe.

**Example:**
```c
void save_session_to_database(ecewo_session_t *sess) {
    char *user_id = ecewo_session_get(sess, "user_id", NULL);
    // ... save to database ...
    free(user_id);

    // Free from memory after persisting
    ecewo_session_free(sess);
}
```

---

## Examples

### Complete Authentication Flow
```c
#include "ecewo.h"
#include "ecewo-session.h"
#include "ecewo-cookie.h"
#include <string.h>

void handle_login(ecewo_request_t *req, ecewo_response_t *res) {
    const char *username = ecewo_query(req, "username");
    const char *password = ecewo_query(req, "password");

    // Validate credentials (example only)
    if (!username || !password ||
        strcmp(username, "admin") != 0 ||
        strcmp(password, "secret") != 0) {
        ecewo_send_text(res, 401, "Invalid credentials");
        return;
    }

    ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), 3600); // 1 hour
    if (!sess) {
        ecewo_send_text(res, 500, "Session creation failed");
        return;
    }

    ecewo_session_set(sess, "user_id", "1");
    ecewo_session_set(sess, "username", username);
    ecewo_session_set(sess, "role", "admin");

    ecewo_cookie_options_t *opts = ecewo_cookie_options_new();
    ecewo_cookie_options_set_path(opts, "/");
    ecewo_cookie_options_set_same_site(opts, ECEWO_COOKIE_SAMESITE_STRICT);
    ecewo_cookie_options_set_http_only(opts, 1);
    ecewo_cookie_options_set_secure(opts, 1); // HTTPS in production

    ecewo_session_send(res, sess, opts);
    ecewo_cookie_options_free(opts);

    ecewo_send_text(res, 200, "Login successful");
}

void handle_dashboard(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_text(res, 401, "Please log in");
        return;
    }

    char *username = ecewo_session_get(sess, "username", ecewo_req_arena(req));
    char *response = ecewo_sprintf(ecewo_req_arena(req),
        "<h1>Welcome, %s!</h1>", username ? username : "guest");

    ecewo_send_html(res, 200, response);
}

void handle_logout(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (sess) {
        ecewo_session_destroy(res, sess, NULL);
    }

    ecewo_send_text(res, 200, "Logged out");
}

int main(void) {
    ecewo_app_t *app = ecewo_create();

    if (ecewo_session_init(app) != 0) {
        fprintf(stderr, "Session init failed\n");
        return 1;
    }

    ECEWO_GET(app, "/login", handle_login);
    ECEWO_GET(app, "/dashboard", handle_dashboard);
    ECEWO_GET(app, "/logout", handle_logout);

    ecewo_listen(app, 3000);
    return 0;
}
```

### Shopping Cart with Sessions
```c
void handle_add_to_cart(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        // Create an anonymous cart session
        sess = ecewo_session_create(ecewo_req_app(req), 86400); // 24 hours
        if (!sess) {
            ecewo_send_text(res, 500, "Failed to create cart");
            return;
        }
    }

    const char *item_id = ecewo_query(req, "item");
    const char *quantity = ecewo_query(req, "qty");

    if (!item_id || !quantity) {
        ecewo_send_text(res, 400, "Missing item or quantity");
        return;
    }

    char key[64];
    snprintf(key, sizeof(key), "cart_%s", item_id);
    ecewo_session_set(sess, key, quantity);

    ecewo_session_send(res, sess, NULL);
    ecewo_send_text(res, 200, "Item added to cart");
}

void handle_view_cart(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_json(res, 200, "{\"items\":[]}");
        return;
    }

    ecewo_arena_t *arena = ecewo_req_arena(req);
    char *item1_qty = ecewo_session_get(sess, "cart_101", arena);
    char *item2_qty = ecewo_session_get(sess, "cart_205", arena);

    char *response = ecewo_sprintf(arena,
        "{\"items\":[{\"id\":101,\"qty\":%s},{\"id\":205,\"qty\":%s}]}",
        item1_qty ? item1_qty : "0",
        item2_qty ? item2_qty : "0");

    ecewo_send_json(res, 200, response);
}
```

### Session Regeneration for Security
```c
void handle_privilege_escalation(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_text(res, 401, "Not logged in");
        return;
    }

    // Before granting admin privileges, regenerate the session ID.
    // This prevents session fixation attacks.
    if (ecewo_session_regenerate(sess) != 0) {
        ecewo_send_text(res, 500, "Security update failed");
        return;
    }

    ecewo_session_set(sess, "role", "admin");
    ecewo_session_send(res, sess, NULL);

    ecewo_send_text(res, 200, "Admin privileges granted");
}
```

### Multi-Factor Authentication with Sessions
```c
void handle_mfa_initiate(ecewo_request_t *req, ecewo_response_t *res) {
    const char *username = ecewo_query(req, "username");
    // ... verify credentials ...

    // Create a temporary session for the MFA flow
    ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), 300); // 5 minutes
    ecewo_session_set(sess, "mfa_pending", "true");
    ecewo_session_set(sess, "username", username);

    // Send the MFA code via email/SMS ...

    ecewo_session_send(res, sess, NULL);
    ecewo_send_text(res, 200, "MFA code sent");
}

void handle_mfa_verify(ecewo_request_t *req, ecewo_response_t *res) {
    ecewo_session_t *sess = ecewo_session_from_request(req);
    if (!sess) {
        ecewo_send_text(res, 401, "Session expired");
        return;
    }

    char *pending = ecewo_session_get(sess, "mfa_pending", ecewo_req_arena(req));
    if (!pending || strcmp(pending, "true") != 0) {
        ecewo_send_text(res, 400, "MFA not initiated");
        return;
    }

    // ... verify the MFA code from ecewo_query(req, "code") ...

    // Upgrade to a full session
    ecewo_session_t *new_sess = ecewo_session_create(ecewo_req_app(req), 3600);
    char *username = ecewo_session_get(sess, "username", NULL);
    if (username) {
        ecewo_session_set(new_sess, "username", username);
        free(username);
    }
    ecewo_session_set(new_sess, "authenticated", "true");

    // Destroy the temporary session
    ecewo_session_free(sess);

    ecewo_session_send(res, new_sess, NULL);
    ecewo_send_text(res, 200, "MFA verified");
}
```
