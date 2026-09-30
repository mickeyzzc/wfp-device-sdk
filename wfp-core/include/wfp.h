/*
 * wfp-core — WFP 设备协议的可移植端点（骨架）。
 * 契约：homepulse 平台仓 docs/wfp-protocol.md（v2 = #S1 家族超集）。
 *
 * 零 SDK 依赖，仅以下三个移植口（见 README 移植口表）。
 * API 面为草案：随首个真实抽取（csi_collector / rmt_dht 之后的第三块板）
 * 定稿，定稿前不追求完备。
 */
#ifndef WFP_H
#define WFP_H

#include <stddef.h>

#define WFP_PROTO 2

/* 移植口：由各 SDK 适配层实现（ports/）。 */
typedef struct {
    /* 输出一行（不含换行，wfp-core 负责加 \n）。返回 <0 = 发送失败。 */
    int  (*send_line)(void *user, const char *line);
    /* 单调毫秒（遥测 t_ms）。不得回绕跳变。 */
    long (*now_ms)(void *user);
    /* 持久键值（校准下发等）。返回 0 = 命中，<0 = 无此键/失败。 */
    int  (*kv_load)(void *user, const char *key, char *out, size_t out_sz);
    int  (*kv_store)(void *user, const char *key, const char *val);
    void *user;
} wfp_port_t;

/* 能力与命令注册（启动期完成，运行期只读）。 */
void wfp_init(const wfp_port_t *port, const char *devid, const char *fw);
void wfp_cap_add(const char *cap);                       /* 如 "csi"、"env"、"net" */
typedef void (*wfp_cmd_fn)(const char *args_json);       /* 参数：单层扁平键值 JSON */
void wfp_cmd_add(const char *name, wfp_cmd_fn fn);       /* 如 "csi.start"、"sys.reboot" */

/* 运行期。 */
void wfp_feed(const char *line);                         /* 喂入一行（串口/TCP 收到，去\n） */
void wfp_telemetry(const char *tag, const char *payload);/* 发 #<tag> <payload> 遥测行 */
void wfp_evt(const char *payload_json);                  /* 发 #EVT 行 */
void wfp_ack(int id);                                    /* 发 #ACK {"id":n} */
void wfp_err(int id, const char *code, const char *msg); /* 发 #ERR 行 */

#endif /* WFP_H */
