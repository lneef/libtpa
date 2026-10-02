#include <stdio.h>
#include <assert.h>
#include <arpa/inet.h>

#include "rss.h"

#define RETA_SIZE	128

struct vector {
	const char *src_ip;
	uint16_t src_port;
	const char *dst_ip;
	uint16_t dst_port;
	uint32_t hash;
	uint32_t bucket;
};

static const struct vector vectors[] = {
	{ "10.0.1.10",   40000, "10.0.2.20",   5000,  0x2f2b5eff, 127 },
	{ "10.0.1.11",   40000, "10.0.2.20",   5000,  0x2f2b5eff, 127 },
	{ "10.0.1.10",   40001, "10.0.2.20",   5000,  0x562d651a, 26  },
	{ "1.2.3.4",     7000,  "1.2.3.5",     7000,  0x264872bb, 59  },
	{ "192.168.0.1", 1,     "192.168.0.2", 65535, 0xda48c43c, 60  },
};

static uint32_t hash(const char *src_ip, uint16_t src_port, const char *dst_ip, uint16_t dst_port)
{
	return rss_hash_4tuple(rss_default_key_adapted, inet_addr(src_ip), htons(src_port),
			       inet_addr(dst_ip), htons(dst_port));
}

static void test_reference_vectors(void)
{
	const struct vector *v;
	uint32_t h;
	int i;

	printf("testing %s ...\n", __func__);

	for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		v = &vectors[i];
		h = hash(v->src_ip, v->src_port, v->dst_ip, v->dst_port);
		assert(h == v->hash);
		assert(rss_bucket(h, RETA_SIZE) == v->bucket);
	}
}

static void test_tag_steers_rx_port_to_queue(void)
{
	static const uint16_t ports[] = { 1, 4711, 30000, 65535 };
	uint32_t remote_ip = inet_addr("10.0.2.20");
	uint32_t local_ip = inet_addr("10.0.1.10");
	uint16_t remote_port = 3000;
	uint16_t table[RSS_TAG_TABLE_SIZE];
	uint16_t queue_cnt;
	uint16_t qid;
	uint16_t port;
	uint32_t h;
	int tag;
	int i;

	printf("testing %s ...\n", __func__);

	rss_build_tag_table(table);

	for (queue_cnt = 2; queue_cnt <= 8; queue_cnt += 2) {
		for (i = 0; i < sizeof(ports) / sizeof(ports[0]); i++) {
			port = ports[i];
			for (qid = 0; qid < queue_cnt; qid++) {
				h = rss_hash_4tuple(rss_default_key_adapted, remote_ip, htons(remote_port),
						    local_ip, htons(port));
				tag = rss_get_tag_for_queue(table, h, qid, queue_cnt);
				assert(tag >= 0);

				h = rss_hash_4tuple(rss_default_key_adapted, remote_ip, htons(remote_port),
						    local_ip, htons(port ^ tag));
				assert(rss_bucket(h, RETA_SIZE) % queue_cnt == qid);
			}
		}
	}
}

static void test_source_address_is_cancelled(void)
{
	printf("testing %s ...\n", __func__);

	assert(hash("10.0.1.10", 40000, "10.0.2.20", 5000) ==
	       hash("10.0.1.11", 40000, "10.0.2.20", 5000));
}

static void test_source_port_low_bit_reaches_the_hash(void)
{
	printf("testing %s ...\n", __func__);

	assert(hash("10.0.1.10", 40000, "10.0.2.20", 5000) !=
	       hash("10.0.1.10", 40001, "10.0.2.20", 5000));
}

int main(int argc, char *argv[])
{
	test_reference_vectors();
	test_tag_steers_rx_port_to_queue();
	test_source_address_is_cancelled();
	test_source_port_low_bit_reaches_the_hash();

	return 0;
}
