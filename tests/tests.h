#ifndef ECEWO_COOKIE_TESTS
#define ECEWO_COOKIE_TESTS

#include "ecewo.h"

void handler_session_create(Req *req, Res *res);
void handler_session_get(Req *req, Res *res);
void handler_session_destroy(Req *req, Res *res);

int test_session_create(void);
int test_session_no_session(void);
int test_session_value_set_get(void);
int test_session_value_overwrite(void);
int test_session_value_remove(void);
int test_session_find(void);
int test_session_utf8_values(void);

#endif
