#ifndef BOARD_TEST_H
#define BOARD_TEST_H

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool nvs_ok;
    bool wifi_init_ok;
    bool wifi_scan_ok;
    bool dns_resolve_ok;
    bool http_probe_ok;
    bool tcp_probe_ok;
    bool metrics_server_ok;
    uint8_t total_pass;
    uint8_t total_fail;
} board_test_result_t;

esp_err_t board_test_run(board_test_result_t *result);

void board_test_print_report(const board_test_result_t *result);

#endif
