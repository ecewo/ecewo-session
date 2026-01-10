#ifndef ECEWO_SESSION_H
#define ECEWO_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <stdint.h>
#include "ecewo.h"
#include "ecewo-cookie.h"

#define SESSION_ID_LEN 32
#define MAX_SESSIONS_DEFAULT 10

// Session structure
// Note: Session pointers remain valid until session_free() is called or session expires.
typedef struct
{
  char id[SESSION_ID_LEN + 1];
  char *data;
  time_t expires;
} Session;

// Initialize the session manager
// Must be called before any other session functions
// Starts a periodic cleanup timer
// Returns: 1 on success, 0 on failure
int session_init(void);

// Cleanup all sessions and free resources
// Stops cleanup timer and destroys mutex
// Should be called at application shutdown
void session_cleanup(void);

// Create a new session with specified max_age in seconds
// Uses cryptographically secure random for session IDs
// Returns: Session pointer or NULL on failure
// Note: Returned pointer is valid until session_free() or expiry
Session *session_create(int max_age);

// Find a session by ID
// Automatically renews expiry time on access (sliding window)
// Cleans up expired sessions periodically
// Returns: Session pointer or NULL if not found/expired
Session *session_find(const char *id);

// Regenerate session ID (for security after privilege escalation)
// Keeps session data and expiry time
// Returns: 1 on success, 0 on failure
// Use case: After login to prevent session fixation attacks
int session_regenerate(Session *sess);

// Set a key-value pair in session data
// Values are URL-encoded and stored efficiently
// Returns: 1 on success, 0 on failure (e.g., size limit exceeded)
int session_value_set(Session *sess, const char *key, const char *value);

// Get a value from session data by key
//
// Memory management:
// - If arena is provided: returns arena-allocated string (auto-freed with arena)
// - If arena is NULL: returns malloc'd string (caller must free())
//
// Returns: String value or NULL if not found
char *session_value_get(Session *sess, const char *key, Arena *arena);

// Remove a key-value pair from session data
// Returns: 1 on success, 0 on failure
int session_value_remove(Session *sess, const char *key);

// Free a session and clear its data
// Thread-safe - can be called from any thread
void session_free(Session *sess);

// Get session from request cookie
// Convenience function that extracts session ID from cookie and finds session
// Returns: Session pointer or NULL if no session cookie or session not found
Session *session_get(Req *req);

// Send session cookie to client
// Sets the "session" cookie with appropriate security flags
// Uses options if provided, otherwise uses secure defaults
void session_send(Res *res, Session *sess, Cookie *options);

// Destroy session and send expiry cookie to client
// Immediately expires the session cookie in browser
void session_destroy(Res *res, Session *sess, Cookie *options);

// Debug function: print all active sessions to stdout
// Shows session IDs, expiry times, and stored data
void session_print_all(void);

#ifdef __cplusplus
}
#endif

#endif
