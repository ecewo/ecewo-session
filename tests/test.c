#include "ecewo.h"
#include "ecewo-mock.h"
#include "ecewo-session.h"
#include "tester.h"
#include "tests.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Fail a handler-based test by sending a 500 with a "FAIL: ..." body.
#define H_CHECK(cond, res, msg)                 \
  do {                                          \
    if (!(cond)) {                              \
      ecewo_send_text(res, 500, "FAIL: " msg);  \
      return;                                   \
    }                                           \
  } while (0)

// ---------------------------------------------------------------------------
// HANDLERS (run on the event-loop thread, where the app/arenas are valid)
// ---------------------------------------------------------------------------

void handler_session_create(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), 3600);
  if (!sess) {
    ecewo_send_text(res, 500, "Session creation failed");
    return;
  }

  ecewo_session_set(sess, "user_id", "12345");
  ecewo_session_set(sess, "username", "john");

  ecewo_session_send(res, sess, NULL);
  ecewo_send_text(res, 200, "Session created");
}

void handler_session_get(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_session_t *sess = ecewo_session_from_request(req);
  if (!sess) {
    ecewo_send_text(res, 401, "No session");
    return;
  }

  char *user_id = ecewo_session_get(sess, "user_id", ecewo_req_arena(req));
  if (user_id)
    ecewo_send_text(res, 200, user_id);
  else
    ecewo_send_text(res, 404, "user_id not found");
}

void handler_session_destroy(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_session_t *sess = ecewo_session_from_request(req);
  if (sess)
    ecewo_session_destroy(res, sess, NULL);
  ecewo_send_text(res, 200, "Session destroyed");
}

void handler_test_set_get(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_app_t *app = ecewo_req_app(req);
  ecewo_arena_t *arena = ecewo_req_arena(req);

  ecewo_session_t *sess = ecewo_session_create(app, 3600);
  H_CHECK(sess, res, "create");
  H_CHECK(ecewo_session_set(sess, "key1", "value1") == 0, res, "set key1");
  H_CHECK(ecewo_session_set(sess, "key2", "value2") == 0, res, "set key2");

  char *v1 = ecewo_session_get(sess, "key1", arena);
  char *v2 = ecewo_session_get(sess, "key2", arena);
  char *v3 = ecewo_session_get(sess, "nonexistent", arena);

  H_CHECK(v1 && strcmp(v1, "value1") == 0, res, "get key1");
  H_CHECK(v2 && strcmp(v2, "value2") == 0, res, "get key2");
  H_CHECK(v3 == NULL, res, "get nonexistent");

  ecewo_session_free(sess);
  ecewo_send_text(res, 200, "PASS");
}

void handler_test_overwrite(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_app_t *app = ecewo_req_app(req);
  ecewo_arena_t *arena = ecewo_req_arena(req);

  ecewo_session_t *sess = ecewo_session_create(app, 3600);
  H_CHECK(sess, res, "create");
  H_CHECK(ecewo_session_set(sess, "key", "first") == 0, res, "set first");
  H_CHECK(ecewo_session_set(sess, "key", "second") == 0, res, "set second");

  char *val = ecewo_session_get(sess, "key", arena);
  H_CHECK(val && strcmp(val, "second") == 0, res, "overwrite");

  ecewo_session_free(sess);
  ecewo_send_text(res, 200, "PASS");
}

void handler_test_remove(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_app_t *app = ecewo_req_app(req);
  ecewo_arena_t *arena = ecewo_req_arena(req);

  ecewo_session_t *sess = ecewo_session_create(app, 3600);
  H_CHECK(sess, res, "create");
  H_CHECK(ecewo_session_set(sess, "to_remove", "value") == 0, res, "set");

  char *before = ecewo_session_get(sess, "to_remove", arena);
  H_CHECK(before && strcmp(before, "value") == 0, res, "get before");

  H_CHECK(ecewo_session_remove(sess, "to_remove") == 0, res, "remove");

  char *after = ecewo_session_get(sess, "to_remove", arena);
  H_CHECK(after == NULL, res, "get after");

  ecewo_session_free(sess);
  ecewo_send_text(res, 200, "PASS");
}

void handler_test_find(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_app_t *app = ecewo_req_app(req);

  ecewo_session_t *sess = ecewo_session_create(app, 3600);
  H_CHECK(sess, res, "create");

  const char *id = ecewo_session_id(sess);
  H_CHECK(id, res, "id");

  ecewo_session_t *found = ecewo_session_find(app, id);
  H_CHECK(found, res, "find");
  H_CHECK(ecewo_session_id(found) && strcmp(ecewo_session_id(found), id) == 0, res, "id match");

  ecewo_session_t *not_found = ecewo_session_find(app, "nonexistent_session_id_12345");
  H_CHECK(not_found == NULL, res, "find nonexistent");

  ecewo_session_free(sess);
  ecewo_send_text(res, 200, "PASS");
}

void handler_test_regenerate(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_app_t *app = ecewo_req_app(req);
  ecewo_arena_t *arena = ecewo_req_arena(req);

  ecewo_session_t *sess = ecewo_session_create(app, 3600);
  H_CHECK(sess, res, "create");
  H_CHECK(ecewo_session_set(sess, "key", "value") == 0, res, "set");

  const char *old_id = ecewo_session_id(sess);
  H_CHECK(old_id, res, "old id");

  char saved_id[64];
  snprintf(saved_id, sizeof(saved_id), "%s", old_id);

  H_CHECK(ecewo_session_regenerate(sess) == 0, res, "regenerate");

  const char *new_id = ecewo_session_id(sess);
  H_CHECK(new_id && strcmp(saved_id, new_id) != 0, res, "new id differs");
  H_CHECK(ecewo_session_find(app, new_id) != NULL, res, "find new id");
  H_CHECK(ecewo_session_find(app, saved_id) == NULL, res, "old id gone");

  char *val = ecewo_session_get(sess, "key", arena);
  H_CHECK(val && strcmp(val, "value") == 0, res, "data preserved");

  ecewo_session_free(sess);
  ecewo_send_text(res, 200, "PASS");
}

void handler_test_utf8(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_app_t *app = ecewo_req_app(req);
  ecewo_arena_t *arena = ecewo_req_arena(req);

  ecewo_session_t *sess = ecewo_session_create(app, 3600);
  H_CHECK(sess, res, "create");
  H_CHECK(ecewo_session_set(sess, "turkish", "üç dört beş") == 0, res, "set turkish");
  H_CHECK(ecewo_session_set(sess, "emoji", "test 🎉") == 0, res, "set emoji");

  char *turkish = ecewo_session_get(sess, "turkish", arena);
  H_CHECK(turkish && strcmp(turkish, "üç dört beş") == 0, res, "get turkish");

  char *emoji = ecewo_session_get(sess, "emoji", arena);
  H_CHECK(emoji && strcmp(emoji, "test 🎉") == 0, res, "get emoji");

  ecewo_session_free(sess);
  ecewo_send_text(res, 200, "PASS");
}

void handler_test_create_ttl(ecewo_request_t *req, ecewo_response_t *res) {
  const char *ttl_str = ecewo_query(req, "ttl");
  int ttl = ttl_str ? atoi(ttl_str) : 3600;

  ecewo_session_t *sess = ecewo_session_create(ecewo_req_app(req), ttl);
  if (!sess) {
    ecewo_send_text(res, 500, "FAIL: create");
    return;
  }

  const char *id = ecewo_session_id(sess);
  ecewo_send_text(res, 200, id ? id : "");
}

void handler_test_find_id(ecewo_request_t *req, ecewo_response_t *res) {
  const char *id = ecewo_query(req, "id");
  if (!id) {
    ecewo_send_text(res, 400, "FAIL: no id");
    return;
  }

  ecewo_session_t *found = ecewo_session_find(ecewo_req_app(req), id);
  ecewo_send_text(res, 200, found ? "FOUND" : "NOTFOUND");
}

// ---------------------------------------------------------------------------
// TESTS (run on the main thread, drive the API over HTTP)
// ---------------------------------------------------------------------------

static MockResponse get(const char *path) {
  MockParams params = {
    .method = MOCK_GET,
    .path = path,
    .body = NULL,
    .headers = NULL,
    .header_count = 0
  };
  return request(&params);
}

// Copy the "name=value" part of a Set-Cookie header (up to the first ';').
static void extract_cookie_pair(const char *set_cookie, char *out, size_t out_sz) {
  size_t i = 0;
  while (set_cookie[i] && set_cookie[i] != ';' && i + 1 < out_sz) {
    out[i] = set_cookie[i];
    i++;
  }
  out[i] = '\0';
}

int test_session_create(void) {
  MockResponse res = get("/session/create");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("Session created", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_no_session(void) {
  MockResponse res = get("/session/get");
  ASSERT_EQ(401, res.status_code);
  ASSERT_EQ_STR("No session", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_roundtrip(void) {
  MockResponse created = get("/session/create");
  ASSERT_EQ(200, created.status_code);

  const char *set_cookie = mock_get_header(&created, "Set-Cookie");
  ASSERT_NOT_NULL(set_cookie);

  char cookie[128];
  extract_cookie_pair(set_cookie, cookie, sizeof(cookie));
  free_request(&created);

  MockHeaders headers[] = {
    { "Cookie", cookie }
  };
  MockParams params = {
    .method = MOCK_GET,
    .path = "/session/get",
    .body = NULL,
    .headers = headers,
    .header_count = 1
  };
  MockResponse got = request(&params);

  ASSERT_EQ(200, got.status_code);
  ASSERT_EQ_STR("12345", got.body);
  free_request(&got);
  RETURN_OK();
}

int test_session_value_set_get(void) {
  MockResponse res = get("/test/set-get");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("PASS", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_value_overwrite(void) {
  MockResponse res = get("/test/overwrite");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("PASS", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_value_remove(void) {
  MockResponse res = get("/test/remove");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("PASS", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_find(void) {
  MockResponse res = get("/test/find");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("PASS", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_regenerate(void) {
  MockResponse res = get("/test/regenerate");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("PASS", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_utf8_values(void) {
  MockResponse res = get("/test/utf8");
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("PASS", res.body);
  free_request(&res);
  RETURN_OK();
}

int test_session_expired(void) {
  MockResponse created = get("/test/create-ttl?ttl=1");
  ASSERT_EQ(200, created.status_code);
  ASSERT_NOT_NULL(created.body);

  char id[64];
  snprintf(id, sizeof(id), "%s", created.body);
  free_request(&created);

  sleep(2);

  char path[128];
  snprintf(path, sizeof(path), "/test/find-id?id=%s", id);
  MockResponse found = get(path);

  ASSERT_EQ(200, found.status_code);
  ASSERT_EQ_STR("NOTFOUND", found.body);
  free_request(&found);
  RETURN_OK();
}
