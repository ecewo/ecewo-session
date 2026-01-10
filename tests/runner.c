#include "ecewo-mock.h"
#include "ecewo-session.h"
#include "tests.h"
#include "tester.h"

void setup_all_routes(void) {
  get("/session/create", handler_session_create);
  get("/session/get", handler_session_get);
  get("/session/destroy", handler_session_destroy);
}

int main(void) {
  if (mock_init(setup_all_routes) != 0) {
    printf("ERROR: Failed to initialize mock server\n");
    return 1;
  }

  session_init();

  RUN_TEST(test_session_value_set_get);
  RUN_TEST(test_session_value_overwrite);
  RUN_TEST(test_session_value_remove);
  RUN_TEST(test_session_find);
  RUN_TEST(test_session_utf8_values);

  session_cleanup();
  mock_cleanup();
  return 0;
}