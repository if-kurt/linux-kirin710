/*
 * Kara Kutu G43: 4.9 include/linux/mtd/hisi_nve_interface.h'nin derleme kabugu.
 * Yapi ve sabitler 4.9 ile ayni (K49/include/linux/mtd/hisi_nve_interface.h).
 * hisi_nve_direct_access() ana hatta YOK; uygulamasi src/platform/kk_stub.c
 * (okuma: yalniz MACWLAN #193 icin modul parametresi, digerleri -ENODEV; yazma: -EPERM).
 */
#ifndef __HISI_NVE_INTERFACE_H__
#define __HISI_NVE_INTERFACE_H__

#include <linux/types.h>
#define NV_NAME_LENGTH 8     /*NV name maximum length*/
#define NVE_NV_DATA_SIZE 104 /*NV data maximum length*/

#define NV_WRITE 0 /*NV write operation*/
#define NV_READ 1  /*NV read  operation*/

struct hisi_nve_info_user {
	uint32_t nv_operation;
	uint32_t nv_number;
	char nv_name[NV_NAME_LENGTH];
	uint32_t valid_size;
	u_char nv_data[NVE_NV_DATA_SIZE];
};
extern int hisi_nve_direct_access(struct hisi_nve_info_user *user_info);

#endif
