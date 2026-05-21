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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "ecewo-session.h"
#include "ecewo.h"
#include "ecewo-cookie.h"
#include "uv.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

#define SESSION_ID_LEN 32
#define MAX_SESSIONS_DEFAULT 10

#define KV_DELIMITER '\x1F'
#define PAIR_DELIMITER '\x1E'
#define MAX_SESSION_DATA_SIZE 4096
#define SESSION_CLEANUP_INTERVAL_MS 60000

typedef struct session_store_s session_store_t;

struct ecewo_session_s {
  char id[SESSION_ID_LEN + 1];
  char *data; // malloc'd, NULL until the first key is set
  time_t expires;
  session_store_t *store; // back-pointer so handle-only ops can find the store
};

struct session_store_s {
  ecewo_session_t *sessions; // malloc/realloc array of slots
  int max_sessions;
  uv_mutex_t mutex;
  ecewo_timer_t *cleanup_timer;
};

// The address of this file-static is used as the unique app_data key, per the
// ecewo plugin convention. Its value is never read or written.
static int session_store_key;

// ---------------------------------------------------------------------------
// URL ENCODING (arena-backed scratch / result buffers)
// ---------------------------------------------------------------------------

static int is_url_safe(unsigned char c) {
  return (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~');
}

static char *url_encode(ecewo_arena_t *arena, const char *str) {
  if (!str)
    return NULL;

  static const char hex[] = "0123456789ABCDEF";
  size_t len = strlen(str);
  char *encoded = ecewo_alloc(arena, len * 3 + 1);
  if (!encoded)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if (is_url_safe(c)) {
      encoded[j++] = (char)c;
    } else {
      encoded[j++] = '%';
      encoded[j++] = hex[c >> 4];
      encoded[j++] = hex[c & 0xF];
    }
  }
  encoded[j] = '\0';
  return encoded;
}

static int hex_to_int(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// Decode `src_len` bytes. When `arena` is non-NULL the result lives in that
// arena (e.g. the request arena); when NULL the result is malloc'd and the
// caller must free it.
static char *url_decode(ecewo_arena_t *arena, const char *src, size_t src_len) {
  char *decoded = arena ? ecewo_alloc(arena, src_len + 1) : malloc(src_len + 1);
  if (!decoded)
    return NULL;

  size_t i = 0, j = 0;
  while (i < src_len) {
    if (src[i] == '%' && i + 2 < src_len) {
      int high = hex_to_int(src[i + 1]);
      int low = hex_to_int(src[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded[j++] = (char)((high << 4) | low);
        i += 3;
        continue;
      }
    }
    decoded[j++] = src[i++];
  }
  decoded[j] = '\0';
  return decoded;
}

// ---------------------------------------------------------------------------
// SESSION ID GENERATION
// ---------------------------------------------------------------------------

static int get_random_bytes(unsigned char *buffer, size_t length) {
#ifdef _WIN32
  HCRYPTPROV hCryptProv;
  int result = 0;

  if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
    if (CryptGenRandom(hCryptProv, (DWORD)length, buffer))
      result = 1;
    CryptReleaseContext(hCryptProv, 0);
  }
  return result;
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return -1;

  size_t bytes_read = 0;
  while (bytes_read < length) {
    ssize_t result = read(fd, buffer + bytes_read, length - bytes_read);
    if (result < 0) {
      if (errno == EINTR)
        continue;
      close(fd);
      return -1;
    }
    bytes_read += (size_t)result;
  }

  close(fd);
  return 0;
#endif
}

static int generate_session_id(char *buffer) {
  unsigned char entropy[SESSION_ID_LEN];

#ifdef _WIN32
  if (!get_random_bytes(entropy, SESSION_ID_LEN)) {
#else
  if (get_random_bytes(entropy, SESSION_ID_LEN) != 0) {
#endif
    fprintf(stderr, "CRITICAL: Cryptographically secure random generation failed\n");
    return -1;
  }

  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  const size_t charset_len = sizeof(charset) - 1;

  for (size_t i = 0; i < SESSION_ID_LEN; i++) {
    buffer[i] = charset[entropy[i] % charset_len];
  }

  memset(entropy, 0, SESSION_ID_LEN);
  buffer[SESSION_ID_LEN] = '\0';
  return 0;
}

// ---------------------------------------------------------------------------
// STORE HELPERS
// ---------------------------------------------------------------------------

static session_store_t *store_from_app(const ecewo_app_t *app) {
  return app ? (session_store_t *)ecewo_get_app_data(app, &session_store_key) : NULL;
}

// Validate that `sess` points at a live slot in the store. Caller must hold the
// store mutex.
static int session_is_live_locked(const session_store_t *store, const ecewo_session_t *sess) {
  if (sess < store->sessions || sess >= store->sessions + store->max_sessions)
    return 0;
  return sess->id[0] != '\0';
}

static void cleanup_expired_sessions_locked(session_store_t *store) {
  time_t now = time(NULL);
  for (int i = 0; i < store->max_sessions; i++) {
    if (store->sessions[i].id[0] != '\0' && store->sessions[i].expires < now) {
      free(store->sessions[i].data);
      store->sessions[i].data = NULL;
      memset(store->sessions[i].id, 0, sizeof(store->sessions[i].id));
      store->sessions[i].expires = 0;
    }
  }
}

static void cleanup_timer_cb(void *user_data) {
  session_store_t *store = (session_store_t *)user_data;
  if (!store)
    return;

  uv_mutex_lock(&store->mutex);
  cleanup_expired_sessions_locked(store);
  uv_mutex_unlock(&store->mutex);
}

static int resize_sessions_locked(session_store_t *store, int new_capacity) {
  if (new_capacity <= store->max_sessions)
    return 0;

  ecewo_session_t *new_sessions =
      realloc(store->sessions, (size_t)new_capacity * sizeof(ecewo_session_t));
  if (!new_sessions)
    return -1;

  for (int i = store->max_sessions; i < new_capacity; i++) {
    memset(&new_sessions[i], 0, sizeof(ecewo_session_t));
  }

  store->sessions = new_sessions;
  store->max_sessions = new_capacity;

  // The array may have moved; refresh every slot's back-pointer.
  for (int i = 0; i < new_capacity; i++) {
    new_sessions[i].store = store;
  }
  return 0;
}

// Registered with ecewo_atexit(); runs on the loop thread during shutdown.
static void session_store_destroy(void *user_data) {
  session_store_t *store = (session_store_t *)user_data;
  if (!store)
    return;

  if (store->cleanup_timer) {
    ecewo_clear_timer(store->cleanup_timer);
    store->cleanup_timer = NULL;
  }

  uv_mutex_lock(&store->mutex);
  for (int i = 0; i < store->max_sessions; i++) {
    free(store->sessions[i].data);
    store->sessions[i].data = NULL;
  }
  free(store->sessions);
  store->sessions = NULL;
  store->max_sessions = 0;
  uv_mutex_unlock(&store->mutex);

  uv_mutex_destroy(&store->mutex);
  // The store struct itself is app-arena memory and is freed with the app.
}

static void remove_key_from_data(char *data, const char *encoded_key) {
  if (!data || !encoded_key)
    return;

  char *search_start = data;
  size_t key_len_target = strlen(encoded_key);

  while (search_start && *search_start) {
    char *key_end = strchr(search_start, KV_DELIMITER);
    if (!key_end)
      break;

    size_t key_len = (size_t)(key_end - search_start);

    if (key_len == key_len_target && strncmp(search_start, encoded_key, key_len) == 0) {
      char *value_start = key_end + 1;
      char *pair_end = strchr(value_start, PAIR_DELIMITER);

      if (pair_end) {
        char *after_pair = pair_end + 1;
        size_t remaining_len = strlen(after_pair);
        memmove(search_start, after_pair, remaining_len + 1);
      } else {
        *search_start = '\0';
      }
      break;
    }

    char *next_pair = strchr(search_start, PAIR_DELIMITER);
    search_start = next_pair ? next_pair + 1 : NULL;
  }
}

// ---------------------------------------------------------------------------
// PUBLIC API
// ---------------------------------------------------------------------------

int ecewo_session_init(ecewo_app_t *app) {
  if (!app)
    return -1;

  if (store_from_app(app))
    return 0; // already initialized for this app

  ecewo_arena_t *app_arena = ecewo_app_arena(app);
  if (!app_arena)
    return -1;

  session_store_t *store = ecewo_alloc(app_arena, sizeof(*store));
  if (!store)
    return -1;

  store->cleanup_timer = NULL;
  store->max_sessions = MAX_SESSIONS_DEFAULT;
  store->sessions = calloc(MAX_SESSIONS_DEFAULT, sizeof(ecewo_session_t));
  if (!store->sessions)
    return -1;

  for (int i = 0; i < store->max_sessions; i++) {
    store->sessions[i].store = store;
  }

  if (uv_mutex_init(&store->mutex) != 0) {
    free(store->sessions);
    return -1;
  }

  store->cleanup_timer = ecewo_interval(cleanup_timer_cb, SESSION_CLEANUP_INTERVAL_MS, store);

  ecewo_set_app_data(app, &session_store_key, store);
  ecewo_atexit(app, session_store_destroy, store);
  return 0;
}

ecewo_session_t *ecewo_session_create(ecewo_app_t *app, int max_age) {
  session_store_t *store = store_from_app(app);
  if (!store)
    return NULL;

  uv_mutex_lock(&store->mutex);

  cleanup_expired_sessions_locked(store);

  int slot = -1;
  for (int i = 0; i < store->max_sessions; i++) {
    if (store->sessions[i].id[0] == '\0') {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    int old_capacity = store->max_sessions;
    if (resize_sessions_locked(store, old_capacity * 2) != 0) {
      uv_mutex_unlock(&store->mutex);
      return NULL;
    }
    slot = old_capacity;
  }

  ecewo_session_t *sess = &store->sessions[slot];

  if (generate_session_id(sess->id) != 0) {
    uv_mutex_unlock(&store->mutex);
    return NULL;
  }

  sess->expires = time(NULL) + max_age;
  sess->store = store;

  // Data is allocated lazily on the first ecewo_session_set().
  free(sess->data);
  sess->data = NULL;

  uv_mutex_unlock(&store->mutex);
  return sess;
}

ecewo_session_t *ecewo_session_find(ecewo_app_t *app, const char *id) {
  if (!id)
    return NULL;

  session_store_t *store = store_from_app(app);
  if (!store)
    return NULL;

  uv_mutex_lock(&store->mutex);

  time_t now = time(NULL);
  ecewo_session_t *result = NULL;

  for (int i = 0; i < store->max_sessions; i++) {
    if (store->sessions[i].id[0] != '\0' && strcmp(store->sessions[i].id, id) == 0) {
      if (store->sessions[i].expires >= now)
        result = &store->sessions[i];
      break;
    }
  }

  uv_mutex_unlock(&store->mutex);
  return result;
}

const char *ecewo_session_id(const ecewo_session_t *sess) {
  if (!sess || sess->id[0] == '\0')
    return NULL;
  return sess->id;
}

int ecewo_session_regenerate(ecewo_session_t *sess) {
  if (!sess || !sess->store)
    return -1;

  session_store_t *store = sess->store;
  uv_mutex_lock(&store->mutex);

  if (!session_is_live_locked(store, sess)) {
    uv_mutex_unlock(&store->mutex);
    return -1;
  }

  if (generate_session_id(sess->id) != 0) {
    uv_mutex_unlock(&store->mutex);
    return -1;
  }

  uv_mutex_unlock(&store->mutex);
  return 0;
}

int ecewo_session_set(ecewo_session_t *sess, const char *key, const char *value) {
  if (!sess || !key || !value || !sess->store)
    return -1;

  session_store_t *store = sess->store;

  ecewo_arena_t *scratch = ecewo_arena_borrow();
  if (!scratch)
    return -1;

  char *encoded_key = url_encode(scratch, key);
  char *encoded_value = url_encode(scratch, value);
  if (!encoded_key || !encoded_value) {
    ecewo_arena_return(scratch);
    return -1;
  }

  uv_mutex_lock(&store->mutex);

  if (!session_is_live_locked(store, sess)) {
    uv_mutex_unlock(&store->mutex);
    ecewo_arena_return(scratch);
    return -1;
  }

  if (sess->data && sess->data[0] != '\0')
    remove_key_from_data(sess->data, encoded_key);

  size_t current_len = sess->data ? strlen(sess->data) : 0;
  size_t key_len = strlen(encoded_key);
  size_t value_len = strlen(encoded_value);

  if (current_len + key_len + value_len + 2 > MAX_SESSION_DATA_SIZE) {
    fprintf(stderr, "Session data size limit exceeded\n");
    uv_mutex_unlock(&store->mutex);
    ecewo_arena_return(scratch);
    return -1;
  }

  size_t new_len = current_len + key_len + value_len + 3; // KV + PAIR + NUL
  char *new_data = malloc(new_len);
  if (!new_data) {
    uv_mutex_unlock(&store->mutex);
    ecewo_arena_return(scratch);
    return -1;
  }

  size_t pos = 0;
  if (current_len > 0) {
    memcpy(new_data, sess->data, current_len);
    pos = current_len;
  }
  memcpy(new_data + pos, encoded_key, key_len);
  pos += key_len;
  new_data[pos++] = KV_DELIMITER;
  memcpy(new_data + pos, encoded_value, value_len);
  pos += value_len;
  new_data[pos++] = PAIR_DELIMITER;
  new_data[pos] = '\0';

  free(sess->data);
  sess->data = new_data;

  uv_mutex_unlock(&store->mutex);
  ecewo_arena_return(scratch);
  return 0;
}

char *ecewo_session_get(ecewo_session_t *sess, const char *key, ecewo_arena_t *arena) {
  if (!sess || !key || !sess->store)
    return NULL;

  session_store_t *store = sess->store;

  ecewo_arena_t *scratch = ecewo_arena_borrow();
  if (!scratch)
    return NULL;

  char *encoded_key = url_encode(scratch, key);
  if (!encoded_key) {
    ecewo_arena_return(scratch);
    return NULL;
  }
  size_t encoded_key_len = strlen(encoded_key);

  uv_mutex_lock(&store->mutex);

  if (!session_is_live_locked(store, sess) || !sess->data || sess->data[0] == '\0') {
    uv_mutex_unlock(&store->mutex);
    ecewo_arena_return(scratch);
    return NULL;
  }

  char *result = NULL;
  char *search_start = sess->data;

  while (search_start && *search_start) {
    char *key_end = strchr(search_start, KV_DELIMITER);
    if (!key_end)
      break;

    size_t key_len = (size_t)(key_end - search_start);

    if (key_len == encoded_key_len && strncmp(search_start, encoded_key, key_len) == 0) {
      char *value_start = key_end + 1;
      char *value_end = strchr(value_start, PAIR_DELIMITER);
      size_t value_len = value_end ? (size_t)(value_end - value_start) : strlen(value_start);
      result = url_decode(arena, value_start, value_len);
      break;
    }

    char *next_pair = strchr(search_start, PAIR_DELIMITER);
    search_start = next_pair ? next_pair + 1 : NULL;
  }

  uv_mutex_unlock(&store->mutex);
  ecewo_arena_return(scratch);
  return result;
}

int ecewo_session_remove(ecewo_session_t *sess, const char *key) {
  if (!sess || !key || !sess->store)
    return -1;

  session_store_t *store = sess->store;

  ecewo_arena_t *scratch = ecewo_arena_borrow();
  if (!scratch)
    return -1;

  char *encoded_key = url_encode(scratch, key);
  if (!encoded_key) {
    ecewo_arena_return(scratch);
    return -1;
  }

  uv_mutex_lock(&store->mutex);

  if (!session_is_live_locked(store, sess)) {
    uv_mutex_unlock(&store->mutex);
    ecewo_arena_return(scratch);
    return -1;
  }

  // Removing a key from an empty (never-written) session is a no-op success;
  // remove_key_from_data() tolerates a NULL data buffer.
  remove_key_from_data(sess->data, encoded_key);

  uv_mutex_unlock(&store->mutex);
  ecewo_arena_return(scratch);
  return 0;
}

void ecewo_session_free(ecewo_session_t *sess) {
  if (!sess || !sess->store)
    return;

  session_store_t *store = sess->store;
  uv_mutex_lock(&store->mutex);

  if (session_is_live_locked(store, sess)) {
    memset(sess->id, 0, sizeof(sess->id));
    sess->expires = 0;
    free(sess->data);
    sess->data = NULL;
  }

  uv_mutex_unlock(&store->mutex);
}

ecewo_session_t *ecewo_session_from_request(const ecewo_request_t *req) {
  if (!req)
    return NULL;

  const char *sid = ecewo_cookie_get(req, "session");
  if (!sid)
    return NULL;

  return ecewo_session_find(ecewo_req_app(req), sid);
}

void ecewo_session_send(ecewo_response_t *res, ecewo_session_t *sess, const ecewo_cookie_options_t *options) {
  if (!res || !sess || !sess->store) {
    fprintf(stderr, "Error on session sending\n");
    return;
  }

  session_store_t *store = sess->store;
  char id_copy[SESSION_ID_LEN + 1];
  int max_age;

  uv_mutex_lock(&store->mutex);
  if (!session_is_live_locked(store, sess)) {
    uv_mutex_unlock(&store->mutex);
    return;
  }
  max_age = (int)difftime(sess->expires, time(NULL));
  memcpy(id_copy, sess->id, sizeof(id_copy));
  uv_mutex_unlock(&store->mutex);

  if (max_age < 0)
    return;

  if (options) {
    ecewo_cookie_set(res, "session", id_copy, options);
    return;
  }

  ecewo_cookie_options_t *opts = ecewo_cookie_options_new();
  if (!opts)
    return;

  ecewo_cookie_options_set_max_age(opts, max_age);
  ecewo_cookie_options_set_path(opts, "/");
  ecewo_cookie_options_set_same_site(opts, ECEWO_COOKIE_SAMESITE_LAX);
  ecewo_cookie_options_set_http_only(opts, 1);
  ecewo_cookie_options_set_secure(opts, 0);

  ecewo_cookie_set(res, "session", id_copy, opts);
  ecewo_cookie_options_free(opts);
}

void ecewo_session_destroy(ecewo_response_t *res, ecewo_session_t *sess, const ecewo_cookie_options_t *options) {
  if (!res || !sess || !sess->store)
    return;

  session_store_t *store = sess->store;
  char id_copy[SESSION_ID_LEN + 1];

  uv_mutex_lock(&store->mutex);
  if (!session_is_live_locked(store, sess)) {
    uv_mutex_unlock(&store->mutex);
    return;
  }
  memcpy(id_copy, sess->id, sizeof(id_copy));
  uv_mutex_unlock(&store->mutex);

  if (options) {
    ecewo_cookie_set(res, "session", id_copy, options);
  } else {
    ecewo_cookie_options_t *opts = ecewo_cookie_options_new();
    if (!opts) {
      ecewo_session_free(sess);
      return;
    }

    ecewo_cookie_options_set_max_age(opts, 0);
    ecewo_cookie_options_set_path(opts, "/");
    ecewo_cookie_options_set_same_site(opts, ECEWO_COOKIE_SAMESITE_LAX);
    ecewo_cookie_options_set_http_only(opts, 1);
    ecewo_cookie_options_set_secure(opts, 0);

    ecewo_cookie_set(res, "session", id_copy, opts);
    ecewo_cookie_options_free(opts);
  }

  ecewo_session_free(sess);
}
