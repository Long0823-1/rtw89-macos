// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Runtime half of the Linux kernel API compatibility layer (workqueues, timers, completions, skb, firmware, printk).
#include <kcompat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>
#include <ctype.h>
extern int backtrace(void **, int);
extern void backtrace_symbols_fd(void *const *, int, int);

volatile unsigned long jiffies;
static pthread_t ticker_thr;

u64 kc_now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

u64 kc_now_ms(void)
{
	return kc_now_ns() / 1000000ULL;
}

void kc_sleep_us(unsigned long us)
{
	struct timespec ts = { .tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000 };
	nanosleep(&ts, NULL);
}

static void *ticker(void *arg)
{
	for (;;) {
		jiffies = (unsigned long)kc_now_ms();
		usleep(1000);
	}
	return NULL;
}

void kc_warn(const char *file, int line)
{
	fprintf(stderr, "[WARN] %s:%d\n", file, line);
}

u32 kc_random32(void)
{
	static int init;
	if (!init) { init = 1; srandom((unsigned)kc_now_ns()); }
	return (u32)random();
}

void kc_mutex_init(pthread_mutex_t *m)
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(m, &a);
	pthread_mutexattr_destroy(&a);
}

int kstrtou8(const char *s, unsigned int base, u8 *res) { char *e; unsigned long v = strtoul(s, &e, base); if (e == s || v > 255) return -EINVAL; *res = (u8)v; return 0; }
int kstrtou32(const char *s, unsigned int base, u32 *res) { char *e; unsigned long v = strtoul(s, &e, base); if (e == s) return -EINVAL; *res = (u32)v; return 0; }
int kstrtoint(const char *s, unsigned int base, int *res) { char *e; long v = strtol(s, &e, base); if (e == s) return -EINVAL; *res = (int)v; return 0; }

/* ---------------- printk with %pM / %pI4 / %*ph extensions ---------------- */
static int kc_log_level = 6;
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

static char *expand_fmt(const char *fmt, va_list *ap_in, char *out, size_t outsz)
{
	/* Pre-format the Linux-only specifiers into strings; simplest is to walk fmt and rebuild it,
	 * consuming only the pointer args for the extended specifiers by converting them to %s. */
	(void)ap_in; (void)outsz; (void)out;
	return (char *)fmt;
}

static int kc_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	/* Handle Linux extensions manually by splitting the format at each %p?, %*ph. */
	size_t n = 0;
	const char *p = fmt;
	char piece[512];
	#define EMIT(s) do { const char *_s = (s); size_t _l = strlen(_s); if (n + _l < size) memcpy(buf + n, _s, _l); n += _l; } while (0)
	while (*p) {
		if (*p != '%') {
			char c[2] = { *p++, 0 };
			EMIT(c);
			continue;
		}
		/* collect one conversion specification */
		const char *q = p + 1;
		char spec[64]; size_t sl = 0;
		spec[sl++] = '%';
		/* flags/width/precision incl. '*' */
		int star_w = 0;
		while (*q && strchr("-+ #0123456789.*", *q)) {
			if (*q == '*') star_w++;
			if (sl < sizeof spec - 8) spec[sl++] = *q;
			q++;
		}
		/* length modifiers */
		while (*q && strchr("hlLzjtq", *q)) { if (sl < sizeof spec - 8) spec[sl++] = *q; q++; }
		char conv = *q;
		if (conv == 'p' && (q[1] == 'M' || q[1] == 'm')) {
			const u8 *a = va_arg(ap, const u8 *);
			if (a) snprintf(piece, sizeof piece, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
			else snprintf(piece, sizeof piece, "(null)");
			EMIT(piece); p = q + 2; continue;
		}
		if (conv == 'p' && q[1] == 'I' && q[2] == '4') {
			const u8 *a = va_arg(ap, const u8 *);
			snprintf(piece, sizeof piece, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
			EMIT(piece); p = q + 3; continue;
		}
		if (conv == 'p' && q[1] == 'h') {
			int len = star_w ? va_arg(ap, int) : 1;
			const u8 *a = va_arg(ap, const u8 *);
			char *o = piece;
			for (int i = 0; i < len && i < 120; i++) o += snprintf(o, 4, "%s%02x", i ? " " : "", a ? a[i] : 0);
			*o = 0;
			EMIT(piece); p = q + 2; continue;
		}
		if (conv == 'p') {
			/* skip Linux pointer extension letters */
			void *ptr = va_arg(ap, void *);
			snprintf(piece, sizeof piece, "%p", ptr);
			EMIT(piece);
			p = q + 1;
			while (*p && isalnum((unsigned char)*p) && (q[1] >= 'A' && q[1] <= 'z')) { p++; break; }
			continue;
		}
		if (conv == '%') { EMIT("%"); p = q + 1; continue; }
		/* standard conversion: delegate to libc with the collected spec */
		spec[sl++] = conv; spec[sl] = 0;
		int ints = star_w;
		(void)ints;
		switch (conv) {
		case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c': {
			/* determine integer width from modifiers */
			if (strstr(spec, "ll") || strstr(spec, "j") || strstr(spec, "z") || strstr(spec, "t") || strstr(spec, "q")) {
				/* 64-bit; star widths need int args first */
				int w1 = 0, w2 = 0;
				if (star_w >= 1) w1 = va_arg(ap, int);
				if (star_w >= 2) w2 = va_arg(ap, int);
				long long v = va_arg(ap, long long);
				if (star_w == 0) snprintf(piece, sizeof piece, spec, v);
				else if (star_w == 1) snprintf(piece, sizeof piece, spec, w1, v);
				else snprintf(piece, sizeof piece, spec, w1, w2, v);
			} else if (strstr(spec, "l")) {
				int w1 = 0, w2 = 0;
				if (star_w >= 1) w1 = va_arg(ap, int);
				if (star_w >= 2) w2 = va_arg(ap, int);
				long v = va_arg(ap, long);
				if (star_w == 0) snprintf(piece, sizeof piece, spec, v);
				else if (star_w == 1) snprintf(piece, sizeof piece, spec, w1, v);
				else snprintf(piece, sizeof piece, spec, w1, w2, v);
			} else {
				int w1 = 0, w2 = 0;
				if (star_w >= 1) w1 = va_arg(ap, int);
				if (star_w >= 2) w2 = va_arg(ap, int);
				int v = va_arg(ap, int);
				if (star_w == 0) snprintf(piece, sizeof piece, spec, v);
				else if (star_w == 1) snprintf(piece, sizeof piece, spec, w1, v);
				else snprintf(piece, sizeof piece, spec, w1, w2, v);
			}
			break;
		}
		case 's': {
			int w1 = 0, w2 = 0;
			if (star_w >= 1) w1 = va_arg(ap, int);
			if (star_w >= 2) w2 = va_arg(ap, int);
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";
			if (star_w == 0) snprintf(piece, sizeof piece, spec, s);
			else if (star_w == 1) snprintf(piece, sizeof piece, spec, w1, s);
			else snprintf(piece, sizeof piece, spec, w1, w2, s);
			break;
		}
		case 'f': case 'e': case 'g': {
			double v = va_arg(ap, double);
			snprintf(piece, sizeof piece, spec, v);
			break;
		}
		default:
			snprintf(piece, sizeof piece, "%s", spec);
			break;
		}
		EMIT(piece);
		p = q + 1;
	}
	if (size) buf[n < size ? n : size - 1] = 0;
	return (int)n;
}

void kc_printk(int level, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	if (level > kc_log_level) return;
	va_start(ap, fmt);
	kc_vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	pthread_mutex_lock(&print_lock);
	fputs(buf, stderr);
	fflush(stderr);
	if (level <= 4 && getenv("RTW89_BT")) { void *bt[24]; int n = backtrace(bt, 24); backtrace_symbols_fd(bt, n, 2); }
	pthread_mutex_unlock(&print_lock);
}

char *kc_asprintf(const char *fmt, ...)
{
	char *buf = malloc(512);
	va_list ap;
	va_start(ap, fmt);
	kc_vsnprintf(buf, 512, fmt, ap);
	va_end(ap);
	return buf;
}

/* ---------------- completion ---------------- */
void kc_init_completion(struct completion *x)
{
	pthread_mutex_init(&x->m, NULL);
	pthread_cond_init(&x->c, NULL);
	x->done = 0;
}

void complete(struct completion *x)
{
	pthread_mutex_lock(&x->m);
	x->done++;
	pthread_cond_signal(&x->c);
	pthread_mutex_unlock(&x->m);
}

void complete_all(struct completion *x)
{
	pthread_mutex_lock(&x->m);
	x->done = 1u << 30;
	pthread_cond_broadcast(&x->c);
	pthread_mutex_unlock(&x->m);
}

void wait_for_completion(struct completion *x)
{
	pthread_mutex_lock(&x->m);
	while (!x->done)
		pthread_cond_wait(&x->c, &x->m);
	if (x->done < (1u << 30)) x->done--;
	pthread_mutex_unlock(&x->m);
}

unsigned long wait_for_completion_timeout(struct completion *x, unsigned long timeout_ms)
{
	struct timespec ts;
	unsigned long left = 0;
	u64 start = kc_now_ms();
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
	pthread_mutex_lock(&x->m);
	while (!x->done) {
		if (pthread_cond_timedwait(&x->c, &x->m, &ts) != 0)
			break;
	}
	if (x->done) {
		if (x->done < (1u << 30)) x->done--;
		u64 el = kc_now_ms() - start;
		left = el < timeout_ms ? timeout_ms - el : 1;
	}
	pthread_mutex_unlock(&x->m);
	return left;
}

int wait_for_completion_interruptible_timeout(struct completion *x, unsigned long timeout_ms)
{
	return (int)wait_for_completion_timeout(x, timeout_ms);
}

bool try_wait_for_completion(struct completion *x)
{
	bool r = false;
	pthread_mutex_lock(&x->m);
	if (x->done) { if (x->done < (1u << 30)) x->done--; r = true; }
	pthread_mutex_unlock(&x->m);
	return r;
}

bool completion_done(struct completion *x)
{
	return x->done != 0;
}

/* ---------------- workqueue ---------------- */
struct kc_wq_thread {
	pthread_t thr;
	pthread_mutex_t m;
	pthread_cond_t c;
	struct list_head queue;
	struct list_head delayed;	/* work items waiting on expiry (struct work_struct.entry) */
	int stop;
	int running;
	struct work_struct *current;
};

static struct kc_wq_thread sys_wq[4];
static int sys_wq_init;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static struct workqueue_struct *ordered_wqs[16];

/* The wq cookie in work->wq points to a kc_wq_thread. */
static void *wq_thread_fn(void *arg)
{
	struct kc_wq_thread *t = arg;
	for (;;) {
		struct work_struct *w = NULL;
		pthread_mutex_lock(&t->m);
		for (;;) {
			u64 now = kc_now_ms();
			u64 next_exp = 0;
			struct list_head *pos, *n;
			list_for_each_safe(pos, n, &t->delayed) {
				struct work_struct *dw = container_of(pos, struct work_struct, entry);
				struct delayed_work *d = container_of(dw, struct delayed_work, work);
				if (d->expires_ms <= now) {
					list_del_init(&dw->entry);
					list_add_tail(&dw->entry, &t->queue);
				} else if (!next_exp || d->expires_ms < next_exp) {
					next_exp = d->expires_ms;
				}
			}
			if (!list_empty(&t->queue)) {
				w = list_first_entry(&t->queue, struct work_struct, entry);
				list_del_init(&w->entry);
				break;
			}
			if (t->stop) { pthread_mutex_unlock(&t->m); return NULL; }
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			u64 wait_ms = next_exp ? (next_exp > now ? next_exp - now : 0) : 100;
			if (wait_ms > 100) wait_ms = 100;
			ts.tv_sec += wait_ms / 1000;
			ts.tv_nsec += (wait_ms % 1000) * 1000000L;
			if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
			pthread_cond_timedwait(&t->c, &t->m, &ts);
		}
		w->pending = 0;
		t->current = w;
		t->running = 1;
		pthread_mutex_unlock(&t->m);
		w->func(w);
		pthread_mutex_lock(&t->m);
		t->running = 0;
		t->current = NULL;
		pthread_cond_broadcast(&t->c);
		pthread_mutex_unlock(&t->m);
	}
	return NULL;
}

static void wq_start(struct kc_wq_thread *t)
{
	pthread_mutex_init(&t->m, NULL);
	pthread_cond_init(&t->c, NULL);
	INIT_LIST_HEAD(&t->queue);
	INIT_LIST_HEAD(&t->delayed);
	pthread_create(&t->thr, NULL, wq_thread_fn, t);
}

static void kc_init_once(void)
{
	pthread_create(&ticker_thr, NULL, ticker, NULL);
	for (int i = 0; i < 4; i++)
		wq_start(&sys_wq[i]);
	sys_wq_init = 1;
}

void kc_runtime_init(void)
{
	pthread_once(&once, kc_init_once);
}

static struct kc_wq_thread *pick_thread(struct workqueue_struct *wq, struct work_struct *w)
{
	kc_runtime_init();
	if (wq)
		return (struct kc_wq_thread *)wq;
	if (w->wq)
		return w->wq;
	/* system workqueue: pick by hash of work address so one work never runs concurrently with itself */
	return &sys_wq[((uintptr_t)w >> 4) % 4];
}

bool queue_work(struct workqueue_struct *wq, struct work_struct *w)
{
	struct kc_wq_thread *t = pick_thread(wq, w);
	bool queued = false;
	pthread_mutex_lock(&t->m);
	if (!w->pending) {
		w->pending = 1;
		w->wq = t;
		list_add_tail(&w->entry, &t->queue);
		pthread_cond_signal(&t->c);
		queued = true;
	}
	pthread_mutex_unlock(&t->m);
	return queued;
}

bool schedule_work(struct work_struct *w) { return queue_work(NULL, w); }

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay_ms)
{
	struct work_struct *w = &d->work;
	if (!delay_ms) return queue_work(wq, w);
	struct kc_wq_thread *t = pick_thread(wq, w);
	bool queued = false;
	pthread_mutex_lock(&t->m);
	if (!w->pending) {
		w->pending = 1;
		w->wq = t;
		d->expires_ms = kc_now_ms() + delay_ms;
		list_add_tail(&w->entry, &t->delayed);
		pthread_cond_signal(&t->c);
		queued = true;
	}
	pthread_mutex_unlock(&t->m);
	return queued;
}

bool schedule_delayed_work(struct delayed_work *d, unsigned long delay_ms) { return queue_delayed_work(NULL, d, delay_ms); }

static bool cancel_locked(struct work_struct *w)
{
	struct kc_wq_thread *t = w->wq;
	bool was = false;
	if (!t) return false;
	pthread_mutex_lock(&t->m);
	if (w->pending) {
		list_del_init(&w->entry);
		w->pending = 0;
		was = true;
	}
	pthread_mutex_unlock(&t->m);
	return was;
}

static void wait_not_running(struct work_struct *w)
{
	struct kc_wq_thread *t = w->wq;
	if (!t) return;
	if (pthread_self() == t->thr) return;
	pthread_mutex_lock(&t->m);
	while (t->running && t->current == w)
		pthread_cond_wait(&t->c, &t->m);
	pthread_mutex_unlock(&t->m);
}

bool cancel_work_sync(struct work_struct *w)
{
	bool was = cancel_locked(w);
	wait_not_running(w);
	return was;
}

bool cancel_delayed_work(struct delayed_work *d) { return cancel_locked(&d->work); }

bool cancel_delayed_work_sync(struct delayed_work *d)
{
	bool was = cancel_locked(&d->work);
	wait_not_running(&d->work);
	return was;
}

bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay_ms)
{
	cancel_locked(&d->work);
	return queue_delayed_work(wq, d, delay_ms);
}

bool flush_work(struct work_struct *w)
{
	struct kc_wq_thread *t = w->wq;
	if (!t) return false;
	for (int i = 0; i < 5000; i++) {
		pthread_mutex_lock(&t->m);
		bool busy = w->pending || (t->running && t->current == w);
		pthread_mutex_unlock(&t->m);
		if (!busy) break;
		if (pthread_self() == t->thr) break;
		usleep(1000);
	}
	return true;
}

bool flush_delayed_work(struct delayed_work *d)
{
	/* force immediate execution */
	struct work_struct *w = &d->work;
	struct kc_wq_thread *t = w->wq;
	if (t) {
		pthread_mutex_lock(&t->m);
		if (w->pending) d->expires_ms = 0;
		pthread_cond_signal(&t->c);
		pthread_mutex_unlock(&t->m);
	}
	return flush_work(w);
}

void flush_workqueue(struct workqueue_struct *wq)
{
	struct kc_wq_thread *t = (struct kc_wq_thread *)wq;
	if (!t) return;
	for (int i = 0; i < 5000; i++) {
		pthread_mutex_lock(&t->m);
		bool busy = !list_empty(&t->queue) || t->running;
		pthread_mutex_unlock(&t->m);
		if (!busy) break;
		usleep(1000);
	}
}

struct workqueue_struct *create_singlethread_workqueue(const char *name)
{
	struct kc_wq_thread *t = calloc(1, sizeof(*t));
	kc_runtime_init();
	wq_start(t);
	return (struct workqueue_struct *)t;
}

struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...)
{
	return create_singlethread_workqueue(fmt);
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	struct kc_wq_thread *t = (struct kc_wq_thread *)wq;
	if (!t) return;
	pthread_mutex_lock(&t->m);
	t->stop = 1;
	pthread_cond_broadcast(&t->c);
	pthread_mutex_unlock(&t->m);
	pthread_join(t->thr, NULL);
	free(t);
}

/* ---------------- timers (one dedicated thread) ---------------- */
static struct list_head timer_list_head = LIST_HEAD_INIT(timer_list_head);
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
static pthread_once_t timer_once = PTHREAD_ONCE_INIT;
static struct timer_list *timer_running;

static void *timer_thread(void *arg)
{
	pthread_mutex_lock(&timer_lock);
	for (;;) {
		u64 now = kc_now_ms();
		struct timer_list *fire = NULL;
		struct list_head *pos;
		u64 next = 0;
		list_for_each(pos, &timer_list_head) {
			struct timer_list *t = container_of(pos, struct timer_list, entry);
			if (t->expires <= now) { fire = t; break; }
			if (!next || t->expires < next) next = t->expires;
		}
		if (fire) {
			list_del_init(&fire->entry);
			fire->armed = 0;
			timer_running = fire;
			pthread_mutex_unlock(&timer_lock);
			fire->function(fire);
			pthread_mutex_lock(&timer_lock);
			timer_running = NULL;
			pthread_cond_broadcast(&timer_cond);
			continue;
		}
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		u64 wait_ms = next ? (next > now ? next - now : 0) : 200;
		if (wait_ms > 200) wait_ms = 200;
		ts.tv_sec += wait_ms / 1000;
		ts.tv_nsec += (wait_ms % 1000) * 1000000L;
		if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
		pthread_cond_timedwait(&timer_cond, &timer_lock, &ts);
	}
	return NULL;
}

static void timer_start_thread(void)
{
	pthread_t t;
	pthread_create(&t, NULL, timer_thread, NULL);
}

int mod_timer(struct timer_list *t, unsigned long expires_ms)
{
	int was;
	kc_runtime_init();
	pthread_once(&timer_once, timer_start_thread);
	pthread_mutex_lock(&timer_lock);
	was = t->armed;
	if (was) list_del_init(&t->entry);
	t->expires = expires_ms;
	t->armed = 1;
	list_add_tail(&t->entry, &timer_list_head);
	pthread_cond_signal(&timer_cond);
	pthread_mutex_unlock(&timer_lock);
	return was;
}

int add_timer(struct timer_list *t) { return mod_timer(t, t->expires); }

int del_timer(struct timer_list *t)
{
	int was;
	pthread_mutex_lock(&timer_lock);
	was = t->armed;
	if (was) { list_del_init(&t->entry); t->armed = 0; }
	pthread_mutex_unlock(&timer_lock);
	return was;
}

int del_timer_sync(struct timer_list *t)
{
	int was = del_timer(t);
	pthread_mutex_lock(&timer_lock);
	while (timer_running == t)
		pthread_cond_wait(&timer_cond, &timer_lock);
	pthread_mutex_unlock(&timer_lock);
	return was;
}

/* ---------------- skb ---------------- */
struct sk_buff *alloc_skb(unsigned int size, gfp_t gfp)
{
	struct sk_buff *s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	size = (size + 63) & ~63u;
	s->head = malloc(size + sizeof(struct skb_shared_info) + 64);
	if (!s->head) { free(s); return NULL; }
	s->data = s->tail = s->head;
	s->end = s->head + size;
	s->truesize = size;
	memset(s->end, 0, sizeof(struct skb_shared_info));
	return s;
}

void kfree_skb(struct sk_buff *skb)
{
	if (!skb) return;
	free(skb->head);
	free(skb);
}

struct sk_buff *skb_copy(const struct sk_buff *s, gfp_t gfp)
{
	struct sk_buff *n = alloc_skb(s->end - s->head, gfp);
	if (!n) return NULL;
	skb_reserve(n, s->data - s->head);
	skb_put_data(n, s->data, s->len);
	memcpy(n->cb, s->cb, sizeof n->cb);
	n->protocol = s->protocol; n->priority = s->priority; n->queue_mapping = s->queue_mapping;
	return n;
}

struct sk_buff *skb_clone(struct sk_buff *s, gfp_t gfp) { return skb_copy(s, gfp); }

struct sk_buff *skb_copy_expand(const struct sk_buff *s, int newheadroom, int newtailroom, gfp_t gfp)
{
	struct sk_buff *n = alloc_skb(newheadroom + s->len + newtailroom, gfp);
	if (!n) return NULL;
	skb_reserve(n, newheadroom);
	skb_put_data(n, s->data, s->len);
	memcpy(n->cb, s->cb, sizeof n->cb);
	n->protocol = s->protocol; n->priority = s->priority; n->queue_mapping = s->queue_mapping;
	return n;
}

int pskb_expand_head(struct sk_buff *s, int nhead, int ntail, gfp_t gfp)
{
	unsigned int newsz = (s->end - s->head) + nhead + ntail;
	unsigned char *nh = malloc(newsz + sizeof(struct skb_shared_info) + 64);
	if (!nh) return -ENOMEM;
	memcpy(nh + nhead, s->head, s->end - s->head);
	s->data = nh + nhead + (s->data - s->head);
	s->tail = nh + nhead + (s->tail - s->head);
	s->end = nh + newsz;
	free(s->head);
	s->head = nh;
	return 0;
}

int skb_ensure_writable(struct sk_buff *s, int write_len) { return 0; }

/* ---------------- firmware ---------------- */
int request_firmware(const struct firmware **fw, const char *name, struct device *dev)
{
	const char *dir = getenv("RTW89_FW_DIR");
	char path[1024];
	char *base = strrchr(name, '/');
	base = base ? base + 1 : (char *)name;
	snprintf(path, sizeof path, "%s/%s", dir ? dir : ".", base);
	FILE *f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "firmware %s not found (%s)\n", name, path); *fw = NULL; return -ENOENT; }
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	struct firmware *fwp = malloc(sizeof(*fwp));
	u8 *data = malloc(len);
	if (fread(data, 1, len, f) != (size_t)len) { fclose(f); free(data); free(fwp); return -EIO; }
	fclose(f);
	fwp->size = len;
	fwp->data = data;
	*fw = fwp;
	fprintf(stderr, "firmware: loaded %s (%ld bytes)\n", path, len);
	return 0;
}

void release_firmware(const struct firmware *fw)
{
	if (!fw) return;
	free((void *)fw->data);
	free((void *)fw);
}

int kc_vsnprintf_pub(char *buf, size_t size, const char *fmt, va_list ap)
{
	return kc_vsnprintf(buf, size, fmt, ap);
}
