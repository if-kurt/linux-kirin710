// SPDX-License-Identifier: GPL-2.0
/*
 * Kara Kutu G54 (G-6): düşük pil koruması.
 *
 * Kademeler (TASARIM-G6.md):
 *   K1 cpufreq tavanı (little 1440 / big 1460 MHz = thermal "sert" trip'iyle aynı tavanlar),
 *   K2 yük kısma (en düşük OPP: 480 / 807 MHz; C1 ölçümü) + uevent (kullanıcı alanı bekçisi için),
 *   K3 kritik eylem (tek atımlık, mandallı): "gunluk" (yalnız log), "kurtarma" (kernel_restart(hedef)),
 *      "secmeli" (VBUS yok çıkarımında temiz poweroff, aksi hâlde kurtarma).
 * Ölçü: v_komp = vbat + ibat * R (R = 120 mΩ; + = deşarj) — OCV kestirimi; son 3 örneğin ortancası.
 *   ibat ortalaması son ort_ornek örnek (cihazda 6 × 10 sn = 60 sn).
 * Okuma: kk_sensor_pil() (coul V/I saf SPMI okuması, kk-sarj ile aynı; yeni okuma adresi YOK). QEMU: sahte pil.
 * Yazma: donanıma YOK. cpufreq tavanı freq_qos MAX isteği (çekirdek ayarı); oy yazmasını mevcut cpufreq sürücüsü
 *   (kk-kirin710-cpufreq, onaylı tür) yapar. K3'te reboot/poweroff çekirdeğin olağan yolu (reboot notifier'ları,
 *   kk-sarj kapanış kancası dahil).
 * sysfs (pil/): durum (RO); esik_k1_mv/esik_k2_mv/esik_k3_mv/taban_mv yalnız YÜKSELTİLEBİLİR (daha erken davranır);
 *   kritik_eylem yalnız "gunluk"a çekilebilir. Diğer her yazma -EPERM.
 * G55 CONFIG_KK_PIL_KORUMA_YALNIZ_LOG (Beyin: "yalnız log kipi, eylem yok"): kademeler hesaplanır ve yalnız loglanır.
 *   freq_qos tavanı, kademe uevent'i ve K3 eylemleri (ksys_sync, kernel_restart, orderly_poweroff, kernel_power_off)
 *   DERLENMEZ (#if); DT "karakutu,pil-kritik-eylem" yok sayılır (hep "gunluk"). Kanıt: kk-pil-koruma.o tanımsız
 *   sembollerinde bu işlevler yok (build.sh nesne denetimi).
 */
#include <linux/cpufreq.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/reboot.h>
#include <linux/sort.h>
#include <linux/syscalls.h>
#include <linux/workqueue.h>
#include "kk-sarj.h"
#include "kk-pil-koruma.h"

enum { K0, K1, K2, K3 };
static const char * const kk_sv_ad[] = { "K0", "K1", "K2", "K3" };
enum { E_GUNLUK, E_KURTARMA, E_SECMELI };
static const char * const kk_e_ad[] = { "gunluk", "kurtarma", "secmeli" };
#if IS_ENABLED(CONFIG_KK_PIL_KORUMA_YALNIZ_LOG)
#define KK_YALNIZ_LOG	1
#define KK_TAVAN_NOT	" UYGULANMADI (yalniz log)"
#else
#define KK_YALNIZ_LOG	0
#define KK_TAVAN_NOT	""
#endif

#define KK_HAL		16
#define KK_KUME		2	/* 0 = little, 1 = big */

struct kk_pk {
	struct device *dev;
	struct mutex kilit;
	struct delayed_work iz, yedek;
	/* ayarlar (DT; varsayılanlar cihaz değerleri) */
	u32 aralik_ms, acilis_sn, r_mohm, ardisik_in, ardisik_cik, ort_n, satir_sn, vbus_yok_ma;
	u32 e_k1, g_k1, e_k2, g_k2, e_k3, taban_mv;
	u32 cpu[KK_KUME], k1_khz[KK_KUME], k2_khz[KK_KUME];
	int eylem;
	const char *hedef;
	/* cpufreq */
	struct cpufreq_policy *pol[KK_KUME];
	struct freq_qos_request qos[KK_KUME];
	bool qos_var[KK_KUME], qos_uyari[KK_KUME];
	s32 tavan[KK_KUME];
	u32 n_tutmadi;
	/* durum */
	int sv;
	int say_in, say_cik, say_taban, say_hata;
	int vk_hal[3], vk_n;
	int ib_hal[KK_HAL], ib_n, ib_i;
	int vbat, ibat, vkomp, vk_med, ort, pil_mc;
	bool sic_gecerli, okundu;
	unsigned long bas, son_satir, sv_bas;
	u32 n_okuma, n_hata, n_gecis;
	bool bekleme_logu, sarj_logu, k3;
	const char *k3_neden, *k3_eylem;
};

/* ---------------- kaynak (cihaz: kk_sensor_pil; QEMU: sahte) ---------------- */

static DEFINE_MUTEX(kk_kaynak_kilit);
static kk_pil_kaynak_fn kk_kaynak_fn;
static void *kk_kaynak_ctx;
static const char *kk_kaynak_ad = "coul (kk_sensor_pil)";

static int kk_coul(void *ctx, int *v, int *i, int *t, bool *g)
{
	return kk_sensor_pil(v, i, t, g);
}

void kk_pil_koruma_kaynak(kk_pil_kaynak_fn fn, void *ctx, const char *ad)
{
	mutex_lock(&kk_kaynak_kilit);
	kk_kaynak_fn = fn;
	kk_kaynak_ctx = ctx;
	kk_kaynak_ad = ad;
	mutex_unlock(&kk_kaynak_kilit);
	pr_info("kk: pil: kaynak -> %s\n", ad);
}
EXPORT_SYMBOL_GPL(kk_pil_koruma_kaynak);

static int kk_oku(int *v, int *i, int *t, bool *g)
{
	int r;

	mutex_lock(&kk_kaynak_kilit);
	r = kk_kaynak_fn ? kk_kaynak_fn(kk_kaynak_ctx, v, i, t, g) : kk_coul(NULL, v, i, t, g);
	mutex_unlock(&kk_kaynak_kilit);
	return r;
}

/* ---------------- cpufreq tavanı (freq_qos MAX; donanım yazması değil) ---------------- */

static s32 kk_hedef_tavan(struct kk_pk *k, int c)
{
	if (k->sv >= K2)
		return k->k2_khz[c];
	if (k->sv == K1)
		return k->k1_khz[c];
	return FREQ_QOS_MAX_DEFAULT_VALUE;
}

static void kk_tavan_uygula(struct kk_pk *k)
{
#if !KK_YALNIZ_LOG
	int c, r;

	for (c = 0; c < KK_KUME; c++) {
		s32 h = kk_hedef_tavan(k, c);

		if (!k->qos_var[c]) {
			struct cpufreq_policy *p = cpufreq_cpu_get(k->cpu[c]);

			if (!p) {
				if (!k->qos_uyari[c] && h != FREQ_QOS_MAX_DEFAULT_VALUE) {
					k->qos_uyari[c] = true;
					dev_warn(k->dev, "kk: pil: cpu%u cpufreq politikasi yok; tavan %d kHz UYGULANAMADI (yeniden denenecek)\n",
						 k->cpu[c], h);
				}
				continue;
			}
			r = freq_qos_add_request(&p->constraints, &k->qos[c], FREQ_QOS_MAX, h);
			if (r < 0) {
				cpufreq_cpu_put(p);
				dev_err(k->dev, "kk: pil: cpu%u freq_qos istegi eklenemedi (%d)\n", k->cpu[c], r);
				continue;
			}
			k->pol[c] = p;
			k->qos_var[c] = true;
			k->tavan[c] = h;
			dev_info(k->dev, "kk: pil: cpu%u politikasi alindi (cpular %*pbl), tavan istegi %d kHz\n",
				 k->cpu[c], cpumask_pr_args(p->related_cpus), h);
			continue;
		}
		if (k->tavan[c] != h) {
			r = freq_qos_update_request(&k->qos[c], h);
			if (r < 0)
				dev_err(k->dev, "kk: pil: cpu%u tavan %d kHz guncellenemedi (%d)\n", k->cpu[c], h, r);
			else
				k->tavan[c] = h;
		}
	}
#endif
}

/* Bir sonraki örnekte politikanın tavana uyduğunu denetler (güncelleme eşzamansız: policy->update işi). */
static void kk_tavan_denetle(struct kk_pk *k)
{
#if !KK_YALNIZ_LOG
	int c;

	for (c = 0; c < KK_KUME; c++) {
		if (!k->qos_var[c] || k->tavan[c] == FREQ_QOS_MAX_DEFAULT_VALUE)
			continue;
		if (READ_ONCE(k->pol[c]->max) > (unsigned int)k->tavan[c]) {
			if (!k->n_tutmadi++)
				dev_warn(k->dev, "kk: pil: cpu%u tavan TUTMADI: policy max %u > %d kHz\n",
					 k->cpu[c], READ_ONCE(k->pol[c]->max), k->tavan[c]);
		}
	}
#endif
}

/* ---------------- durum makinesi ---------------- */

static int kk_int_kars(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

static int kk_ortanca(struct kk_pk *k)
{
	int t[3], n = min(k->vk_n, 3);

	memcpy(t, k->vk_hal, sizeof(t));
	sort(t, n, sizeof(int), kk_int_kars, NULL);
	return t[n / 2];
}

static int kk_ib_ort(struct kk_pk *k)
{
	int i, n = min_t(int, k->ib_n, k->ort_n);
	long s = 0;

	for (i = 0; i < n; i++)
		s += k->ib_hal[(k->ib_i - 1 - i + KK_HAL) % KK_HAL];
	return n ? (int)(s / n) : 0;
}

static unsigned int kk_gecen_sn(struct kk_pk *k)
{
	return jiffies_to_msecs(jiffies - k->bas) / 1000;
}

static void kk_uevent(struct kk_pk *k)
{
#if !KK_YALNIZ_LOG
	char sv[24];
	char *env[] = { sv, NULL };

	snprintf(sv, sizeof(sv), "KK_PIL_SEVIYE=%s", kk_sv_ad[k->sv]);
	kobject_uevent_env(&k->dev->kobj, KOBJ_CHANGE, env);
#endif
}

static void kk_gec(struct kk_pk *k, int yeni, const char *neden)
{
	int eski = k->sv;

	k->sv = yeni;
	k->sv_bas = jiffies;
	k->n_gecis++;
	k->say_in = k->say_cik = 0;
	kk_tavan_uygula(k);
	dev_warn(k->dev, "kk: pil: %s -> %s (%s; v_komp=%d ortanca=%d vbat=%d ibat=%d ort=%d; tavan little/big %d/%d kHz%s; t=%u sn)\n",
		 kk_sv_ad[eski], kk_sv_ad[yeni], neden, k->vkomp, k->vk_med, k->vbat, k->ibat, k->ort,
		 KK_YALNIZ_LOG ? kk_hedef_tavan(k, 0) : k->tavan[0], KK_YALNIZ_LOG ? kk_hedef_tavan(k, 1) : k->tavan[1],
		 KK_TAVAN_NOT, kk_gecen_sn(k));
	kk_uevent(k);
}

/* K3: mandallı, tek atımlık. Eylem kilit DIŞINDA yapılır (bkz. kk_iz). */
static const char *kk_k3_karar(struct kk_pk *k, const char *neden)
{
	bool vbus_yok = k->ort >= (int)k->vbus_yok_ma;
	const char *e;

	k->k3 = true;
	k->k3_neden = neden;
	if (KK_YALNIZ_LOG || k->eylem == E_GUNLUK)
		e = "gunluk";
	else if (k->eylem == E_SECMELI && vbus_yok)
		e = "poweroff";
	else
		e = "kurtarma";
	k->k3_eylem = e;
	kk_gec(k, K3, neden);
	pr_emerg("kk: pil: KRITIK: %s; v_komp=%d ortanca=%d vbat=%d ibat=%d ort=%d (VBUS %s [cikarim, ort >= %u mA]) -> %s%s%s\n",
		 neden, k->vkomp, k->vk_med, k->vbat, k->ibat, k->ort, vbus_yok ? "yok" : "var", k->vbus_yok_ma, e,
		 !strcmp(e, "kurtarma") ? " " : "", !strcmp(e, "kurtarma") ? k->hedef : "");
	return e;
}

static const char *kk_adim(struct kk_pk *k)
{
	int v, i, t, r, hedef;
	bool g, acilis_bitti;

	r = kk_oku(&v, &i, &t, &g);
	k->n_okuma++;
	if (r || v < 2500 || v > 4600 || i < -5000 || i > 5000) {
		k->n_hata++;
		if (++k->say_hata == 3)
			dev_err(k->dev, "kk: pil: kaynak 3 ardisik okunamadi/anlamsiz (r=%d vbat=%d ibat=%d); seviye %s korunur\n",
				r, v, i, kk_sv_ad[k->sv]);
		return NULL;
	}
	if (k->say_hata >= 3)
		dev_info(k->dev, "kk: pil: kaynak yeniden okunuyor (%d ardisik hatadan sonra)\n", k->say_hata);
	k->say_hata = 0;
	k->okundu = true;
	k->vbat = v;
	k->ibat = i;
	k->pil_mc = t;
	k->sic_gecerli = g;
	k->vkomp = v + i * (int)k->r_mohm / 1000;
	k->vk_hal[k->vk_n % 3] = k->vkomp;
	k->vk_n++;
	k->vk_med = kk_ortanca(k);
	k->ib_hal[k->ib_i] = i;
	k->ib_i = (k->ib_i + 1) % KK_HAL;
	k->ib_n++;
	k->ort = kk_ib_ort(k);
	kk_tavan_denetle(k);
	if (!KK_YALNIZ_LOG && (!k->qos_var[0] || !k->qos_var[1]))
		kk_tavan_uygula(k);

	if (k->k3)
		return NULL;
	acilis_bitti = kk_gecen_sn(k) >= k->acilis_sn;

	/* taban: ham vbat (bootloader açılış eşiği) — şarj durumundan bağımsız */
	if (v <= (int)k->taban_mv) {
		if (++k->say_taban >= 2) {
			if (acilis_bitti)
				return kk_k3_karar(k, "ham vbat taban esiginde");
			if (k->sv < K2)
				kk_gec(k, K2, "taban; acilis beklemesi suruyor");
		}
	} else {
		k->say_taban = 0;
	}

	/* kötüleşme */
	hedef = k->sv;
	if (k->vk_med <= (int)k->e_k3)
		hedef = K3;
	else if (k->vk_med <= (int)k->e_k2)
		hedef = max(hedef, K2);
	else if (k->vk_med <= (int)k->e_k1)
		hedef = max(hedef, K1);
	if (hedef == K3 && k->ort <= 0) {	/* net deşarj yok (şarj ya da VBUS boşta): K3 yok, K2'de kal; taban ayrı */
		if (!k->sarj_logu) {
			k->sarj_logu = true;
			dev_warn(k->dev, "kk: pil: K3 esiginin altinda ama net desarj yok (ort=%d mA <= 0; sarj/VBUS); K3 yok, K2\n", k->ort);
		}
		hedef = max(k->sv, K2);
	}
	if (hedef > k->sv) {
		if (++k->say_in >= (int)k->ardisik_in) {
			if (hedef < K3)
				kk_gec(k, hedef, "esik ardisik asildi");
			else if (acilis_bitti)
				return kk_k3_karar(k, "v_komp K3 esiginde, desarj suruyor");
			else {
				if (!k->bekleme_logu) {
					k->bekleme_logu = true;
					dev_warn(k->dev, "kk: pil: K3 kosulu; acilis beklemesi suruyor (%u/%u sn) -> simdilik K2\n",
						 kk_gecen_sn(k), k->acilis_sn);
				}
				if (k->sv < K2)
					kk_gec(k, K2, "K3 kosulu, acilis beklemesi");
			}
		}
		return NULL;
	}
	k->say_in = 0;

	/* iyileşme (histerezis; tek basamak) */
	if ((k->sv == K2 && k->vk_med >= (int)k->g_k2) || (k->sv == K1 && k->vk_med >= (int)k->g_k1)) {
		if (++k->say_cik >= (int)k->ardisik_cik)
			kk_gec(k, k->sv - 1, "geri esik ardisik asildi");
	} else {
		k->say_cik = 0;
	}
	return NULL;
}

static int kk_satir(struct kk_pk *k, char *b, size_t n)
{
	return scnprintf(b, n, "seviye=%s v_komp=%d ortanca=%d vbat=%d ibat=%d ort=%d pil=%d%s | esik K1 %u/%u K2 %u/%u K3 %u taban %u mV R %u mOhm | tavan little/big %d/%d kHz qos %d/%d tutmadi %u | okuma %u hata %u gecis %u | eylem %s hedef %s acilis %u/%u sn | kaynak %s%s%s\n",
			 kk_sv_ad[k->sv], k->vkomp, k->vk_med, k->vbat, k->ibat, k->ort, k->pil_mc,
			 k->sic_gecerli ? "" : "(gecersiz)", k->e_k1, k->g_k1, k->e_k2, k->g_k2, k->e_k3, k->taban_mv, k->r_mohm,
			 k->tavan[0], k->tavan[1], k->qos_var[0], k->qos_var[1], k->n_tutmadi, k->n_okuma, k->n_hata, k->n_gecis,
			 kk_e_ad[k->eylem], k->hedef, min(kk_gecen_sn(k), k->acilis_sn), k->acilis_sn, kk_kaynak_ad,
			 k->k3 ? " | K3: " : "", k->k3 ? k->k3_neden : "");
}

static void kk_yedek(struct work_struct *w)
{
#if !KK_YALNIZ_LOG
	struct kk_pk *k = container_of(to_delayed_work(w), struct kk_pk, yedek);

	pr_emerg("kk: pil: KRITIK: duzenli poweroff 120 sn'de bitmedi -> kernel_power_off\n");
	kernel_power_off();
	pr_emerg("kk: pil: KRITIK: kernel_power_off dondu -> kernel_restart(%s)\n", k->hedef);
	kernel_restart((char *)k->hedef);
#endif
}

static void kk_iz(struct work_struct *w)
{
	struct kk_pk *k = container_of(to_delayed_work(w), struct kk_pk, iz);
	const char *e;
	char *b;

	mutex_lock(&k->kilit);
	e = kk_adim(k);
	if (k->okundu && time_after_eq(jiffies, k->son_satir + k->satir_sn * HZ)) {
		k->son_satir = jiffies;
		b = kmalloc(512, GFP_KERNEL);
		if (b) {
			kk_satir(k, b, 512);
			dev_info(k->dev, "kk: pil: durum %s", b);
			kfree(b);
		}
	}
	mutex_unlock(&k->kilit);

#if !KK_YALNIZ_LOG
	if (e && !strcmp(e, "poweroff")) {
		schedule_delayed_work(&k->yedek, 120 * HZ);
		orderly_poweroff(true);
	} else if (e && !strcmp(e, "kurtarma")) {
		ksys_sync();	/* sigortadan farkı: acil değil, önce eşitle (E6 alanı/journald) */
		pr_emerg("kk: pil: KRITIK: sync bitti -> kernel_restart(%s)\n", k->hedef);
		kernel_restart((char *)k->hedef);
	}
#else
	(void)e;	/* G55 yalnız log: K3 kararı yalnız loglandı (kk_k3_karar), eylem derlenmedi */
#endif
	schedule_delayed_work(&k->iz, msecs_to_jiffies(k->aralik_ms));
}

/* ---------------- sysfs ---------------- */

static ssize_t durum_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct kk_pk *k = dev_get_drvdata(dev);
	ssize_t r;

	mutex_lock(&k->kilit);
	r = kk_satir(k, buf, PAGE_SIZE);
	mutex_unlock(&k->kilit);
	return r;
}
static DEVICE_ATTR_RO(durum);

/* eşik yalnız yükseltilir; geri eşiği aradaki farkı korur (en az) */
static ssize_t kk_esik_yaz(struct device *dev, u32 *e, u32 *gr, const char *ad, const char *buf, size_t n)
{
	struct kk_pk *k = dev_get_drvdata(dev);
	u32 v, eski;

	if (kstrtou32(buf, 0, &v))
		return -EINVAL;
	mutex_lock(&k->kilit);
	eski = *e;
	if (v < eski || v > 4300) {
		mutex_unlock(&k->kilit);
		dev_warn(dev, "kk: pil: %s esigi %u -> %u REDDEDILDI (yalniz yukseltilebilir, <= 4300)\n", ad, eski, v);
		return -EPERM;
	}
	if (gr)
		*gr += v - eski;	/* histerezis farkı korunur */
	*e = v;
	mutex_unlock(&k->kilit);
	dev_warn(dev, "kk: pil: %s esigi %u -> %u mV (test; yalniz yukseltilebilir)\n", ad, eski, v);
	return n;
}

#define KK_ESIK(ad, alan, geri)									\
static ssize_t ad##_show(struct device *dev, struct device_attribute *a, char *buf)		\
{ return sysfs_emit(buf, "%u\n", ((struct kk_pk *)dev_get_drvdata(dev))->alan); }		\
static ssize_t ad##_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n) \
{ struct kk_pk *k = dev_get_drvdata(dev); return kk_esik_yaz(dev, &k->alan, geri, #ad, buf, n); } \
static DEVICE_ATTR_RW(ad)

KK_ESIK(esik_k1_mv, e_k1, &k->g_k1);
KK_ESIK(esik_k2_mv, e_k2, &k->g_k2);
KK_ESIK(esik_k3_mv, e_k3, NULL);
KK_ESIK(taban_mv, taban_mv, NULL);	/* cihaz sınaması: K3 yolunu net deşarjdan bağımsız tetikler */

static ssize_t kritik_eylem_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%s\n", kk_e_ad[((struct kk_pk *)dev_get_drvdata(dev))->eylem]);
}

static ssize_t kritik_eylem_store(struct device *dev, struct device_attribute *a, const char *buf, size_t n)
{
	struct kk_pk *k = dev_get_drvdata(dev);

	if (!sysfs_streq(buf, "gunluk")) {
		dev_warn(dev, "kk: pil: kritik_eylem -> '%.*s' REDDEDILDI (yalniz gunluk)\n", (int)min_t(size_t, n, 16), buf);
		return -EPERM;
	}
	mutex_lock(&k->kilit);
	k->eylem = E_GUNLUK;
	mutex_unlock(&k->kilit);
	dev_warn(dev, "kk: pil: kritik_eylem -> gunluk (K3 yalniz log)\n");
	return n;
}
static DEVICE_ATTR_RW(kritik_eylem);

static struct attribute *kk_attrs[] = {
	&dev_attr_durum.attr, &dev_attr_esik_k1_mv.attr, &dev_attr_esik_k2_mv.attr, &dev_attr_esik_k3_mv.attr,
	&dev_attr_taban_mv.attr, &dev_attr_kritik_eylem.attr, NULL
};
static const struct attribute_group kk_grup = { .name = "pil", .attrs = kk_attrs };

/* ---------------- probe ---------------- */

static u32 kk_dt(struct device *dev, const char *p, u32 v)
{
	of_property_read_u32(dev->of_node, p, &v);
	return v;
}

static int kk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kk_pk *k;
	const char *s = NULL;
	int r, c;

	k = devm_kzalloc(dev, sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	k->dev = dev;
	mutex_init(&k->kilit);
	k->aralik_ms = clamp_t(u32, kk_dt(dev, "karakutu,pil-aralik-ms", 10000), 500, 60000);
	k->acilis_sn = kk_dt(dev, "karakutu,pil-acilis-bekle-sn", 300);
	k->r_mohm = clamp_t(u32, kk_dt(dev, "karakutu,pil-r-mohm", 120), 0, 500);
	k->ardisik_in = clamp_t(u32, kk_dt(dev, "karakutu,pil-ardisik-in", 3), 1, 10);
	k->ardisik_cik = clamp_t(u32, kk_dt(dev, "karakutu,pil-ardisik-cik", 6), 1, 30);
	k->ort_n = clamp_t(u32, kk_dt(dev, "karakutu,pil-ort-ornek", 6), 1, KK_HAL);
	k->satir_sn = clamp_t(u32, kk_dt(dev, "karakutu,pil-satir-sn", 600), 5, 3600);
	k->vbus_yok_ma = kk_dt(dev, "karakutu,pil-vbus-yok-ma", 80);
	/* desaysony_3240_110k_4400_25, 25 °C: %40 3801, %50 3844, %20 3726, %10 3666 (OCV); taban = bootloader açılış eşiği */
	k->e_k1 = kk_dt(dev, "karakutu,pil-k1-mv", 3801);
	k->g_k1 = kk_dt(dev, "karakutu,pil-k1-geri-mv", 3844);
	k->e_k2 = kk_dt(dev, "karakutu,pil-k2-mv", 3726);
	k->g_k2 = kk_dt(dev, "karakutu,pil-k2-geri-mv", 3801);
	k->e_k3 = kk_dt(dev, "karakutu,pil-k3-mv", 3666);
	k->taban_mv = kk_dt(dev, "karakutu,pil-taban-mv", 3450);
	if (!(k->e_k3 < k->e_k2 && k->e_k2 < k->e_k1 && k->g_k2 > k->e_k2 && k->g_k1 > k->e_k1 && k->g_k2 <= k->g_k1 &&
	      k->taban_mv < k->e_k3 && k->g_k1 <= 4300)) {
		dev_err(dev, "kk: pil: DT esikleri tutarsiz; surucu BASLAMADI\n");
		return -EINVAL;
	}
	k->cpu[0] = 0; k->cpu[1] = 4;
	k->k1_khz[0] = 1440000; k->k1_khz[1] = 1460000;
	k->k2_khz[0] = 480000; k->k2_khz[1] = 807000;
	of_property_read_u32_array(dev->of_node, "karakutu,pil-cpular", k->cpu, KK_KUME);
	of_property_read_u32_array(dev->of_node, "karakutu,pil-k1-khz", k->k1_khz, KK_KUME);
	of_property_read_u32_array(dev->of_node, "karakutu,pil-k2-khz", k->k2_khz, KK_KUME);
	k->eylem = E_GUNLUK;
	of_property_read_string(dev->of_node, "karakutu,pil-kritik-eylem", &s);
	if (s && !strcmp(s, "kurtarma"))
		k->eylem = E_KURTARMA;
	else if (s && !strcmp(s, "secmeli"))
		k->eylem = E_SECMELI;
	else if (s && strcmp(s, "gunluk"))
		dev_warn(dev, "kk: pil: bilinmeyen kritik eylem '%s' -> gunluk\n", s);
	if (KK_YALNIZ_LOG && k->eylem != E_GUNLUK) {
		dev_warn(dev, "kk: pil: DT kritik eylem '%s' yok sayildi -> gunluk (YALNIZ LOG derlemesi)\n", s);
		k->eylem = E_GUNLUK;
	}
	k->hedef = "bootloader";
	of_property_read_string(dev->of_node, "karakutu,pil-kurtarma-hedef", &k->hedef);
	if (strcmp(k->hedef, "bootloader") && strcmp(k->hedef, "erecovery")) {
		dev_warn(dev, "kk: pil: kurtarma hedefi '%s' izinli degil -> bootloader\n", k->hedef);
		k->hedef = "bootloader";
	}
	for (c = 0; c < KK_KUME; c++)
		k->tavan[c] = FREQ_QOS_MAX_DEFAULT_VALUE;
	k->sv = K0;
	k->bas = k->sv_bas = jiffies;
	k->son_satir = jiffies - k->satir_sn * HZ;
	platform_set_drvdata(pdev, k);
	r = devm_device_add_group(dev, &kk_grup);
	if (r)
		return r;
	INIT_DELAYED_WORK(&k->iz, kk_iz);
	INIT_DELAYED_WORK(&k->yedek, kk_yedek);
	dev_info(dev, "kk: pil: koruma basladi: kaynak %s; aralik %u ms, R %u mOhm, ortanca 3, ort %u ornek; K1 <=%u/>=%u (tavan %u/%u kHz), K2 <=%u/>=%u (tavan %u/%u kHz), K3 <=%u (ort > 0) | taban vbat <=%u; ardisik %u/%u; acilis bekleme %u sn; kritik eylem %s (kurtarma hedefi %s, VBUS yok cikarimi ort >= %u mA); donanim yazmasi YOK%s\n",
		 kk_kaynak_fn ? kk_kaynak_ad : "coul (kk_sensor_pil)", k->aralik_ms, k->r_mohm, k->ort_n,
		 k->e_k1, k->g_k1, k->k1_khz[0], k->k1_khz[1], k->e_k2, k->g_k2, k->k2_khz[0], k->k2_khz[1], k->e_k3,
		 k->taban_mv, k->ardisik_in, k->ardisik_cik, k->acilis_sn, kk_e_ad[k->eylem], k->hedef, k->vbus_yok_ma,
		 KK_YALNIZ_LOG ? "; YALNIZ LOG: tavan/uevent/K3 eylemi derlenmedi" : "");
	schedule_delayed_work(&k->iz, msecs_to_jiffies(k->aralik_ms));
	return 0;
}

static void kk_remove(struct platform_device *pdev)
{
	struct kk_pk *k = platform_get_drvdata(pdev);
	int c;

	cancel_delayed_work_sync(&k->iz);
	cancel_delayed_work_sync(&k->yedek);
#if !KK_YALNIZ_LOG
	for (c = 0; c < KK_KUME; c++)
		if (k->qos_var[c]) {
			freq_qos_remove_request(&k->qos[c]);
			cpufreq_cpu_put(k->pol[c]);
		}
#else
	(void)c;
#endif
}

static const struct of_device_id kk_of[] = {
	{ .compatible = "karakutu,pil-koruma" },
	{ }
};
MODULE_DEVICE_TABLE(of, kk_of);

static struct platform_driver kk_drv = {
	.probe = kk_probe,
	.remove = kk_remove,
	.driver = { .name = "kk-pil-koruma", .of_match_table = kk_of },
};
module_platform_driver(kk_drv);

MODULE_DESCRIPTION("Kara Kutu: dusuk pil korumasi (G-6; cpufreq tavani, yuk kisma, kritik eylem)");
MODULE_LICENSE("GPL");
