#ifndef PROBE_TYPES_H
#define PROBE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "config_manager.h"

typedef struct {
    bool success;
    uint32_t duration_ms;
    int status_code;
    char error_msg[128];
    
    union {
        struct {
            uint32_t connect_time_ms;
            uint32_t tls_time_ms;
            uint32_t ttfb_ms;
            int http_status;
        } http;
        
        struct {
            uint32_t connect_time_ms;
            uint32_t tls_time_ms;
        } tcp;
        
        struct {
            uint32_t resolve_time_ms;
            char resolved_ip[16];
        } dns;
        
        struct {
            uint32_t rtt_ms;
            uint8_t packets_sent;
            uint8_t packets_received;
        } icmp;
        
        struct {
            uint32_t connect_time_ms;
            uint32_t tls_time_ms;
            uint32_t handshake_time_ms;
        } ws;
    } details;
} probe_result_t;

typedef probe_result_t (*probe_func_t)(const probe_target_t *target, const probe_module_config_t *module_config);

probe_result_t probe_http_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_https_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_tcp_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_tcp_tls_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_dns_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_icmp_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_ws_execute(const probe_target_t *target, const probe_module_config_t *module_config);
probe_result_t probe_wss_execute(const probe_target_t *target, const probe_module_config_t *module_config);

#endif