#ifndef ECEWO_SESSION_TESTS
#define ECEWO_SESSION_TESTS

#include "ecewo.h"

// Route setup (registers handlers and initializes the session store).
void setup_all_routes(ecewo_app_t *app);

// HTTP handlers — all session operations run here, on the event-loop thread.
void handler_session_create(ecewo_request_t *req, ecewo_response_t *res);
void handler_session_get(ecewo_request_t *req, ecewo_response_t *res);
void handler_session_destroy(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_set_get(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_overwrite(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_remove(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_find(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_regenerate(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_utf8(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_create_ttl(ecewo_request_t *req, ecewo_response_t *res);
void handler_test_find_id(ecewo_request_t *req, ecewo_response_t *res);

// Tests (driven over HTTP via the mock server).
int test_session_create(void);
int test_session_no_session(void);
int test_session_roundtrip(void);
int test_session_value_set_get(void);
int test_session_value_overwrite(void);
int test_session_value_remove(void);
int test_session_find(void);
int test_session_utf8_values(void);
int test_session_regenerate(void);
int test_session_expired(void);

#endif
