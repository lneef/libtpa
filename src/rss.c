#include <arpa/inet.h>
#include <assert.h>
#include <string.h>

#include "rss.h"

#define RSS_INPUT_LEN (12)

const uint8_t rss_default_key_adapted[RSS_KEY_LEN] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x30,
    0xf2, 0x0c, 0x77, 0xcb, 0x2d, 0xa3, 0xae, 0x7b, 0x30, 0xb4,
    0xd0, 0xca, 0x2b, 0xcb, 0x43, 0xa3, 0x8f, 0xb0, 0x41, 0x67,
    0x25, 0x3d, 0x25, 0x5b, 0x0e, 0xc2, 0x6d, 0x5a, 0x56, 0xda,
};

static inline uint32_t key_bit(const uint8_t *key, uint32_t bit) {
  return (key[bit / 8] >> (7 - bit % 8)) & 1;
}

uint32_t rss_hash_4tuple(const uint8_t *key, uint32_t src_ip, uint16_t src_port,
                         uint32_t dst_ip, uint16_t dst_port) {
  uint8_t input[RSS_INPUT_LEN];
  uint32_t window;
  uint32_t next_bit = 32;
  uint32_t hash = 0;
  int i;
  int shift;

  src_port = htons(src_port);
  dst_port = htons(dst_port);

  memcpy(&input[0], &src_ip, 4);
  memcpy(&input[4], &dst_ip, 4);
  memcpy(&input[8], &src_port, 2);
  memcpy(&input[10], &dst_port, 2);

  window = ((uint32_t)key[0] << 24) | ((uint32_t)key[1] << 16) |
           ((uint32_t)key[2] << 8) | key[3];

  for (i = 0; i < RSS_INPUT_LEN; i++) {
    for (shift = 7; shift >= 0; shift--) {
      if ((input[i] >> shift) & 1)
        hash ^= window;
      window = (window << 1) | key_bit(key, next_bit);
      next_bit++;
    }
  }

  return hash;
}

uint32_t rss_bucket(uint32_t hash, uint16_t reta_size) {
  assert(reta_size != 0);
  return hash % reta_size;
}

static uint32_t tag_hash(uint16_t port) {
  return rss_hash_4tuple(rss_default_key_adapted, 0, 0, 0, port);
}

void rss_build_tag_table(uint16_t *table) {
  int remaining = RSS_TAG_TABLE_SIZE;
  uint32_t bucket;
  uint32_t tag;
  int i;

  for (i = 0; i < RSS_TAG_TABLE_SIZE; i++)
    table[i] = RSS_TAG_NONE;

  for (tag = 0; remaining > 0 && tag < RSS_MAX_TAG; tag++) {
    bucket = rss_bucket(tag_hash(tag), RSS_TAG_TABLE_SIZE);
    if (table[bucket] == RSS_TAG_NONE) {
      table[bucket] = tag;
      remaining--;
    }
  }

  for (i = 0; i < RSS_TAG_TABLE_SIZE; i++)
    assert(table[i] != RSS_TAG_NONE);
}

static uint16_t nearest_queue_bucket(uint16_t bucket, uint16_t qid,
                                     uint16_t queue_cnt) {
  uint16_t lower;
  uint16_t upper;

  if (bucket <= qid)
    return qid;

  lower = bucket - (bucket - qid) % queue_cnt;
  upper = lower + queue_cnt;
  if (upper >= RSS_TAG_TABLE_SIZE || bucket - lower <= upper - bucket)
    return lower;

  return upper;
}

int rss_get_tag_for_queue(const uint16_t *table, uint32_t hash, uint16_t qid,
                          uint16_t queue_cnt) {
  uint16_t bucket;
  uint16_t slot;

  assert(queue_cnt > 0 && queue_cnt <= RSS_TAG_TABLE_SIZE && qid < queue_cnt);

  bucket = rss_bucket(hash, RSS_TAG_TABLE_SIZE);
  slot = bucket ^ nearest_queue_bucket(bucket, qid, queue_cnt);
  if (table[slot] == RSS_TAG_NONE)
    return -1;

  return table[slot];
}
