#ifndef _DOWNLOAD_OIS_FW_H_
#define _DOWNLOAD_OIS_FW_H_

#include <linux/module.h>
#include <linux/firmware.h>
#include <cam_sensor_cmn_header.h>
#include "cam_ois_dev.h"
#include "cam_ois_core.h"
#include "cam_ois_soc.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "cam_res_mgr_api.h"
#include "cam_common_util.h"

#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>

//int RamWrite32A(uint32_t addr, uint32_t data);
//int RamRead32A(uint32_t addr, uint32_t* data);
int DownloadFW(struct cam_ois_ctrl_t *o_ctrl);
void OISControl(struct cam_ois_ctrl_t *o_ctrl, void *arg);
void ReadOISHALLData(struct cam_ois_ctrl_t *o_ctrl, void *data);
bool IsOISReady(struct cam_ois_ctrl_t *o_ctrl);
void InitOIS(struct cam_ois_ctrl_t *o_ctrl);
void DeinitOIS(struct cam_ois_ctrl_t *o_ctrl);
void InitOISResource(struct cam_ois_ctrl_t *o_ctrl);
void CheckOISdata(void);
void CheckOISfwVersion(void);
int OISRamWriteWord(struct cam_ois_ctrl_t *ois_ctrl, uint32_t addr, uint32_t data);
int OISRead(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t* data);
void forceExitpoll(struct cam_ois_ctrl_t *o_ctrl);
void Sem1215sReadOISHALLData(struct cam_ois_ctrl_t *o_ctrl, void *data);
void Sem1215sReadOISHALLDataV2(struct cam_ois_ctrl_t *o_ctrl, void *data);

void set_ois_thread_status(int main_thread_status ,int tele_thread_status);
#endif
/* _DOWNLOAD_OIS_FW_H_ */

