// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/soc/qcom/smem.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/syscalls.h>

#include <soc/oplus/system/oplus_project.h>

#ifdef CONFIG_MTK_SECURITY_SW_SUPPORT
#include <sec_boot_lib.h>
#endif

#define SMEM_PROJECT    135

#define UINT2Ptr(n)        (uint32_t *)(n)
#define Ptr2UINT32(p)    (uint32_t)(p)

#define PROJECT_VERSION            (0x1)
#define PCB_VERSION                (0x2)
#define RF_INFO                    (0x3)
#define MODEM_TYPE                (0x4)
#define OPPO_BOOTMODE            (0x5)
#define SECURE_TYPE                (0x6)
#define SECURE_STAGE            (0x7)
#define OCP_NUMBER                (0x8)
#define SERIAL_NUMBER            (0x9)
#define ENG_VERSION                (0xA)
#define CONFIDENTIAL_STATUS        (0xB)
#define CDT_INTEGRITY            (0xC)
#define OPPO_FEATURE            (0xD)
#define OPERATOR_NAME            (0xE)
#define PROJECT_TEST            (0x1F)

static ProjectInfoOCDT *g_project = NULL;

static struct pcb_match pcb_str[] = {
    {.version=PRE_EVB1, .str="PRE_EVB1"},
    {.version=PRE_EVB2, .str="PRE_EVB2"},
    {.version=EVB1, .str="EVB1"},
    {.version=EVB2, .str="EVB2"},
    {.version=EVB3, .str="EVB3"},
    {.version=EVB4, .str="EVB4"},
    {.version=EVB5, .str="EVB5"},
    {.version=EVB6, .str="EVB6"},
    {.version=T0, .str="T0"},
    {.version=T1, .str="T1"},
    {.version=T2, .str="T2"},
    {.version=T3, .str="T3"},
    {.version=T4, .str="T4"},
    {.version=T5, .str="T5"},
    {.version=T6, .str="T6"},
    {.version=EVT1, .str="EVT1"},
    {.version=EVT2, .str="EVT2"},
    {.version=EVT3, .str="EVT3"},
    {.version=EVT4, .str="EVT4"},
    {.version=EVT5, .str="EVT5"},
    {.version=EVT6, .str="EVT6"},
    {.version=DVT1, .str="DVT1"},
    {.version=DVT2, .str="DVT2"},
    {.version=DVT3, .str="DVT3"},
    {.version=DVT4, .str="DVT4"},
    {.version=DVT5, .str="DVT5"},
    {.version=DVT6, .str="DVT6"},
    {.version=PVT1, .str="PVT1"},
    {.version=PVT2, .str="PVT2"},
    {.version=PVT3, .str="PVT3"},
    {.version=PVT4, .str="PVT4"},
    {.version=PVT5, .str="PVT5"},
    {.version=PVT6, .str="PVT6"},
    {.version=MP1, .str="MP1"},
    {.version=MP2, .str="MP2"},
    {.version=MP3, .str="MP3"},
    {.version=MP4, .str="MP4"},
    {.version=MP5, .str="MP5"},
    {.version=MP6, .str="MP6"},
};


struct proc_dir_entry *oppo_info = NULL;
struct proc_dir_entry *oppo_info_temp = NULL;

static void init_project_version(void)
{
    /*for qcom's smem*/
    size_t smem_size;
    void *smem_addr;
    char *PCB_version_name = NULL;
    uint16_t index = 0;

    if (g_project) {
        return;
	}
    /*get project info from smem*/
    else {
        smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
        SMEM_PROJECT,
        &smem_size);
        if (IS_ERR(smem_addr)) {
            pr_err("unable to acquire smem SMEM_PROJECT entry\n");
            return;
        }

        g_project = (ProjectInfoOCDT *)smem_addr;
        if (g_project == ERR_PTR(-EPROBE_DEFER)) {
            g_project = NULL;
            return;
        }

        do {
            if(pcb_str[index].version == g_project->nDataSCDT.PCB){
                PCB_version_name = pcb_str[index].str;
                break;
            }
            index++;
        }while(index < sizeof(pcb_str)/sizeof(struct pcb_match));

        pr_err("KE Project:%d, Audio:%d, nRF:%d, PCB:%s\n",
            g_project->nDataBCDT.ProjectNo,
            g_project->nDataBCDT.AudioIdx,
            g_project->nDataSCDT.RF,PCB_version_name);
        pr_err("OCP: %d 0x%x %c %d 0x%x %c\n",
            g_project->nDataSCDT.PmicOcp[0],
            g_project->nDataSCDT.PmicOcp[1],
            g_project->nDataSCDT.PmicOcp[2],
            g_project->nDataSCDT.PmicOcp[3],
            g_project->nDataSCDT.PmicOcp[4],
            g_project->nDataSCDT.PmicOcp[5]);
    }

    if(is_new_cdt()){
		if(oppo_info){
			remove_proc_entry("oppoVersion/operatorName", NULL);
			pr_err("remove proc operatorName\n");
			remove_proc_entry("oppoVersion/modemType", NULL);
			pr_err("remove proc modemType\n");
		}
        if(oppo_info_temp){
			remove_proc_entry("oplusVersion/operatorName", NULL);
			pr_err("remove proc operatorName\n");
			remove_proc_entry("oplusVersion/modemType", NULL);
			pr_err("remove proc modemType\n");
		}
	} else {
		if(oppo_info){
			remove_proc_entry("oppoVersion/RFType", NULL);
			pr_err("remove proc RFType\n");
		}
        if(oppo_info_temp){
			remove_proc_entry("oplusVersion/RFType", NULL);
			pr_err("remove proc RFType\n");
		}
	}

    pr_err("get_project:%d, is_new_cdt:%d, get_PCB_Version:%d, get_Oppo_Boot_Mode:%d, get_Modem_Version:%d\n", 
            get_project(),
            is_new_cdt(),
            get_PCB_Version(),
            get_Oppo_Boot_Mode(),
            get_Modem_Version());
    pr_err("get_Operator_Version:%d, get_dtsiNo:%d, get_audio_project:%d\n",
            get_Operator_Version(),
            get_dtsiNo(),
            get_audio());
    pr_err("oppo project info loading finished\n");

}

static bool cdt_integrity = false;
static int __init cdt_setup(char *str)
{
    if (str[0] == '1')
        cdt_integrity = true;

    return 1;
}
__setup("cdt_integrity=", cdt_setup);

unsigned int get_project(void)
{
    init_project_version();

    return g_project? g_project->nDataBCDT.ProjectNo : 0;
}
EXPORT_SYMBOL(get_project);

unsigned int is_project(int project)
{
    init_project_version();

    return ((get_project() == project)?1:0);
}
EXPORT_SYMBOL(is_project);

unsigned int is_new_cdt(void)/*Q and R is new*/
{
	init_project_version();
    if(get_cdt_version() == OCDT_VERSION_1_0)
        return 1;
    else
        return 0;
}

unsigned int get_PCB_Version(void)
{
    init_project_version();

    return g_project? g_project->nDataSCDT.PCB:-EINVAL;
}
EXPORT_SYMBOL(get_PCB_Version);

unsigned int get_Oppo_Boot_Mode(void)
{
    init_project_version();

    return g_project?g_project->nDataSCDT.OppoBootMode:0;
}
EXPORT_SYMBOL(get_Oppo_Boot_Mode);

int32_t get_Modem_Version(void)
{
    init_project_version();

    /*cdt return modem,ocdt return RF*/
    return g_project?g_project->nDataSCDT.RF:-EINVAL;
}
EXPORT_SYMBOL(get_Modem_Version);

int32_t get_Operator_Version(void)
{
    init_project_version();

    if(!is_new_cdt())
        return g_project?g_project->nDataSCDT.Operator:-EINVAL;
    else
        return -EINVAL;
}
EXPORT_SYMBOL(get_Operator_Version);

unsigned int get_dtsiNo(void)
{
    return (g_project && is_new_cdt()) ? g_project->nDataBCDT.DtsiNo : 0;
}
EXPORT_SYMBOL(get_dtsiNo);

unsigned int get_audio(void)
{
    return (g_project && is_new_cdt()) ? g_project->nDataBCDT.AudioIdx : 0;
}
EXPORT_SYMBOL(get_audio);

int rpmb_is_enable(void)
{
#define RPMB_KEY_PROVISIONED 0x00780178

    unsigned int rmpb_rd = 0;
    void __iomem *rpmb_addr = NULL;
    static unsigned int rpmb_enable = 0;

    if (rpmb_enable)
        return rpmb_enable;

    rpmb_addr = ioremap(RPMB_KEY_PROVISIONED , 4);    
    if (rpmb_addr) {
        rmpb_rd = __raw_readl(rpmb_addr);
        iounmap(rpmb_addr);
        rpmb_enable = (rmpb_rd >> 24) & 0x01;
    } else {
        rpmb_enable = 0;
    }

    return rpmb_enable;
}
EXPORT_SYMBOL(rpmb_is_enable);


unsigned int get_eng_version(void)
{
    init_project_version();

    return g_project?g_project->nDataECDT.Version:-EINVAL;
}
EXPORT_SYMBOL(get_eng_version);

extern char *saved_command_line;

bool oppo_daily_build(void)
{
    static int daily_build = -1;
    int eng_version = 0;

    if (daily_build != -1)
        return daily_build;

    if (strstr(saved_command_line, "buildvariant=userdebug") ||
        strstr(saved_command_line, "buildvariant=eng")) {
        daily_build = true;
    } else {
        daily_build = false;
    }

    eng_version = get_eng_version();
    if ((ALL_NET_CMCC_TEST == eng_version) || (ALL_NET_CMCC_FIELD == eng_version) ||
        (ALL_NET_CU_TEST == eng_version) || (ALL_NET_CU_FIELD == eng_version) ||
        (ALL_NET_CT_TEST == eng_version) || (ALL_NET_CT_FIELD == eng_version)){
        daily_build = false;
    }

    return daily_build;
}
EXPORT_SYMBOL(oppo_daily_build);

bool is_confidential(void)
{
    init_project_version();

    return g_project?g_project->nDataECDT.Is_confidential:-EINVAL;
}
EXPORT_SYMBOL(is_confidential);

uint32_t get_oppo_feature(enum F_INDEX index)
{
    if(is_new_cdt()){
        init_project_version();
        if (index < 1 || index > FEATURE_COUNT)
            return 0;
        return g_project?g_project->nDataBCDT.Feature[index-1]:0;
    }
    else
        return 0;
}
EXPORT_SYMBOL(get_oppo_feature);

#define SERIALNO_LEN 16
unsigned int get_serialID()
{
    unsigned int serial_id = 0xFFFFFFFF;

	char * ptr;
    char serialno[20] = {0};

    ptr = strstr(saved_command_line, "androidboot.serialno=");
    ptr += strlen("androidboot.serialno=");
    strncpy(serialno, ptr, SERIALNO_LEN);
    serialno[SERIALNO_LEN] = '\0';
    sscanf(serialno, "%x", &serial_id);
    return serial_id;
}
EXPORT_SYMBOL(get_serialID);

static void dump_ocp_info(struct seq_file *s)
{
    init_project_version();

    if (!g_project)
        return;

    seq_printf(s, "ocp: %d 0x%x %d 0x%x %c %c",
        g_project->nDataSCDT.PmicOcp[0],
        g_project->nDataSCDT.PmicOcp[1],
        g_project->nDataSCDT.PmicOcp[2],
        g_project->nDataSCDT.PmicOcp[3],
        g_project->nDataSCDT.PmicOcp[4],
        g_project->nDataSCDT.PmicOcp[5]);
}

static void dump_serial_info(struct seq_file *s)
{
    seq_printf(s, "0x%x", get_serialID());
}

static void dump_project_test(struct seq_file *s)
{
    return;
}

static void dump_oppo_feature(struct seq_file *s)
{
    init_project_version();

    if (!g_project)
        return;

    seq_printf(s, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        g_project->nDataBCDT.Feature[0],
        g_project->nDataBCDT.Feature[1],
        g_project->nDataBCDT.Feature[2],
        g_project->nDataBCDT.Feature[3],
        g_project->nDataBCDT.Feature[4],
        g_project->nDataBCDT.Feature[5],
        g_project->nDataBCDT.Feature[6],
        g_project->nDataBCDT.Feature[7],
        g_project->nDataBCDT.Feature[8],
        g_project->nDataBCDT.Feature[9]);
    return;
}

static void dump_eng_version(struct seq_file *s)
{
    seq_printf(s, "%d", get_eng_version());
    return;
}

static void dump_confidential_status(struct seq_file *s)
{
    seq_printf(s, "%d", is_confidential());
    return;
}

static void dump_secure_type(struct seq_file *s)
{
#define OEM_SEC_BOOT_REG 0x780350

    void __iomem *oem_config_base = NULL;
    uint32_t secure_oem_config = 0;

    oem_config_base = ioremap(OEM_SEC_BOOT_REG, 4);
    if (oem_config_base) {
        secure_oem_config = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
    }

    seq_printf(s, "%d", secure_oem_config);    
}

static void dump_secure_stage(struct seq_file *s)
{
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG 0x78019c

    void __iomem *oem_config_base = NULL;
    uint32_t secure_oem_config = 0;

    oem_config_base = ioremap(OEM_SEC_ENABLE_ANTIROLLBACK_REG, 4);
    if (oem_config_base) {
        secure_oem_config = __raw_readl(oem_config_base);
        iounmap(oem_config_base);
    }

    seq_printf(s, "%d", secure_oem_config);
}

static void update_manifest(struct proc_dir_entry *parent_1, struct proc_dir_entry *parent_2)
{
    static const char* manifest_src[2] = {
        "/vendor/odm/etc/vintf/manifest_ssss.xml",
        "/vendor/odm/etc/vintf/manifest_dsds.xml",
    };
    mm_segment_t fs;
    char * substr = strstr(boot_command_line, "simcardnum.doublesim=");

    if(!substr)
        return;

    substr += strlen("simcardnum.doublesim=");

    fs = get_fs();
    set_fs(KERNEL_DS);

    if (parent_1 && parent_2) {
        if (substr[0] == '0') {
            proc_symlink("manifest", parent_1, manifest_src[0]);//single sim
            proc_symlink("manifest", parent_2, manifest_src[0]);
        }
        else {
            proc_symlink("manifest", parent_1, manifest_src[1]);
            proc_symlink("manifest", parent_2, manifest_src[1]);
        }
    }

    set_fs(fs);
}

static void update_telephony_manifest(struct proc_dir_entry *parent_1, struct proc_dir_entry *parent_2)
{
    static const char* manifest_src[2] = {
        "/vendor/odm/etc/vintf/telephony_manifest_ssss.xml",
        "/vendor/odm/etc/vintf/telephony_manifest_dsds.xml",
    };
    mm_segment_t fs;
    char * substr = strstr(boot_command_line, "simcardnum.doublesim=");

    if(!substr)
        return;

    substr += strlen("simcardnum.doublesim=");

    fs = get_fs();
    set_fs(KERNEL_DS);

    if (parent_1 && parent_2) {
        if (substr[0] == '0') {
            proc_symlink("telephony_manifest", parent_1, manifest_src[0]);//single sim
            proc_symlink("telephony_manifest", parent_2, manifest_src[0]);
        }
        else {
            proc_symlink("telephony_manifest", parent_1, manifest_src[1]);
            proc_symlink("telephony_manifest", parent_2, manifest_src[1]);
        }
    }

    set_fs(fs);
}

static int project_read_func(struct seq_file *s, void *v)
{
    void *p = s->private;

    switch(Ptr2UINT32(p)) {
    case PROJECT_VERSION:
        //chenguanhua@BSP.bootloader.bootflow, 2020/09/30, modify for compatible of hexadecimal project model
        if (get_project() > 0x20000) {
            seq_printf(s, "%X", get_project());
        } else {
            seq_printf(s, "%d", get_project());
        }
        break;
    case PCB_VERSION:
        seq_printf(s, "%d", get_PCB_Version());
        break;
    case OPPO_BOOTMODE:
        seq_printf(s, "%d", get_Oppo_Boot_Mode());
        break;
    case MODEM_TYPE:
    case RF_INFO:
        seq_printf(s, "%d", get_Modem_Version());
        break;
    case SECURE_TYPE:
        dump_secure_type(s);
        break;
    case SECURE_STAGE:
        dump_secure_stage(s);
        break;
    case OCP_NUMBER:
        dump_ocp_info(s);
        break;
    case SERIAL_NUMBER:
        dump_serial_info(s);
        break;
    case ENG_VERSION:
        dump_eng_version(s);
        break;
    case CONFIDENTIAL_STATUS:
        dump_confidential_status(s);
        break;
    case PROJECT_TEST:
        dump_project_test(s);
        break;
    case CDT_INTEGRITY:
        seq_printf(s, "%d", cdt_integrity);
        break;
    case OPPO_FEATURE:
        dump_oppo_feature(s);
        break;
    case OPERATOR_NAME:
        seq_printf(s, "%d", get_Operator_Version());
        break;
    default:
        seq_printf(s, "not support\n");
        break;
    }

    return 0;
}

unsigned int get_cdt_version()
{
    init_project_version();

    return g_project?g_project->Version:0;
}

static int projects_open(struct inode *inode, struct file *file)
{
    return single_open(file, project_read_func, PDE_DATA(inode));
}

static const struct file_operations project_info_fops = {
    .owner = THIS_MODULE,
    .open  = projects_open,
    .read  = seq_read,
    .release = single_release,
};

static int __init oppo_project_init(void)
{
    struct proc_dir_entry *p_entry;

    oppo_info_temp = proc_mkdir("oplusVersion", NULL);
    oppo_info = proc_mkdir("oppoVersion", NULL);
    if (!oppo_info || !oppo_info_temp) {
        goto error_init;
    }

    p_entry = proc_create_data("prjName", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PROJECT_VERSION));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("pcbVersion", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PCB_VERSION));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("oplusBootmode", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(OPPO_BOOTMODE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("RFType", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(RF_INFO));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("modemType", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(MODEM_TYPE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("operatorName", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(OPERATOR_NAME));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("secureType", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(SECURE_TYPE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("secureStage", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(SECURE_STAGE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("ocp", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(OCP_NUMBER));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("serialID", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(SERIAL_NUMBER));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("engVersion", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(ENG_VERSION));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("isConfidential", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(CONFIDENTIAL_STATUS));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("cdt", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(CDT_INTEGRITY));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("feature", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(OPPO_FEATURE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("test", S_IRUGO, oppo_info, &project_info_fops, UINT2Ptr(PROJECT_TEST));
    if (!p_entry)
        goto error_init;

    /*update single or double cards*/
    //update_manifest(oppo_info);
    
    p_entry = proc_create_data("prjName", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(PROJECT_VERSION));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("pcbVersion", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(PCB_VERSION));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("oplusBootmode", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(OPPO_BOOTMODE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("RFType", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(RF_INFO));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("modemType", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(MODEM_TYPE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("operatorName", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(OPERATOR_NAME));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("secureType", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(SECURE_TYPE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("secureStage", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(SECURE_STAGE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("ocp", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(OCP_NUMBER));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("serialID", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(SERIAL_NUMBER));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("engVersion", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(ENG_VERSION));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("isConfidential", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(CONFIDENTIAL_STATUS));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("cdt", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(CDT_INTEGRITY));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("feature", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(OPPO_FEATURE));
    if (!p_entry)
        goto error_init;

    p_entry = proc_create_data("test", S_IRUGO, oppo_info_temp, &project_info_fops, UINT2Ptr(PROJECT_TEST));
    if (!p_entry)
        goto error_init;

    /*update single or double cards*/
    update_manifest(oppo_info, oppo_info_temp);
    update_telephony_manifest(oppo_info, oppo_info_temp);

    return 0;

error_init:
    remove_proc_entry("oppoVersion", NULL);
    remove_proc_entry("oplusVersion", NULL);
    return -ENOENT;
}

arch_initcall(oppo_project_init);

MODULE_DESCRIPTION("OPPO project version");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joshua <>");
