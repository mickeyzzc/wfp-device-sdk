/* 
 * DNS 探测实现
 * 
 * 该模块提供 DNS 域名解析功能。
 * 通过调用 gethostbyname() 解析目标域名，并测量解析时间。
 * 解析结果包括解析时间和解析后的 IP 地址。
 */

#include "probe_types.h"
#include <string.h>
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_timer.h"

probe_result_t probe_dns_execute(const probe_target_t *target, const probe_module_config_t *module_config)
{
    probe_result_t result = {0};
    result.success = false;
    
    int64_t start_time = esp_timer_get_time();
    
    struct hostent *host = gethostbyname(target->target);
    
    int64_t end_time = esp_timer_get_time();
    result.duration_ms = (uint32_t)((end_time - start_time) / 1000);
    
    if (host != NULL && host->h_addr_list[0] != NULL) {
        result.success = true;
        result.status_code = 0;
        result.details.dns.resolve_time_ms = result.duration_ms;
        
        struct in_addr addr;
        memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
        strncpy(result.details.dns.resolved_ip, inet_ntoa(addr), 
                sizeof(result.details.dns.resolved_ip));
    } else {
        strncpy(result.error_msg, "DNS resolution failed", sizeof(result.error_msg));
    }
    
    return result;
}
