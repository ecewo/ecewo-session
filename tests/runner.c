#include "ecewo-mock.h"
#include "ecewo-session.h"
#include "tests.h"
#include "tester.h"

void setup_all_routes(ecewo_app_t *app) {
  // Runs on the event-loop thread, before ecewo_run(): the right place to
  // initialize the per-app session store.
  ecewo_session_init(app);

  ECEWO_GET(app, "/session/create", handler_session_create);
  ECEWO_GET(app, "/session/get", handler_session_get);
  ECEWO_GET(app, "/session/destroy", handler_session_destroy);

  ECEWO_GET(app, "/test/set-get", handler_test_set_get);
  ECEWO_GET(app, "/test/overwrite", handler_test_overwrite);
  ECEWO_GET(app, "/test/remove", handler_test_remove);
  ECEWO_GET(app, "/test/find", handler_test_find);
  ECEWO_GET(app, "/test/regenerate", handler_test_regenerate);
  ECEWO_GET(app, "/test/utf8", handler_test_utf8);
  ECEWO_GET(app, "/test/create-ttl", handler_test_create_ttl);
  ECEWO_GET(app, "/test/find-id", handler_test_find_id);
}

int main(void) {
  if (mock_init(setup_all_routes) != 0) {
    printf("ERROR: Failed to initialize mock server\n");
    return 1;
  }

  RUN_TEST(test_session_create);
  RUN_TEST(test_session_no_session);
  RUN_TEST(test_session_roundtrip);
  RUN_TEST(test_session_value_set_get);
  RUN_TEST(test_session_value_overwrite);
  RUN_TEST(test_session_value_remove);
  RUN_TEST(test_session_find);
  RUN_TEST(test_session_regenerate);
  RUN_TEST(test_session_expired);
  RUN_TEST(test_session_utf8_values);

  mock_cleanup();
  return 0;
}
