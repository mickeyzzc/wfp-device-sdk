/*
 * wfp-core 骨架实现：行格式化与最小分发。
 * 首个真实抽取时按契约补全（JSON 参数解析、caps 表、命令查找）。
 */
#include <stdio.h>
#include <string.h>

#include "wfp.h"

static wfp_port_t port;
static char devid[17];
static char fw[16];

void wfp_init(const wfp_port_t *p, const char *id, const char *fwver)
{
    port = *p;
    snprintf(devid, sizeof(devid), "%s", id);
    snprintf(fw, sizeof(fw), "%s", fwver);
}

void wfp_telemetry(const char *tag, const char *payload)
{
    char line[256];
    int n = snprintf(line, sizeof(line), "#%s %s", tag, payload);
    if (n > 0 && (size_t)n < sizeof(line))
        port.send_line(port.user, line);
}

void wfp_evt(const char *payload_json)
{
    wfp_telemetry("EVT", payload_json);
}

void wfp_ack(int id)
{
    char line[32];
    snprintf(line, sizeof(line), "{\"id\":%d}", id);
    wfp_telemetry("ACK", line);
}

void wfp_err(int id, const char *code, const char *msg)
{
    char line[96];
    int n = snprintf(line, sizeof(line), "{\"id\":%d,\"code\":\"%s\",\"msg\":\"%s\"}",
                     id, code, msg);
    if (n > 0 && (size_t)n < sizeof(line))
        wfp_telemetry("ERR", line);
}
