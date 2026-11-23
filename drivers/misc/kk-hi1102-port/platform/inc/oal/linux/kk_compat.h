/*
 * kk_compat.h - Kara Kutu G42: Hi1102 OAL/FRW ortak basliklari icin 4.9 -> 6.12 uyumluluk katmani.
 *
 * Yalniz ORTAK BASLIKLARDAKI (inc/oal/linux/arch/ altindakiler, inc/oal/oal_schedule.h, frw_event_main.h)
 * derleme hatalarini kapatir. .c dosyalarina dokunulmaz (dilim 2 kurali).
 * Her girdi 6.12'deki disa acik karsiligina gider; anlam degisikligi varsa yaninda yazar.
 */
#ifndef __KK_COMPAT_H__
#define __KK_COMPAT_H__

#include <linux/version.h>
#include <linux/types.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/timer.h>
#include <linux/sched/clock.h>	/* local_clock(): cpu_clock/local_clock bildirimi */
#include <linux/etherdevice.h>	/* eth_random_addr() */
#include <linux/pm_wakeup.h>
#include <linux/string.h>
#include <linux/slab.h>

/* ---- Zaman: struct timeval / do_gettimeofday (5.6'da kaldirildi) ----
 * Yalniz cekirdek ici zaman damgasi (DFT izleri, ms damgasi); ABI degil.
 * Alan adlari (tv_sec/tv_usec) korunur, saniye 64 bit (y2038 guvenli). */
struct kk_timeval {
	time64_t tv_sec;
	long tv_usec;
};

static inline void kk_do_gettimeofday(struct kk_timeval *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}

/* ---- Timer: init_timer + .data kaldirildi (4.15) -> timer_setup + from_timer ----
 * OAL API'si (oal_timer_init(t, gecikme, fonk, arg)) aynen kalir; arg sarmalayicida tutulur,
 * fonk(arg) 6.12 geri cagirimindan (struct timer_list *) cagrilir.
 * oal_timer_list_stru'ya yalniz oal_timer_* islevleriyle erisiliyor (frw_timer.c, oal_hcc_host.c). */
struct kk_oal_timer {
	struct timer_list timer;
	void (*func)(unsigned long);
	unsigned long data;
};

static inline void kk_oal_timer_tramp(struct timer_list *t)
{
	struct kk_oal_timer *ot = from_timer(ot, t, timer);

	if (ot->func)
		ot->func(ot->data);
}

/* ---- wakeup_source_init/trash (5.4-5.7'de kaldirildi) ----
 * oal_wakelock_stru icinde gomulu struct wakeup_source korunur (oal_sdio_host.c .name okuyor).
 * 4.9 wakeup_source_init = prepare(ad kstrdup) + add. 6.12'de disa acik wakeup_source_add/remove
 * gomulu kaynagi destekler (wakeup.c:167-207); sysfs girdisi acilmaz (yalniz _register yapar). */
static inline void kk_wakeup_source_init(struct wakeup_source *ws, const char *name)
{
	memset(ws, 0, sizeof(*ws));
	ws->name = kstrdup_const(name, GFP_KERNEL);
	wakeup_source_add(ws);
}

static inline void kk_wakeup_source_trash(struct wakeup_source *ws)
{
	__pm_relax(ws);
	wakeup_source_remove(ws);
	kfree_const(ws->name);
	ws->name = NULL;
}

/* ---- set_fs/get_fs/KERNEL_DS (arm64'te 5.11'de kaldirildi) ----
 * Derleme kabugu: hicbir sey yapmaz. Tutulan .c'lerde bu yolu kullanan yer yok
 * (oal_get_fs/oal_set_fs yalniz atilan dmac_reset.c'de); dogrudan get_fs/set_fs
 * kullanan .c'ler (plat_firmware.c) kernel_read()/request_firmware'e gecmeli. */
typedef unsigned long kk_mm_segment_t;
#define KK_KERNEL_DS		((kk_mm_segment_t)0)

/* ---- sched_setscheduler (5.9'da export'u kaldirildi) ----  [G43]
 * 4.9 politika ve onceligi AYNEN korunur: sched_setattr_nocheck (EXPORT_SYMBOL_GPL,
 * kernel/sched/syscalls.c:834). RT olmayan politikada nice korunur (4.9 _sched_setscheduler
 * gibi). Alternatif sched_set_fifo() oncelik 50'ye sabitler; secilmedi. */
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
static inline int kk_sched_setscheduler(struct task_struct *p, int policy,
					const struct sched_param *param)
{
	struct sched_attr attr = {
		.size		= sizeof(attr),
		.sched_policy	= policy,
		.sched_priority	= param->sched_priority,
		.sched_nice	= (policy == SCHED_FIFO || policy == SCHED_RR) ? 0 : task_nice(p),
	};

	return sched_setattr_nocheck(p, &attr);
}

#endif /* __KK_COMPAT_H__ */
