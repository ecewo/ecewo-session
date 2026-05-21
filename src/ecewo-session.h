// Copyright 2025-2026 Savas Sahin <savashn@proton.me>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef ECEWO_SESSION_H
#define ECEWO_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ecewo-session-export.h"

typedef struct ecewo_app_s ecewo_app_t;
typedef struct ecewo_request_s ecewo_request_t;
typedef struct ecewo_response_s ecewo_response_t;
typedef struct ecewo_arena_s ecewo_arena_t;
typedef struct ecewo_cookie_options_s ecewo_cookie_options_t;
typedef struct ecewo_session_s ecewo_session_t;

/**
 * Initialize the session manager for an application.
 * Allocates the session store from the app arena, registers it as app data,
 * starts a periodic cleanup timer, and schedules teardown via ecewo_atexit().
 * Call once during app setup, before ecewo_listen() / ecewo_run(), and on the
 * event-loop thread (i.e. from your route-registration step).
 * Idempotent: a second call on the same app is a no-op.
 * Returns 0 on success, -1 on failure.
 */
ECEWO_SESSION_EXPORT int ecewo_session_init(ecewo_app_t *app);

/**
 * Create a new session with specified max_age in seconds.
 * Uses cryptographically secure random for session IDs.
 * `app` selects the store created by ecewo_session_init(app).
 * Returns an opaque session handle or NULL on failure.
 */
ECEWO_SESSION_EXPORT ecewo_session_t *ecewo_session_create(ecewo_app_t *app, int max_age);

/**
 * Find a session by ID within the given app's store.
 * Returns an opaque session handle or NULL if not found/expired.
 */
ECEWO_SESSION_EXPORT ecewo_session_t *ecewo_session_find(ecewo_app_t *app, const char *id);

/**
 * Get the session ID string.
 * Returns the session ID or NULL if session is invalid.
 */
ECEWO_SESSION_EXPORT const char *ecewo_session_id(const ecewo_session_t *sess);

/**
 * Regenerate session ID (for security after privilege escalation).
 * Keeps session data and expiry time.
 * Returns 0 on success, -1 on failure.
 */
ECEWO_SESSION_EXPORT int ecewo_session_regenerate(ecewo_session_t *sess);

/**
 * Set a key-value pair in session data.
 * Values are URL-encoded and stored efficiently.
 * Returns 0 on success, -1 on failure.
 */
ECEWO_SESSION_EXPORT int ecewo_session_set(ecewo_session_t *sess, const char *key, const char *value);

/**
 * Get a value from session data by key.
 * Returns the value allocated in the provided arena, or NULL if not found.
 * If arena is NULL, returns a malloc'd string (caller must free).
 */
ECEWO_SESSION_EXPORT char *ecewo_session_get(ecewo_session_t *sess, const char *key, ecewo_arena_t *arena);

/**
 * Remove a key-value pair from session data.
 * Returns 0 on success, -1 on failure.
 */
ECEWO_SESSION_EXPORT int ecewo_session_remove(ecewo_session_t *sess, const char *key);

/**
 * Free a session and clear its data.
 * Thread-safe.
 */
ECEWO_SESSION_EXPORT void ecewo_session_free(ecewo_session_t *sess);

/**
 * Get session from request cookie.
 * Extracts session ID from the "session" cookie and finds the session.
 * Returns session handle or NULL if no session cookie or session not found.
 */
ECEWO_SESSION_EXPORT ecewo_session_t *ecewo_session_from_request(const ecewo_request_t *req);

/**
 * Send session cookie to client.
 * Sets the "session" cookie with appropriate security flags.
 * Uses options if provided, otherwise uses secure defaults.
 * The options pointer must remain valid until this function returns.
 */
ECEWO_SESSION_EXPORT void ecewo_session_send(ecewo_response_t *res, ecewo_session_t *sess, const ecewo_cookie_options_t *options);

/**
 * Destroy session and send expiry cookie to client.
 * Immediately expires the session cookie in browser.
 * The options pointer must remain valid until this function returns.
 */
ECEWO_SESSION_EXPORT void ecewo_session_destroy(ecewo_response_t *res, ecewo_session_t *sess, const ecewo_cookie_options_t *options);

#ifdef __cplusplus
}
#endif

#endif
