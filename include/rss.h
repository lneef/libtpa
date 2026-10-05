#ifndef _RSS_H_
#define _RSS_H_

#include <stdint.h>

#define RSS_KEY_LEN 40
#define RSS_TAG_TABLE_SIZE 128
#define RSS_MAX_TAG (1 << 15)
#define RSS_TAG_NONE UINT16_MAX

extern const uint8_t rss_default_key_adapted[RSS_KEY_LEN];

/* ips in network byte order, ports in host byte order */
uint32_t rss_hash_4tuple(const uint8_t *key, uint32_t src_ip, uint16_t src_port,
                         uint32_t dst_ip, uint16_t dst_port);
uint32_t rss_bucket(uint32_t hash, uint16_t reta_size);
void rss_build_tag_table(uint16_t *table);
int rss_get_tag_for_queue(const uint16_t *table, uint32_t hash, uint16_t qid);

#endif
