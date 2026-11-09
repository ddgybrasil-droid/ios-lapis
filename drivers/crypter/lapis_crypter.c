/* Identity text crypter so FairPlay/apple_protect load commands don't kill launchd.
 * Our IPSW extract has cryptid=0 on system binaries, but some LC_ENCRYPTION_INFO
 * still appear (or DSC apple_protect); without a registered crypter the loader
 * returns LOAD_FAILURE → EXEC_EXIT_REASON_DECRYPT. */
#include "../include/kern.h"

#define LOG(fmt, ...) klog("LapisCrypt: " fmt "\n", ##__VA_ARGS__)

struct pager_crypt_info {
	int  (*page_decrypt)(const void *src, void *dst, unsigned long long off, void *ops);
	void (*crypt_end)(void *ops);
	void *crypt_ops;
	volatile int crypt_refcnt;
};

extern void text_crypter_create_hook_set(void *hook);

static int id_decrypt(const void *src, void *dst, unsigned long long off, void *ops)
{
	(void)off; (void)ops;
	/* page-sized; caller guarantees sizes */
	memcpy(dst, src, 0x4000);
	return 0;
}
static void id_end(void *ops) { (void)ops; }

static int lapis_crypter_create(struct pager_crypt_info *ci, const char *id, void *data)
{
	(void)data;
	ci->page_decrypt = id_decrypt;
	ci->crypt_end = id_end;
	ci->crypt_ops = NULL;
	ci->crypt_refcnt = 0;
	LOG("crypter create id='%s' (identity)", id ? id : "?");
	return 0; /* KERN_SUCCESS / kIOReturnSuccess */
}

void lapis_crypter_start(void)
{
	text_crypter_create_hook_set((void *)lapis_crypter_create);
	LOG("identity text_crypter registered");
}
