# SPDX-License-Identifier: GPL-2.0
# UFSHCD makefile
obj-$(CONFIG_SCSI_UFS_DWC_TC_PCI) += tc-dwc-g210-pci.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_DWC_TC_PLATFORM) += tc-dwc-g210-pltfrm.o ufshcd-dwc.o tc-dwc-g210.o
obj-$(CONFIG_SCSI_UFS_QCOM) += ufs-qcom.o
obj-$(CONFIG_SCSI_UFSHCD) += ufshcd.o
#ifdef VENDOR_EDIT
#/* Hank.liu@TECH.PLAT.Storage, 2019-10-31, add UFS+ hpb and tw driver*/
obj-$(CONFIG_UFSFEATURE) += ufsfeature.o
obj-$(CONFIG_UFSHPB) += ufshpb.o
obj-$(CONFIG_UFSTW) += ufstw.o
obj-$(CONFIG_UFSTW_DEBUGDRV) += ufstw-ddrv.o
#endif
obj-$(CONFIG_SCSI_UFSHCD_PCI) += ufshcd-pci.o
obj-$(CONFIG_SCSI_UFSHCD_PLATFORM) += ufshcd-pltfrm.o
obj-$(CONFIG_SCSI_UFS_TEST) += ufs_test.o
obj-$(CONFIG_DEBUG_FS) += ufs-debugfs.o ufs-qcom-debugfs.o
obj-$(CONFIG_SCSI_UFS_CRYPTO) += ufshcd-crypto.o
obj-$(CONFIG_SCSI_UFS_CRYPTO_QTI) += ufshcd-crypto-qti.o
