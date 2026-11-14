/*
 * lapis_amfi: minimal in-kernel entitlement provider for the iPadOS 26 (xnu-12377.121.6)
 * lapis kernel, which ships without AppleMobileFileIntegrity.kext.
 *
 * Registers an amfi_t (libkern/libkern/amfi/amfi.h) through amfi_interface_register()
 * right after load_static_trust_cache() and before machine_lockdown() (the `amfi` pointer
 * is SECURITY_READ_ONLY_LATE).  Implemented:
 *   OSEntitlements.queryEntitlementBooleanWithProc / queryEntitlementStringWithProc /
 *   copyEntitlementAsOSObjectWithProc (+ the os_entitlements variants: no OSEntitlements
 *   objects are created, so they answer "not entitled").
 * Entitlements are read from the process' code signature: csproc_get_blob() ->
 * csblob_get_entitlements() (the XML blob, CSSLOT_ENTITLEMENTS, whose hash the kernel checks
 * against the CodeDirectory special slot), then a small plist-XML lookup of <key>NAME</key>.
 * Everything else answers "unsupported": TrustCache.* return an error (no AMFI trust caches),
 * no MTE hooks, adjustContext succeeds without attaching anything (csb_entitlements stays NULL,
 * so OSEntitlements_invalidate/asdict/get_xml/get_transmuted are never reached with data).
 * Offsets verified against the binary: get_xml +0x20 (csops_internal), TrustCache.loadModule
 * +0x120 / query +0x130 (load_static/query_trust_cache), OSEntitlements.adjustContext +0x1a0
 * (accelerate_entitlement_queries), copyEntitlementAsOSObjectWithProc +0x1e0
 * (IOUserClient::copyClientEntitlement).
 */
#include <stdint.h>
#include <stddef.h>
typedef int kern_return_t;
typedef struct proc *proc_t;
struct cs_blob;
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define KERN_NOT_SUPPORTED 46

extern struct cs_blob *csproc_get_blob(proc_t);
extern int csblob_get_entitlements(struct cs_blob *, void **, size_t *);
extern void amfi_interface_register(const void *);
extern void load_static_trust_cache(void);
extern void IOLog(const char *, ...);
extern void *kOSBooleanTrue, *kOSBooleanFalse;
extern void *OSString_withCString(const char *) __asm__("_ZN8OSString11withCStringEPKc");
extern void *OSNumber_withNumber(unsigned long long, unsigned int) __asm__("_ZN8OSNumber10withNumberEyj");

static size_t a_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int a_memeq(const char *a, const char *b, size_t n) { while (n--) if (*a++ != *b++) return 0; return 1; }

/* XML entitlements of proc (CS_GenericBlob payload) */
static int ents_xml(proc_t p, const char **x, size_t *n)
{
	struct cs_blob *b;
	void *start = 0; size_t len = 0;
	if (!p || !(b = csproc_get_blob(p))) return 0;
	if (csblob_get_entitlements(b, &start, &len) || !start || len <= 8) return 0;
	*x = (const char *)start + 8; *n = len - 8;
	return 1;
}
static const char *skipws(const char *s, const char *e) { while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++; return s; }
static int pfx(const char *s, const char *e, const char *lit) { size_t l = a_strlen(lit); return (size_t)(e - s) >= l && a_memeq(s, lit, l); }
/* returns pointer to the value element following <key>name</key> (top-level match) */
static const char *find_value(const char *x, size_t n, const char *name)
{
	const char *e = x + n, *s = x;
	size_t nl = a_strlen(name);
	int depth = 0;   /* <dict> nesting: only depth 1 (the top dict) keys count */
	while (s < e) {
		if (*s != '<') { s++; continue; }
		if (pfx(s, e, "<dict>")) { depth++; s += 6; continue; }
		if (pfx(s, e, "</dict>")) { depth--; s += 7; continue; }
		if (pfx(s, e, "<key>")) {
			const char *k = s + 5;
			if (depth == 1 && (size_t)(e - k) >= nl + 6 && a_memeq(k, name, nl) && pfx(k + nl, e, "</key>"))
				return skipws(k + nl + 6, e);
			s = k; continue;
		}
		s++;
	}
	return 0;
}
static int str_eq_elem(const char *v, const char *e, const char *val, const char **next)
{   /* v at "<string>"; compares content with val */
	size_t vl = a_strlen(val);
	const char *c = v + 8, *q = c;
	while (q < e && *q != '<') q++;
	if (next) *next = q;
	return (size_t)(q - c) == vl && a_memeq(c, val, vl);
}

static kern_return_t q_bool_proc(const proc_t p, const char *name)
{
	const char *x, *v; size_t n;
	if (!name || !ents_xml(p, &x, &n) || !(v = find_value(x, n, name))) return KERN_FAILURE;
	return pfx(v, x + n, "<true/>") ? KERN_SUCCESS : KERN_FAILURE;
}
static kern_return_t q_str_proc(const proc_t p, const char *name, const char *val)
{
	const char *x, *v, *e, *nx; size_t n;
	if (!name || !val || !ents_xml(p, &x, &n) || !(v = find_value(x, n, name))) return KERN_FAILURE;
	e = x + n;
	if (pfx(v, e, "<string>")) return str_eq_elem(v, e, val, 0) ? KERN_SUCCESS : KERN_FAILURE;
	if (pfx(v, e, "<array>")) {
		v += 7;
		for (;;) {
			v = skipws(v, e);
			if (!pfx(v, e, "<string>")) break;
			if (str_eq_elem(v, e, val, &nx)) return KERN_SUCCESS;
			v = nx; if (!pfx(v, e, "</string>")) break; v += 9;
		}
	}
	return KERN_FAILURE;
}
static kern_return_t copy_obj_proc(const proc_t p, const char *name, void **obj)
{
	const char *x, *v, *e; size_t n;
	char buf[256];
	if (obj) *obj = 0;
	if (!name || !obj || !ents_xml(p, &x, &n) || !(v = find_value(x, n, name))) return KERN_FAILURE;
	e = x + n;
	if (pfx(v, e, "<true/>")) { *obj = kOSBooleanTrue; return KERN_SUCCESS; }
	if (pfx(v, e, "<false/>")) { *obj = kOSBooleanFalse; return KERN_SUCCESS; }
	if (pfx(v, e, "<string>")) {
		const char *c = v + 8; size_t l = 0;
		while (c + l < e && c[l] != '<' && l < sizeof(buf) - 1) { buf[l] = c[l]; l++; }
		buf[l] = 0;
		*obj = OSString_withCString(buf);
		return *obj ? KERN_SUCCESS : KERN_FAILURE;
	}
	if (pfx(v, e, "<integer>")) {
		const char *c = v + 9; unsigned long long val = 0; int neg = 0;
		if (c < e && *c == '-') { neg = 1; c++; }
		while (c < e && *c >= '0' && *c <= '9') val = val * 10 + (unsigned)(*c++ - '0');
		*obj = OSNumber_withNumber(neg ? (unsigned long long)-(long long)val : val, 64);
		return *obj ? KERN_SUCCESS : KERN_FAILURE;
	}
	return KERN_FAILURE;   /* arrays/dicts: not supported yet */
}

static void ose_invalidate(void *o) { (void)o; }
static void *ose_asdict(void *o) { (void)o; return 0; }
static int ose_query(void *o, uint8_t *cd, const void *q, size_t ql) { return 1; }
static int ose_get_blob(void *o, const void **b) { if (b) *b = 0; return 0; }
static kern_return_t adjust_ctx(void *o, struct cs_blob *b, const void *ctx) { return KERN_SUCCESS; }
static kern_return_t adjust_mon(void *o, const void *c, const void *m, const char *id, uint32_t f) { return KERN_SUCCESS; }
static kern_return_t adjust_nomon(void *o, struct cs_blob *b) { return KERN_SUCCESS; }
static kern_return_t q_bool(const void *o, const char *n) { return KERN_FAILURE; }
static kern_return_t q_str(const void *o, const char *n, const char *v) { return KERN_FAILURE; }
static kern_return_t copy_obj(const void *o, const char *n, void **obj) { if (obj) *obj = 0; return KERN_FAILURE; }
/* TCReturn_t {component:8, error:8, uniqueError:16}: error != 0 -> failure */
static uint32_t tc_fail(void) { return 0x00010100u; }
static void *null_fn(void) { return 0; }

#define S(off, fn) [(off) / 8] = (uint64_t)(uintptr_t)(fn)
const uint64_t lapis_amfi_if[0x210 / 8] __attribute__((aligned(8))) = {
	S(0x00, ose_invalidate), S(0x08, ose_asdict), S(0x10, ose_query),
	S(0x18, ose_get_blob), S(0x20, ose_get_blob),
	/* 0x28..0x100: CoreEntitlements (unused: libCoreEntitlements not provided), legacy
	   profile exemptions, udid, query_context_to_object -> NULL */
	/* TrustCache @0x118 */
	[0x118 / 8] = 4,   /* TRUST_CACHE_INTERFACE_VERSION */
	S(0x120, tc_fail), S(0x128, tc_fail), S(0x130, tc_fail), S(0x138, tc_fail), S(0x140, tc_fail),
	S(0x148, tc_fail), S(0x150, tc_fail), S(0x158, tc_fail), S(0x160, tc_fail), S(0x168, tc_fail),
	S(0x170, tc_fail), S(0x178, tc_fail), S(0x180, tc_fail), S(0x188, tc_fail), S(0x190, tc_fail),
	/* OSEntitlements @0x198 */
	[0x198 / 8] = 2,   /* OSENTITLEMENTS_INTERFACE_VERSION */
	S(0x1a0, adjust_ctx), S(0x1a8, adjust_mon), S(0x1b0, adjust_nomon),
	S(0x1b8, q_bool), S(0x1c0, q_bool_proc), S(0x1c8, q_str), S(0x1d0, q_str_proc),
	S(0x1d8, copy_obj), S(0x1e0, copy_obj_proc),
	/* 0x1e8..0x208: MTE hooks -> NULL (callers check) */
};

/* replaces the bl load_static_trust_cache in kernel_bootstrap_thread */
void lapis_amfi_boot(void)
{
	(void)null_fn;
	load_static_trust_cache();
	amfi_interface_register(lapis_amfi_if);
	IOLog("LAPIS amfi: entitlement provider registered\n");
}
