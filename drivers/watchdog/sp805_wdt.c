// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/char/watchdog/sp805-wdt.c
 *
 * Watchdog driver for ARM SP805 watchdog module
 *
 * Copyright (C) 2010 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2 or later. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/resource.h>
#include <linux/amba/bus.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/watchdog.h>

/* default timeout in seconds */
#define DEFAULT_TIMEOUT		60

#define MODULE_NAME		"sp805-wdt"

/* watchdog register offsets and masks */
#define WDTLOAD			0x000
	#define LOAD_MIN	0x00000001
	#define LOAD_MAX	0xFFFFFFFF
#define WDTVALUE		0x004
#define WDTCONTROL		0x008
	/* control register masks */
	#define	INT_ENABLE	(1 << 0)
	#define	RESET_ENABLE	(1 << 1)
	#define	ENABLE_MASK	(INT_ENABLE | RESET_ENABLE)
#define WDTINTCLR		0x00C
#define WDTRIS			0x010
#define WDTMIS			0x014
	#define INT_MASK	(1 << 0)
#define WDTLOCK			0xC00
	#define	UNLOCK		0x1ACCE551
	#define	LOCK		0x00000001

/**
 * struct sp805_wdt: sp805 wdt device structure
 * @wdd: instance of struct watchdog_device
 * @lock: spin lock protecting dev structure and io access
 * @base: base address of wdt
 * @clk: (optional) clock structure of wdt
 * @rate: (optional) clock rate when provided via properties
 * @adev: amba device structure of wdt
 * @status: current status of wdt
 * @load_val: load value to be set for current timeout
 */
struct sp805_wdt {
	struct watchdog_device		wdd;
	spinlock_t			lock;
	void __iomem			*base;
	struct clk			*clk;
	u64				rate;
	struct amba_device		*adev;
	unsigned int			load_val;
	bool				irq_is_reset;	/* Kara Kutu: Kirin */
	bool				kk_geri_oku;	/* G49 A2: sonraki yapılandırmada LOAD/CONTROL geri okunur */
	unsigned int			kk_red;		/* G49 A1: reddedilen yazma sayısı */
	unsigned int			kk_farkli;	/* G49 A2: tutmayan geri okuma sayısı */
};

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Set to 1 to keep watchdog running after device release");

/*
 * Kara Kutu G49 (TASARIM-WDT A1): bu sürücünün SP805'e yaptığı HER yazma kk_wdt_yaz()'dan geçer.
 *  - Ofset derleme zamanı sabiti olmalı ve beyaz listede: {LOAD 0x000, CONTROL 0x008, INTCLR 0x00C, LOCK 0xC00}
 *    (BUILD_BUG_ON; 0x004 VALUE, 0x010 RIS, 0x014 MIS, Hisi 0x018 WDLOAD_B ve başka her ofset DERLENMEZ).
 *  - Değer kısıtı (kk_wdt_deger_izinli, saf işlev; birim testi aynen derler):
 *      LOCK    ∈ {0x1ACCE551, 0x1};   INTCLR = 0x1;
 *      CONTROL = 0x3, ya da 0 YALNIZ restart yolunda (hemen ardından 0x3);
 *      LOAD    ∈ [rate/2 - 1, rate*600 - 1] (1..600 sn; iki aşamalı kipte rate/2 tabanı), ya da 0 YALNIZ restart yolunda.
 *  - Kısıta uymayan değer YAZILMAZ; red sayılır ve bir kez loglanır. wdt_setload 600 sn üstünü zaten reddeder
 *    (çekirdek zamanlaması ile donanım yükü ayrışmasın diye), yani kısıt çalışan sistemde tetiklenmez.
 * Bu kapı yeni bir yazma eklemez: G24/G25'ten beri yapılan W1-W4 yazmalarının aynısı, aynı sırayla.
 */
#define KK_WDT_SANIYE_AZAMI	600u
/* KK-G49-WDT-BIRIM-BASLA: bu işaretler arası saf C; birim testi (work/g49/birlesik/birim-wdt) aynen derler */
static bool kk_wdt_ofs_izinli(unsigned int ofs)
{
	return ofs == WDTLOAD || ofs == WDTCONTROL || ofs == WDTINTCLR || ofs == WDTLOCK;
}

static bool kk_wdt_deger_izinli(unsigned int ofs, u32 val, u64 rate, bool restart)
{
	switch (ofs) {
	case WDTLOCK:
		return val == UNLOCK || val == LOCK;
	case WDTINTCLR:
		return val == INT_MASK;
	case WDTCONTROL:
		return val == (INT_ENABLE | RESET_ENABLE) || (restart && val == 0);
	case WDTLOAD:
		if (restart && val == 0)
			return true;
		return rate >= 2 && (u64)val + 1 >= div_u64(rate, 2) &&
		       (u64)val + 1 <= rate * KK_WDT_SANIYE_AZAMI;
	default:
		return false;
	}
}

/* G49 A4: kalan süre (saniye). irq-is-reset (Kirin tek aşama): ilk dolum = reset, kalan = VALUE.
 * İki aşamalı kip: kesme etkin değilse VALUE + LOAD + 1 (ana hat davranışı aynen). */
static u64 kk_wdt_kalan(u32 value, bool ris, u32 load_val, bool irq_is_reset)
{
	u64 load = value;

	if (!irq_is_reset && !ris)
		load += (u64)load_val + 1;
	return load;
}
/* KK-G49-WDT-BIRIM-SON */

static noinline void __kk_wdt_yaz(struct sp805_wdt *wdt, unsigned int ofs, u32 val, bool restart)
{
	if (!kk_wdt_ofs_izinli(ofs) || !kk_wdt_deger_izinli(ofs, val, wdt->rate, restart)) {
		wdt->kk_red++;
		pr_warn_once("sp805-wdt: kk: wdt yazma REDDEDILDI ofs=0x%03x deger=0x%08x restart=%d (beyaz liste; yazilmadi)\n",
			     ofs, val, restart);
		return;
	}
	writel_relaxed(val, wdt->base + ofs);
}

#define kk_wdt_yaz(wdt, ofs, val, restart)					\
	do {									\
		BUILD_BUG_ON(!__builtin_constant_p(ofs));			\
		BUILD_BUG_ON((ofs) != WDTLOAD && (ofs) != WDTCONTROL &&		\
			     (ofs) != WDTINTCLR && (ofs) != WDTLOCK);		\
		__kk_wdt_yaz(wdt, ofs, val, restart);				\
	} while (0)

/* returns true if wdt is running; otherwise returns false */
static bool wdt_is_running(struct watchdog_device *wdd)
{
	struct sp805_wdt *wdt = watchdog_get_drvdata(wdd);
	u32 wdtcontrol = readl_relaxed(wdt->base + WDTCONTROL);

	return (wdtcontrol & ENABLE_MASK) == ENABLE_MASK;
}

/* This routine finds load value that will reset system in required timeout */
static int wdt_setload(struct watchdog_device *wdd, unsigned int timeout)
{
	struct sp805_wdt *wdt = watchdog_get_drvdata(wdd);
	u64 load, rate;

	rate = wdt->rate;

	/*
	 * sp805 runs counter with given value twice, after the end of first
	 * counter it gives an interrupt and then starts counter again. If
	 * interrupt already occurred then it resets the system. This is why
	 * load is half of what should be required.
	 */
	/*
	 * Kara Kutu (Kirin 710): the FIRST expiry (interrupt) is taken by BL31/LPM3
	 * and resets the SoC at once (reboot reason AP_S_AWDT, subtype
	 * HI_APWDT_BL31LPM3; Görev 24). Use a single stage: interrupt at `timeout`,
	 * and let the watchdog core ping every timeout/4 (max_hw_heartbeat_ms below).
	 */
	/* G49 A1: 1..600 sn dışı reddedilir (değişiklik yok; systemd 60 s / 120 s, init varsayılan 60 s) */
	if (timeout < 1 || timeout > KK_WDT_SANIYE_AZAMI)
		return -EINVAL;

	if (wdt->irq_is_reset)
		load = rate * timeout - 1;
	else
		load = div_u64(rate, 2) * timeout - 1;

	load = (load > LOAD_MAX) ? LOAD_MAX : load;
	load = (load < LOAD_MIN) ? LOAD_MIN : load;

	spin_lock(&wdt->lock);
	wdt->load_val = load;
	wdt->kk_geri_oku = true;	/* G49 A2: bir sonraki yapılandırmada LOAD/CONTROL geri okunur */
	/* roundup timeout to closest positive integer value */
	if (wdt->irq_is_reset) {
		wdd->timeout = div_u64(load + 1 + (rate / 2), rate);
		/* core keepalive = min(timeout, max_hw_heartbeat)/2 = timeout/4 */
		wdd->max_hw_heartbeat_ms = wdd->timeout * 1000 / 2;
	} else {
		wdd->timeout = div_u64((load + 1) * 2 + (rate / 2), rate);
	}
	spin_unlock(&wdt->lock);

	return 0;
}

/* returns number of seconds left for reset to occur */
static unsigned int wdt_timeleft(struct watchdog_device *wdd)
{
	struct sp805_wdt *wdt = watchdog_get_drvdata(wdd);
	u64 load;
	u32 value;
	bool ris;

	spin_lock(&wdt->lock);
	value = readl_relaxed(wdt->base + WDTVALUE);

	/*
	 * If the interrupt is inactive then time left is WDTValue + WDTLoad.
	 * G49 A4: irq-is-reset kipinde (Kirin) ilk dolum reset'tir; kalan yalnız VALUE (RIS okunmaz).
	 */
	ris = wdt->irq_is_reset ? false : !!(readl_relaxed(wdt->base + WDTRIS) & INT_MASK);
	load = kk_wdt_kalan(value, ris, wdt->load_val, wdt->irq_is_reset);
	spin_unlock(&wdt->lock);

	return div_u64(load, wdt->rate);
}

static int
wdt_restart(struct watchdog_device *wdd, unsigned long mode, void *cmd)
{
	struct sp805_wdt *wdt = watchdog_get_drvdata(wdd);

	kk_wdt_yaz(wdt, WDTLOCK, UNLOCK, true);
	kk_wdt_yaz(wdt, WDTCONTROL, 0, true);
	kk_wdt_yaz(wdt, WDTLOAD, 0, true);
	kk_wdt_yaz(wdt, WDTCONTROL, INT_ENABLE | RESET_ENABLE, true);

	/* Flush posted writes. */
	readl_relaxed(wdt->base + WDTLOCK);

	return 0;
}

static int wdt_config(struct watchdog_device *wdd, bool ping)
{
	struct sp805_wdt *wdt = watchdog_get_drvdata(wdd);
	int ret;

	if (!ping) {

		ret = clk_prepare_enable(wdt->clk);
		if (ret) {
			dev_err(&wdt->adev->dev, "clock enable fail");
			return ret;
		}
	}

	spin_lock(&wdt->lock);

	kk_wdt_yaz(wdt, WDTLOCK, UNLOCK, false);
	kk_wdt_yaz(wdt, WDTLOAD, wdt->load_val, false);
	kk_wdt_yaz(wdt, WDTINTCLR, INT_MASK, false);

	if (!ping)
		kk_wdt_yaz(wdt, WDTCONTROL, INT_ENABLE | RESET_ENABLE, false);

	kk_wdt_yaz(wdt, WDTLOCK, LOCK, false);

	/* Flush posted writes. */
	readl_relaxed(wdt->base + WDTLOCK);

	/* G49 A2: açılışta (enable) ve her set_timeout'tan sonraki ilk yapılandırmada LOAD ve CONTROL geri okunur
	 * (yalnız okuma; tutmazsa yalnız log, davranış değişmez). Her beslemede okunmaz. */
	if (!ping || wdt->kk_geri_oku) {
		u32 l = readl_relaxed(wdt->base + WDTLOAD);
		u32 c = readl_relaxed(wdt->base + WDTCONTROL);

		wdt->kk_geri_oku = false;
		if (l != wdt->load_val || (c & ENABLE_MASK) != ENABLE_MASK) {
			wdt->kk_farkli++;
			pr_warn("sp805-wdt: kk: wdt geri okuma FARKLI (#%u): LOAD=0x%08x beklenen 0x%08x CONTROL=0x%x beklenen 0x3\n",
				wdt->kk_farkli, l, wdt->load_val, c);
		} else if (!ping) {
			pr_info("sp805-wdt: kk: wdt geri okuma TUTTU: LOAD=0x%08x CONTROL=0x%x\n", l, c);
		} else {
			pr_info("sp805-wdt: kk: wdt geri okuma (set_timeout) TUTTU: LOAD=0x%08x CONTROL=0x%x\n", l, c);
		}
	}
	spin_unlock(&wdt->lock);

	return 0;
}

static int wdt_ping(struct watchdog_device *wdd)
{
	return wdt_config(wdd, true);
}

/* enables watchdog timers reset */
static int wdt_enable(struct watchdog_device *wdd)
{
	return wdt_config(wdd, false);
}

/* disables watchdog timers reset */
static int wdt_disable(struct watchdog_device *wdd)
{
	struct sp805_wdt *wdt = watchdog_get_drvdata(wdd);

	/*
	 * G49 A1: CONTROL <- 0 yalnız restart yolunda izinli (kk_wdt_deger_izinli). Durdurma yolu donanıma YAZMAZ,
	 * saati kapatmaz ve -EPERM döner. Cihazda bu yola zaten ulaşılamaz (NOWAYOUT, gömülü sürücü, suspend yok,
	 * irq-is-reset kipinde stop_on_reboot yok); ana hattın eski davranışı UNLOCK, CONTROL <- 0, LOCK idi.
	 */
	wdt->kk_red++;
	pr_warn_once("sp805-wdt: kk: wdt durdurma REDDEDILDI (CONTROL <- 0 yalniz restart yolunda; yazilmadi)\n");
	return -EPERM;
}

static const struct watchdog_info wdt_info = {
	.options = WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = MODULE_NAME,
};

static const struct watchdog_ops wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= wdt_enable,
	.stop		= wdt_disable,
	.ping		= wdt_ping,
	.set_timeout	= wdt_setload,
	.get_timeleft	= wdt_timeleft,
	.restart	= wdt_restart,
};

static int
sp805_wdt_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct sp805_wdt *wdt;
	struct reset_control *rst;
	u64 rate = 0;
	int ret = 0;

	wdt = devm_kzalloc(&adev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt) {
		ret = -ENOMEM;
		goto err;
	}

	wdt->base = devm_ioremap_resource(&adev->dev, &adev->res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	/*
	 * When driver probe with ACPI device, clock devices
	 * are not available, so watchdog rate get from
	 * clock-frequency property given in _DSD object.
	 */
	device_property_read_u64(&adev->dev, "clock-frequency", &rate);

	wdt->clk = devm_clk_get_optional(&adev->dev, NULL);
	if (IS_ERR(wdt->clk))
		return dev_err_probe(&adev->dev, PTR_ERR(wdt->clk), "Clock not found\n");

	wdt->rate = clk_get_rate(wdt->clk);
	if (!wdt->rate)
		wdt->rate = rate;
	if (!wdt->rate) {
		dev_err(&adev->dev, "no clock-frequency property\n");
		return -ENODEV;
	}

	rst = devm_reset_control_get_optional_exclusive(&adev->dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(&adev->dev, PTR_ERR(rst), "Can not get reset\n");

	reset_control_deassert(rst);

	wdt->adev = adev;
	wdt->wdd.info = &wdt_info;
	wdt->wdd.ops = &wdt_ops;
	wdt->wdd.parent = &adev->dev;

	spin_lock_init(&wdt->lock);
	watchdog_set_nowayout(&wdt->wdd, nowayout);
	watchdog_set_drvdata(&wdt->wdd, wdt);
	watchdog_set_restart_priority(&wdt->wdd, 128);
	watchdog_stop_on_unregister(&wdt->wdd);

	/*
	 * If 'timeout-sec' devicetree property is specified, use that.
	 * Otherwise, use DEFAULT_TIMEOUT
	 */
	wdt->irq_is_reset = device_property_read_bool(&adev->dev,
						      "karakutu,irq-is-reset");
	wdt->wdd.timeout = DEFAULT_TIMEOUT;
	watchdog_init_timeout(&wdt->wdd, 0, &adev->dev);
	wdt_setload(&wdt->wdd, wdt->wdd.timeout);

	/*
	 * G49 A3: bootloader'ın bıraktığı durum, wdt_enable'dan ÖNCE, yalnız okuma (yan etkisiz durum yazmaçları;
	 * WDLOAD_B 0x018 okunmaz). Beklenen (RECON G24): LOAD 0x1e0000 (60 << 15), CONTROL 3.
	 */
	dev_info(&adev->dev,
		 "kk: wdt acilis: LOAD=0x%08x VALUE=0x%08x CONTROL=0x%x RIS=%u MIS=%u LOCK=%u rate=%llu (beklenen LOAD 0x001e0000 CONTROL 0x3)\n",
		 readl_relaxed(wdt->base + WDTLOAD), readl_relaxed(wdt->base + WDTVALUE),
		 readl_relaxed(wdt->base + WDTCONTROL), readl_relaxed(wdt->base + WDTRIS) & INT_MASK,
		 readl_relaxed(wdt->base + WDTMIS) & INT_MASK, readl_relaxed(wdt->base + WDTLOCK),
		 (unsigned long long)wdt->rate);

	/*
	 * If HW is already running, enable/reset the wdt and set the running
	 * bit to tell the wdt subsystem
	 */
	if (wdt_is_running(&wdt->wdd)) {
		wdt_enable(&wdt->wdd);
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}

	/*
	 * Kirin: keep the watchdog running across reboot, so that a failed
	 * restart (LPM3 NMI and PSCI both not resetting) still ends in a reset.
	 */
	if (!wdt->irq_is_reset)
		watchdog_stop_on_reboot(&wdt->wdd);
	ret = watchdog_register_device(&wdt->wdd);
	if (!ret && wdt->irq_is_reset)
		dev_info(&adev->dev, "kk: irq-is-reset: tek asama, timeout %us, besleme %ums\n",
			 wdt->wdd.timeout, wdt->wdd.max_hw_heartbeat_ms / 2);
	if (ret)
		goto err;
	amba_set_drvdata(adev, wdt);

	dev_info(&adev->dev, "registration successful\n");
	return 0;

err:
	dev_err(&adev->dev, "Probe Failed!!!\n");
	return ret;
}

static void sp805_wdt_remove(struct amba_device *adev)
{
	struct sp805_wdt *wdt = amba_get_drvdata(adev);

	watchdog_unregister_device(&wdt->wdd);
	watchdog_set_drvdata(&wdt->wdd, NULL);
}

static int __maybe_unused sp805_wdt_suspend(struct device *dev)
{
	struct sp805_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		return wdt_disable(&wdt->wdd);

	return 0;
}

static int __maybe_unused sp805_wdt_resume(struct device *dev)
{
	struct sp805_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		return wdt_enable(&wdt->wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(sp805_wdt_dev_pm_ops, sp805_wdt_suspend,
		sp805_wdt_resume);

static const struct amba_id sp805_wdt_ids[] = {
	{
		.id	= 0x00141805,
		.mask	= 0x00ffffff,
	},
	{
		.id     = 0x001bb824,
		.mask   = 0x00ffffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, sp805_wdt_ids);

static struct amba_driver sp805_wdt_driver = {
	.drv = {
		.name	= MODULE_NAME,
		.pm	= &sp805_wdt_dev_pm_ops,
	},
	.id_table	= sp805_wdt_ids,
	.probe		= sp805_wdt_probe,
	.remove = sp805_wdt_remove,
};

module_amba_driver(sp805_wdt_driver);

MODULE_AUTHOR("Viresh Kumar <vireshk@kernel.org>");
MODULE_DESCRIPTION("ARM SP805 Watchdog Driver");
MODULE_LICENSE("GPL");
