#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include "fw_download_interface.h"
#include "LC898124/Ois.h"
#include "linux/proc_fs.h"

extern unsigned char SelectDownload(uint8_t GyroSelect, uint8_t ActSelect, uint8_t MasterSlave, uint8_t FWType);
extern uint8_t FlashDownload128( uint8_t ModuleVendor, uint8_t ActVer, uint8_t MasterSlave, uint8_t FWType);

#define MAX_DATA_NUM 64
#define REG_OIS_STS  0x0001

static char ic_name_a[] = "lc898";
static char ic_name_b[] = "LC898";

struct mutex ois_mutex;
struct cam_ois_ctrl_t *ois_ctrl = NULL;
struct cam_ois_ctrl_t *ois_ctrls[CAM_OIS_TYPE_MAX] = {NULL};
enum cam_ois_state_vendor ois_state[CAM_OIS_TYPE_MAX] = {0};

#define OIS_REGISTER_SIZE 100
#define OIS_READ_REGISTER_DELAY 100
#define COMMAND_SIZE 255

struct dentry *ois_dentry = NULL;
bool dump_ois_registers = false;
static volatile int g_is_enable_main_ois_thread = 0;
static volatile int g_is_enable_tele_ois_thread = 0;

uint32_t ois_registers_124[OIS_REGISTER_SIZE][2] = {
	{0xF010, 0x0000},//Servo On/Off
	{0xF012, 0x0000},//Enable/Disable OIS
	{0xF013, 0x0000},//OIS Mode
	{0xF015, 0x0000},//Select Gyro vendor
	{0x82B8, 0x0000},//Gyro Gain X
	{0x8318, 0x0000},//Gyro Gain Y
	{0x0338, 0x0000},//Gyro Offset X
	{0x033c, 0x0000},//Gyro Offset Y
	{0x01C0, 0x0000},//Hall Offset X
	{0x0214, 0x0000},//Hall Offset Y
	{0x0310, 0x0000},//Gyro Raw Data X
	{0x0314, 0x0000},//Gyro Raw Data Y
	{0x0268, 0x0000},//Hall Raw Data X
	{0x026C, 0x0000},//Hall Raw Data Y
	{0xF100, 0x0000},//OIS status
	{0x0000, 0x0000},
};

uint32_t ois_registers_128[OIS_REGISTER_SIZE][2] = {
	{0xF010, 0x0000},//Servo On/Off
	{0xF012, 0x0000},//Enable/Disable OIS
	{0xF013, 0x0000},//OIS Mode
	{0xF015, 0x0000},//Select Gyro vendor
	{0x82B8, 0x0000},//Gyro Gain X
	{0x8318, 0x0000},//Gyro Gain Y
	{0x0240, 0x0000},//Gyro Offset X
	{0x0244, 0x0000},//Gyro Offset Y
	{0x00D8, 0x0000},//Hall Offset X
	{0x0128, 0x0000},//Hall Offset Y
	{0x0220, 0x0000},//Gyro Raw Data X
	{0x0224, 0x0000},//Gyro Raw Data Y
	{0x0178, 0x0000},//Hall Raw Data X
	{0x017C, 0x0000},//Hall Raw Data Y
	{0xF01D, 0x0000},//SPI IF read access command
	{0xF01E, 0x0000},//SPI IF Write access command
	{0xF100, 0x0000},//OIS status
	{0x0000, 0x0000},
};


static ssize_t ois_read(struct file *p_file,
    char __user *puser_buf, size_t count, loff_t *p_offset)
{

    return 0;
}

static ssize_t ois_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	char data[COMMAND_SIZE] = {0};
	char* const delim = " ";
	int iIndex = 0;
	char *token = NULL, *cur = NULL;
	uint32_t addr =0, value = 0;
	//int result = 0;
	//uint32_t read_data;
	if(puser_buf) {
		copy_from_user(&data, puser_buf, count);
	}

	cur = data;
	while ((token = strsep(&cur, delim))) {
		//CAM_ERR(CAM_OIS, "string = %s iIndex = %d, count = %d", token, iIndex, count);
		if (iIndex  == 0) {
			kstrtoint(token, 16, &addr);
		} else if (iIndex == 1) {
		    kstrtoint(token, 16, &value);
		}
		iIndex++;
	}
	return count;
}



static const struct file_operations proc_file_fops = {
	.owner = THIS_MODULE,
	.read  = ois_read,
	.write = ois_write,
};


int ois_start_read(void *arg, bool start)
{
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	mutex_lock(&(o_ctrl->ois_read_mutex));
	o_ctrl->ois_read_thread_start_to_read = start;
	mutex_unlock(&(o_ctrl->ois_read_mutex));

	msleep(OIS_READ_REGISTER_DELAY);

	return 0;
}

int ois_read_thread(void *arg)
{
	int rc = 0;
	int i;
	char buf[OIS_REGISTER_SIZE*2*4] = {0};
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	CAM_ERR(CAM_OIS, "ois_read_thread created");

	while (!kthread_should_stop()) {
		memset(buf, 0, sizeof(buf));
		mutex_lock(&(o_ctrl->ois_read_mutex));
		if (o_ctrl->ois_read_thread_start_to_read) {
			if (strstr(o_ctrl->ois_name, "124")) {
				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_124[i][0]) {
						ois_registers_124[i][1] = 0;
						camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)ois_registers_124[i][0], (uint32_t *)&ois_registers_124[i][1],
						                   CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
					}
				}

				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_124[i][0]) {
						snprintf(buf+strlen(buf), sizeof(buf), "0x%x,0x%x,", ois_registers_124[i][0], ois_registers_124[i][1]);
					}
				}
			} else if (strstr(o_ctrl->ois_name, "128")) {
				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_128[i][0]) {
						ois_registers_128[i][1] = 0;
						camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)ois_registers_128[i][0], (uint32_t *)&ois_registers_128[i][1],
						                   CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
					}
				}

				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_128[i][0]) {
						snprintf(buf+strlen(buf), sizeof(buf), "0x%x,0x%x,", ois_registers_128[i][0], ois_registers_128[i][1]);
					}
				}
			}
			CAM_ERR(CAM_OIS, "%s OIS register data: %s", o_ctrl->ois_name, buf);
		}
		mutex_unlock(&(o_ctrl->ois_read_mutex));

		msleep(OIS_READ_REGISTER_DELAY);
	}

	CAM_ERR(CAM_OIS, "ois_read_thread exist");

	return rc;
}

int ois_start_read_thread(void *arg, bool start)
{
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
		return -1;
	}

	mutex_lock(&(o_ctrl->ois_read_mutex));
	if (start) {
		if (o_ctrl->ois_read_thread) {
			CAM_ERR(CAM_OIS, "ois_read_thread is already created, no need to create again.");
		} else {
			o_ctrl->ois_read_thread = kthread_run(ois_read_thread, o_ctrl, o_ctrl->ois_name);
			if (!o_ctrl->ois_read_thread) {
				CAM_ERR(CAM_OIS, "create ois read thread failed");
				mutex_unlock(&(o_ctrl->ois_read_mutex));
				return -2;
			}
		}
	} else {
		if (o_ctrl->ois_read_thread) {
			o_ctrl->ois_read_thread_start_to_read = 0;
			kthread_stop(o_ctrl->ois_read_thread);
			o_ctrl->ois_read_thread = NULL;
		} else {
			CAM_ERR(CAM_OIS, "ois_read_thread is already stopped, no need to stop again.");
		}
	}
	mutex_unlock(&(o_ctrl->ois_read_mutex));

	return 0;
}

void WitTim( uint16_t time)
{
	msleep(time);
}

void CntRd(uint32_t addr, void *data, uint16_t size)
{
	int i = 0;
	int32_t rc = 0;
	int retry = 3;
	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read_seq(&(o_ctrl->io_master_info), addr, (uint8_t *)data,
		                            CAMERA_SENSOR_I2C_TYPE_WORD,
		                            CAMERA_SENSOR_I2C_TYPE_BYTE,
		                            size);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Continue read failed, rc:%d, retry:%d", rc, i+1);
		} else {
			break;
		}
	}
}

void CntWrt(  void *register_data, uint16_t size)
{
	uint8_t *data = (uint8_t *)register_data;
	int32_t rc = 0;
	int i = 0;
	int reg_data_cnt = size - 1;
	int continue_cnt = 0;
	int retry = 3;
	static struct cam_sensor_i2c_reg_array *i2c_write_setting_gl = NULL;

	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	struct cam_sensor_i2c_reg_setting i2c_write;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return;
	}

	if (i2c_write_setting_gl == NULL) {
		i2c_write_setting_gl = (struct cam_sensor_i2c_reg_array *)kzalloc(
		                           sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2, GFP_KERNEL);
		if(!i2c_write_setting_gl) {
			CAM_ERR(CAM_OIS, "Alloc i2c_write_setting_gl failed");
			return;
		}
	}

	memset(i2c_write_setting_gl, 0, sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2);

	for(i = 0; i< reg_data_cnt; i++) {
		if (i == 0) {
			i2c_write_setting_gl[continue_cnt].reg_addr = data[0];
			i2c_write_setting_gl[continue_cnt].reg_data = data[1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		} else {
			i2c_write_setting_gl[continue_cnt].reg_data = data[i+1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		}
		continue_cnt++;
	}
	i2c_write.reg_setting = i2c_write_setting_gl;
	i2c_write.size = continue_cnt;
	i2c_write.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_write.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_write.delay = 0x00;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
		                                    &i2c_write, 1);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Continue write failed, rc:%d, retry:%d", rc, i+1);
		} else {
			break;
		}
	}
}


int RamWrite32A(    uint32_t addr, uint32_t data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = 0x00,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD,
		.delay = 0x00,
	};

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write(&(o_ctrl->io_master_info), &i2c_write);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "write 0x%04x failed, retry:%d", addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

int RamRead32A(uint32_t addr, uint32_t* data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}
	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)addr, (uint32_t *)data,
		                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_BYTE);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "read 0x%04x failed, retry:%d", addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

void OISCountinueRead(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, void *data, uint16_t size)
{
	int i = 0;
	int32_t rc = 0;
	int retry = 3;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read_seq(&(o_ctrl->io_master_info), addr, (uint8_t *)data,
		                            CAMERA_SENSOR_I2C_TYPE_WORD,
		                            CAMERA_SENSOR_I2C_TYPE_DWORD,
		                            size);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Continue read failed, rc:%d, retry:%d", rc, i+1);
		} else {
			break;
		}
	}
}

void OISCountinueWrite(  struct cam_ois_ctrl_t *o_ctrl, void *register_data, uint16_t size)
{
	uint32_t *data = (uint32_t *)register_data;
	int32_t rc = 0;
	int i = 0;
	int reg_data_cnt = size - 1;
	int continue_cnt = 0;
	int retry = 3;
	static struct cam_sensor_i2c_reg_array *i2c_write_setting_gl = NULL;

	struct cam_sensor_i2c_reg_setting i2c_write;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return;
	}

	if (i2c_write_setting_gl == NULL) {
		i2c_write_setting_gl = (struct cam_sensor_i2c_reg_array *)kzalloc(
		                           sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2, GFP_KERNEL);
		if(!i2c_write_setting_gl) {
			CAM_ERR(CAM_OIS, "Alloc i2c_write_setting_gl failed");
			return;
		}
	}

	memset(i2c_write_setting_gl, 0, sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2);

	for(i = 0; i< reg_data_cnt; i++) {
		if (i == 0) {
			i2c_write_setting_gl[continue_cnt].reg_addr = data[0];
			i2c_write_setting_gl[continue_cnt].reg_data = data[1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		} else {
			i2c_write_setting_gl[continue_cnt].reg_data = data[i+1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		}
		continue_cnt++;
	}
	i2c_write.reg_setting = i2c_write_setting_gl;
	i2c_write.size = continue_cnt;
	i2c_write.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	i2c_write.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
	i2c_write.delay = 0x00;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
		                                    &i2c_write, 1);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Continue write failed, rc:%d, retry:%d", rc, i+1);
		} else {
			break;
		}
	}
}

int OISWrite(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = 0x00,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD,
		.delay = 0x00,
	};

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write(&(o_ctrl->io_master_info), &i2c_write);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "write 0x%04x failed, retry:%d", addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

int OISRead(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t* data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}
	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)addr, (uint32_t *)data,
		                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "read 0x%04x failed, retry:%d", addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

void Set124Or128GyroAccelCoef(struct cam_ois_ctrl_t *o_ctrl)
{
	CAM_ERR(CAM_OIS, "SetGyroAccelCoef SelectAct 0x%x GyroPostion 0x%x\n", o_ctrl->ois_actuator_vendor, o_ctrl->ois_gyro_position);

	if (strstr(o_ctrl->ois_name, "124")) {
		if(o_ctrl->ois_gyro_position==3) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x80000001);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_ZP, (UINT32) 0x7FFFFFFF);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		} else if(o_ctrl->ois_gyro_position==2) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_ZP, (UINT32) 0x7FFFFFFF);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		}else if(o_ctrl->ois_gyro_position==4) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x80000001);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x80000001);
			RamWrite32A( GCNV_ZP, (UINT32) 0x7FFFFFFF);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		}
	} else if (strstr(o_ctrl->ois_name, "128")) {

	}
}


static int Download124Or128FW(struct cam_ois_ctrl_t *o_ctrl)
{
	uint32_t UlReadValX, UlReadValY;
	uint32_t spi_type;
	unsigned char rc = 0;
	struct timespec mStartTime, mEndTime, diff;
	uint64_t mSpendTime = 0;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	ois_ctrl = o_ctrl;

	getnstimeofday(&mStartTime);

	CAM_INFO(CAM_OIS, "MasterSlave 0x%x, GyroVendor 0x%x, GyroPosition 0x%x, ModuleVendor 0x%x, ActVer 0x%x, FWType 0x%x\n",
	         o_ctrl->ois_type, o_ctrl->ois_gyro_vendor, o_ctrl->ois_gyro_position, o_ctrl->ois_module_vendor, o_ctrl->ois_actuator_vendor, o_ctrl->ois_fw_flag);

	if (strstr(o_ctrl->ois_name, "124")) {
		rc = SelectDownload(o_ctrl->ois_gyro_vendor, o_ctrl->ois_actuator_vendor, o_ctrl->ois_type, o_ctrl->ois_fw_flag);

		if (0 == rc) {
			Set124Or128GyroAccelCoef(ois_ctrl);

			//remap
			RamWrite32A(0xF000, 0x00000000 );
			//msleep(120);

			//SPI-Master ( Act1 )  Check gyro signal
			RamRead32A(0x061C, & UlReadValX );
			RamRead32A(0x0620, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_X:0x%x, Gyro_Y:0x%x", UlReadValX, UlReadValY);

			spi_type = 0;
			RamRead32A(0xf112, & spi_type );
			CAM_INFO(CAM_OIS, "spi_type:0x%x", spi_type);

			//SPI-Master ( Act1 )  Check gyro gain
			RamRead32A(0x82b8, & UlReadValX );
			RamRead32A(0x8318, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_gain_X:0x%x, Gyro_gain_Y:0x%x", UlReadValX, UlReadValY);

			//SPI-Master ( Act1 )  start gyro signal transfer. ( from Master to slave. )
			if (CAM_OIS_MASTER == o_ctrl->ois_type) {
				RamWrite32A(0x8970, 0x00000001 );
				//msleep(5);
				RamWrite32A(0xf111, 0x00000001 );
				//msleep(5);
			}
		} else {
			switch (rc) {
			case 0x01:
				CAM_ERR(CAM_OIS, "H/W error");
				break;
			case 0x02:
				CAM_ERR(CAM_OIS, "Table Data & Program download verify error");
				break;
			case 0xF0:
				CAM_ERR(CAM_OIS, "Download code select error");
				break;
			case 0xF1:
				CAM_ERR(CAM_OIS, "Download code information read error");
				break;
			case 0xF2:
				CAM_ERR(CAM_OIS, "Download code information disagreement");
				break;
			case 0xF3:
				CAM_ERR(CAM_OIS, "Download code version error");
				break;
			default:
				CAM_ERR(CAM_OIS, "Unkown error code");
				break;
			}
		}
	} else if (strstr(o_ctrl->ois_name, "128")) {
		rc = FlashDownload128(o_ctrl->ois_module_vendor, o_ctrl->ois_actuator_vendor, o_ctrl->ois_type, o_ctrl->ois_fw_flag);

		if (0 == rc) {
			Set124Or128GyroAccelCoef(ois_ctrl);

			//LC898128 don't need to do remap
			//RamWrite32A(0xF000, 0x00000000 );
			//msleep(120);

			//select gyro vendor
			RamWrite32A(0xF015, o_ctrl->ois_gyro_vendor);
			msleep(10);

			//SPI-Master ( Act1 )  Check gyro signal
			RamRead32A(0x0220, & UlReadValX );
			RamRead32A(0x0224, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_X:0x%x, Gyro_Y:0x%x", UlReadValX, UlReadValY);

			spi_type = 0;
			RamRead32A(0xf112, & spi_type );
			CAM_INFO(CAM_OIS, "spi_type:0x%x", spi_type);

			//SPI-Master ( Act1 )  Check gyro gain
			RamRead32A(0x82b8, & UlReadValX );
			RamRead32A(0x8318, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_gain_X:0x%x, Gyro_gain_Y:0x%x", UlReadValX, UlReadValY);

			//SPI-Master ( Act1 )  start gyro signal transfer. ( from Master to slave. )
			//if (CAM_OIS_MASTER == o_ctrl->ois_type) {
			//	RamWrite32A(0xF017, 0x01);
			//}
		} else {
			switch (rc&0xF0) {
			case 0x00:
				CAM_ERR(CAM_OIS, "Error ; during the rom boot changing. Also including 128 power off issue.");
				break;
			case 0x20:
				CAM_ERR(CAM_OIS, "Error ; during Initial program for updating to program memory.");
				break;
			case 0x30:
				CAM_ERR(CAM_OIS, "Error ; during User Mat area erasing.");
				break;
			case 0x40:
				CAM_ERR(CAM_OIS, "Error ; during User Mat area programing.");
				break;
			case 0x50:
				CAM_ERR(CAM_OIS, "Error ; during the verification.");
				break;
			case 0x90:
				CAM_ERR(CAM_OIS, "Error ; during the drive offset confirmation.");
				break;
			case 0xA0:
				CAM_ERR(CAM_OIS, "Error ; during the MAT2 re-write process.");
				break;
			case 0xF0:
				if (rc == 0xF0)
					CAM_ERR(CAM_OIS, "mistake of module vendor designation.");
				else if (rc == 0xF1)
					CAM_ERR(CAM_OIS, "mistake size of From Code.");
				break;
			default:
				CAM_ERR(CAM_OIS, "Unkown error code");
				break;
			}
		}
	} else {
		CAM_ERR(CAM_OIS, "Unsupported OIS");
	}
	getnstimeofday(&mEndTime);
	diff = timespec_sub(mEndTime, mStartTime);
	mSpendTime = (timespec_to_ns(&diff))/1000000;

	CAM_INFO(CAM_OIS, "cam_ois_fw_download rc=%d, (Spend: %d ms)", rc, mSpendTime);

	return 0;
}

int DownloadFW(struct cam_ois_ctrl_t *o_ctrl)
{
	uint8_t rc = 0;

	if (o_ctrl) {
		if (strstr(o_ctrl->ois_name, ic_name_a) == NULL
			&& strstr(o_ctrl->ois_name, ic_name_b) == NULL) {
			return 0;
		}
		mutex_lock(&ois_mutex);

		if (CAM_OIS_INVALID == ois_state[o_ctrl->ois_type]) {

			if (CAM_OIS_MASTER == o_ctrl->ois_type) {
				rc = Download124Or128FW(ois_ctrls[CAM_OIS_MASTER]);
				if (rc) {
					CAM_ERR(CAM_OIS, "Download %s FW failed", o_ctrl->ois_name);
				} else {
					if (dump_ois_registers && !ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 1)) {
						ois_start_read(ois_ctrls[CAM_OIS_MASTER], 1);
					}
				}
			} else if (CAM_OIS_SLAVE == o_ctrl->ois_type) {
				if (CAM_OIS_INVALID == ois_state[CAM_OIS_MASTER]) {
					rc = Download124Or128FW(ois_ctrls[CAM_OIS_MASTER]);
					if (!rc&&dump_ois_registers&&!ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 1)) {
						ois_start_read(ois_ctrls[CAM_OIS_MASTER], 1);
					}
					msleep(120);// Need to check whether we need a long time delay
				}
				if (rc) {
					CAM_ERR(CAM_OIS, "Download %s FW failed", ois_ctrls[CAM_OIS_MASTER]->ois_name);
				} else {
					rc = Download124Or128FW(ois_ctrls[CAM_OIS_SLAVE]);
					if (rc) {
						CAM_ERR(CAM_OIS, "Download %s FW failed", o_ctrl->ois_name);
					} else {
						if (dump_ois_registers&&!ois_start_read_thread(ois_ctrls[CAM_OIS_SLAVE], 1)) {
							ois_start_read(ois_ctrls[CAM_OIS_SLAVE], 1);
						}
					}
				}
			}
			ois_state[o_ctrl->ois_type] = CAM_OIS_FW_DOWNLOADED;
		} else {
			CAM_ERR(CAM_OIS, "OIS state 0x%x is wrong", ois_state[o_ctrl->ois_type]);
		}
		mutex_unlock(&ois_mutex);
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}

	return rc;
}

int OISPollThread124(void *arg)
{
#define SAMPLE_COUNT_IN_OIS_124 7
#define SAMPLE_INTERVAL     4000
	int32_t i = 0;
	uint32_t *data = NULL;
	uint32_t kfifo_in_len = 0;
	uint32_t fifo_size_in_ois = SAMPLE_COUNT_IN_OIS_124*OIS_HALL_SAMPLE_BYTE;
	uint32_t fifo_size_in_ois_driver = OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE;
	struct timeval systemTime;
	uint64_t timestamp;

	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;
	uint32_t ois_hall_registers[SAMPLE_COUNT_IN_OIS_124] = {0x89C4, 0x89C0, 0x89BC, 0x89B8, 0x89B4, 0x89B0, 0x89AC};

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

	data = kzalloc(fifo_size_in_ois, GFP_KERNEL);
	if (!data) {
		CAM_ERR(CAM_OIS, "failed to kzalloc");
		return -1;
	}

	CAM_DBG(CAM_OIS, "OISPollThread124 creat");

	while(1) {
		mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
		if (o_ctrl->ois_poll_thread_exit) {
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			goto exit;
		}
		mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
		do_gettimeofday(&systemTime);
		timestamp = (systemTime.tv_sec & 0xFFFFFFFF) * 1000000 + systemTime.tv_usec;
		memset(data, 0, fifo_size_in_ois);
		//Read OIS HALL data
		for (i = 0; i < SAMPLE_COUNT_IN_OIS_124; i++) {
			data[3*i] = timestamp/1000000;
			data[3*i+1] = timestamp%1000000;
			OISRead(o_ctrl, ois_hall_registers[i], &(data[3*i+2]));
			timestamp -= SAMPLE_INTERVAL;
		}

		for (i = SAMPLE_COUNT_IN_OIS_124 - 1; i >= 0; i--) {
				CAM_DBG(CAM_OIS, "OIS HALL data %lld (0x%x 0x%x)", (uint64_t)data[3*i]*1000000+(uint64_t)data[3*i+1], data[3*i+2]&0xFFFF0000>>16, data[3*i+2]&0xFFFF);
		}

		mutex_lock(&(o_ctrl->ois_hall_data_mutex));
		if (kfifo_len(&(o_ctrl->ois_hall_data_fifo))<fifo_size_in_ois_driver) {
			kfifo_in_len = kfifo_in(&(o_ctrl->ois_hall_data_fifo), data, fifo_size_in_ois);
			if (kfifo_in_len != fifo_size_in_ois) {
				CAM_DBG(CAM_OIS, "kfifo_in %d Bytes, FIFO maybe full, some OIS Hall sample maybe dropped.", kfifo_in_len);
			} else {
				CAM_DBG(CAM_OIS, "kfifo_in %d Bytes", fifo_size_in_ois);
			}
		} else {
			CAM_DBG(CAM_OIS, "ois_hall_data_fifo is full, fifo size %d, file len %d", kfifo_size(&(o_ctrl->ois_hall_data_fifo)), kfifo_len(&(o_ctrl->ois_hall_data_fifo)));
		}
		mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

		usleep_range(SAMPLE_COUNT_IN_OIS_124*SAMPLE_INTERVAL-5, SAMPLE_COUNT_IN_OIS_124*SAMPLE_INTERVAL);
	}

exit:
	kfree(data);
	CAM_DBG(CAM_OIS, "OISPollThread124 exit");
	return 0;
}

void forceExitpoll(struct cam_ois_ctrl_t *o_ctrl)
{
	if (o_ctrl) {
		if (strstr(o_ctrl->ois_name, ic_name_a) == NULL
			&& strstr(o_ctrl->ois_name, ic_name_b) == NULL
			&& strstr(o_ctrl->ois_name, "sem1215") == NULL) {
			return;
		}
	}
	CAM_DBG(CAM_OIS, "++++:%s ois-name %s", __func__,o_ctrl->ois_name);
	cancel_delayed_work_sync(&o_ctrl->ois_delaywork);
	o_ctrl->ois_poll_thread_exit = false;
}

int OISPollThread128(void *arg)
{
	uint32_t i = 0;
	uint32_t *data = NULL;
	uint32_t fifo_len_in_ois = 0;
	uint32_t kfifo_in_len = 0;
	uint32_t fifo_size_in_ois = SAMPLE_COUNT_IN_OIS_FIFO*OIS_HALL_SAMPLE_BYTE;
	uint32_t fifo_size_in_ois_driver = OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE;
	struct timeval systemTime;
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

	data = kzalloc(fifo_size_in_ois, GFP_KERNEL);
	if (!data) {
		CAM_ERR(CAM_OIS, "failed to kzalloc");
		return -1;
	}
	CAM_DBG(CAM_OIS, "OISPollThread128 creat");
	for(i = 0; i < 2; i++) {
		do_gettimeofday(&systemTime);
		//Write TS to OIS controller, we need to write two TS to OIS.
		data[0] = 0xF110;
		data[1] = systemTime.tv_sec;
		data[2] = systemTime.tv_usec;
		CAM_DBG(CAM_OIS, "TS %d:%d", systemTime.tv_sec, systemTime.tv_usec);
		OISCountinueWrite(o_ctrl, (void *)data, 3);
		usleep_range(1995, 2000);
	}

	while(1) {
		fifo_len_in_ois = 0;
		while (fifo_len_in_ois != SAMPLE_COUNT_IN_OIS_FIFO) {
			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			if (o_ctrl->ois_poll_thread_exit) {
				mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
				goto exit;
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			//Read OIS FIFO length
			OISRead(o_ctrl, 0x89E8, &fifo_len_in_ois);
			CAM_DBG(CAM_OIS, "FIFO length %d", fifo_len_in_ois);
			if (fifo_len_in_ois == SAMPLE_COUNT_IN_OIS_FIFO) {
				break;
			}
			usleep_range(1995, 2000);
		}
		if (fifo_len_in_ois == SAMPLE_COUNT_IN_OIS_FIFO) {
			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			if (o_ctrl->ois_poll_thread_exit) {
				mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
				goto exit;
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			memset(data, 0, fifo_size_in_ois);
			//Read OIS HALL data
			OISCountinueRead(o_ctrl, 0xF111, (void *)data, fifo_size_in_ois);

			for (i = 0; i < SAMPLE_COUNT_IN_OIS_FIFO; i++) {
				CAM_DBG(CAM_OIS, "OIS HALL data %lld (0x%x 0x%x)", (uint64_t)data[3*i]*1000000+(uint64_t)data[3*i+1], (data[2+i*3]&0xFFFF0000)>>16, data[2+i*3]&0xFFFF);
			}

			mutex_lock(&(o_ctrl->ois_hall_data_mutex));
			if (kfifo_len(&(o_ctrl->ois_hall_data_fifo))<fifo_size_in_ois_driver) {
				kfifo_in_len = kfifo_in(&(o_ctrl->ois_hall_data_fifo), data, fifo_size_in_ois);
				if (kfifo_in_len != fifo_size_in_ois) {
					CAM_DBG(CAM_OIS, "kfifo_in %d Bytes, FIFO maybe full, some OIS Hall sample maybe dropped.", kfifo_in_len);
				} else {
					CAM_DBG(CAM_OIS, "kfifo_in %d Bytes", fifo_size_in_ois);
				}
			} else {
				CAM_DBG(CAM_OIS, "ois_hall_data_fifo is full, fifo size %d, file len %d", kfifo_size(&(o_ctrl->ois_hall_data_fifo)), kfifo_len(&(o_ctrl->ois_hall_data_fifo)));
			}
			mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

			usleep_range(39995, 40000);
		}
	}

exit:
	kfree(data);
	CAM_DBG(CAM_OIS, "OISPollThread128 exit");
	return 0;
}

static void Sem1215sOISPollThreadwork(struct work_struct *work)
{

	#define SEM1215S_SAMPLE_COUNT_IN_OIS               1
	#define SEM1215S_SAMPLE_INTERVAL                   4000

	int32_t i = 0;
	uint32_t *data = NULL;
	uint32_t data_x = 0;
	uint32_t data_y = 0;
	uint32_t kfifo_in_len = 0;
	uint32_t fifo_size_in_ois = SEM1215S_SAMPLE_COUNT_IN_OIS*OIS_HALL_SAMPLE_BYTE;
	uint32_t fifo_size_in_ois_driver = OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE;
	unsigned long long timestampQ = 0;
	struct delayed_work *dw = to_delayed_work(work);
	struct cam_ois_ctrl_t *o_ctrl = container_of(dw, struct cam_ois_ctrl_t, ois_delaywork);
	uint32_t ois_hall_registers[SEM1215S_SAMPLE_COUNT_IN_OIS] = {0x1100};

	data = kzalloc(fifo_size_in_ois, GFP_KERNEL);
	if (!data) {
		CAM_ERR(CAM_OIS, "failed to kzalloc");
		return ;
	}

	timestampQ = arch_counter_get_cntvct();

	memset(data, 0, fifo_size_in_ois);
	//Read OIS HALL data
	for (i = 0; i < 1; i++) {
		data[3*i] = timestampQ >> 32;
		data[3*i+1] = timestampQ & 0xFFFFFFFF;
		camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)ois_hall_registers[i], &data_x,
	                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_WORD);
		camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)ois_hall_registers[i]+2, &data_y,
							CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_WORD);
		data[3*i+2] = (data_x & 0xFFFF) | ((data_y & 0xFFFF) << 16);//hig16_y,low16_x
		timestampQ += CLOCK_TICKCOUNT_MS * 4;
	}

	for (i = 0; i < 1 ; i++) {
		CAM_DBG(CAM_OIS, "OIS HALL data %lld (y:0x%x,x:0x%x)", ((uint64_t)data[3*i] << 32)+(uint64_t)data[3*i+1], (data[3*i+2]&0xFFFF0000)>>16, data[3*i+2]&0xFFFF);
	}

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	if ((kfifo_len(&(o_ctrl->ois_hall_data_fifo)) + fifo_size_in_ois) > fifo_size_in_ois_driver) {
		CAM_DBG(CAM_OIS, "ois_hall_data_fifo is full, fifo size %d, file len %d, will reset FIFO", kfifo_size(&(o_ctrl->ois_hall_data_fifo)), kfifo_len(&(o_ctrl->ois_hall_data_fifo)));
		kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
	}

	if ((kfifo_len(&(o_ctrl->ois_hall_data_fifo)) + fifo_size_in_ois) <= fifo_size_in_ois_driver) {
		kfifo_in_len = kfifo_in(&(o_ctrl->ois_hall_data_fifo), data, fifo_size_in_ois);
		if (kfifo_in_len != fifo_size_in_ois) {
			CAM_DBG(CAM_OIS, "kfifo_in %d Bytes, FIFO maybe full, some OIS Hall sample maybe dropped.", kfifo_in_len);
		} else {
			//CAM_ERR(CAM_OIS, "kfifo_in %d Bytes", fifo_size_in_ois);
		}
	}
	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

	kfree(data);

	schedule_delayed_work(&o_ctrl->ois_delaywork, msecs_to_jiffies(4));
}


void ReadOISHALLData(struct cam_ois_ctrl_t *o_ctrl, void *data)
{
	uint32_t data_size = 0;
	uint32_t fifo_len_in_ois_driver;

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	fifo_len_in_ois_driver = kfifo_len(&(o_ctrl->ois_hall_data_fifo));
	if (fifo_len_in_ois_driver > 0) {
		if (fifo_len_in_ois_driver > OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE) {
			fifo_len_in_ois_driver = OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE;
		}
		kfifo_to_user(&(o_ctrl->ois_hall_data_fifo), data, fifo_len_in_ois_driver, &data_size);
		CAM_DBG(CAM_OIS, "Copied %d Bytes to UMD", data_size);
	} else {
		CAM_DBG(CAM_OIS, "fifo_len is %d, no need copy to UMD", fifo_len_in_ois_driver);
	}
	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
}

void Sem1215sReadOISHALLData(struct cam_ois_ctrl_t *o_ctrl, void *data)
{
	uint32_t data_size = 0;
	uint32_t fifo_len_in_ois_driver;
	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	fifo_len_in_ois_driver = kfifo_len(&(o_ctrl->ois_hall_data_fifo));
	if (fifo_len_in_ois_driver > 0) {
		if (fifo_len_in_ois_driver > OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE) {
			fifo_len_in_ois_driver = OIS_HALL_SAMPLE_COUNT*OIS_HALL_SAMPLE_BYTE;
		}
		kfifo_to_user(&(o_ctrl->ois_hall_data_fifo), data, fifo_len_in_ois_driver, &data_size);
		CAM_DBG(CAM_OIS, "Copied %d Bytes to UMD", data_size);
	} else {
		CAM_DBG(CAM_OIS, "fifo_len is %d, no need copy to UMD", fifo_len_in_ois_driver);
	}
	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
}

void Sem1215sReadOISHALLDataV2(struct cam_ois_ctrl_t *o_ctrl, void *data)
{
	uint32_t fifo_len_in_ois_driver;
	int j =0;
	mutex_lock(&(o_ctrl->ois_hall_data_mutex));

	for(j = OIS_HALL_DATA_INFO_CACHE_SIZE - 1; j > 0; j--)
	{
		memset(&o_ctrl->hall_data[j],0,sizeof(OISHALL2EIS));
		o_ctrl->hall_data[j] = o_ctrl->hall_data[j-1];
	}

	fifo_len_in_ois_driver = kfifo_len(&(o_ctrl->ois_hall_data_fifo));
	if (fifo_len_in_ois_driver > 0) {
		if (fifo_len_in_ois_driver > SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_DRIVER) {
			fifo_len_in_ois_driver = SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_DRIVER;
		}
		memset(&o_ctrl->hall_data[0],0,sizeof(OISHALL2EIS));
		kfifo_out(&(o_ctrl->ois_hall_data_fifo),&o_ctrl->hall_data,fifo_len_in_ois_driver);
		copy_to_user((void __user *)data, &o_ctrl->hall_data[0],sizeof(OISHALL2EIS)*OIS_HALL_DATA_INFO_CACHE_SIZE);
		CAM_DBG(CAM_OIS, "Sem1215sReadOISHALLDataV2 Copied %d Bytes to UMD", fifo_len_in_ois_driver);
	} else {
		CAM_DBG(CAM_OIS, "Sem1215sReadOISHALLDataV2 fifo_len is %d, no need copy to UMD", fifo_len_in_ois_driver);
	}

	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

}

void OISControl(struct cam_ois_ctrl_t *o_ctrl, void *arg)
{

}

void set_ois_thread_status(int main_thread_status ,int tele_thread_status)
{
		g_is_enable_main_ois_thread = main_thread_status;
		g_is_enable_tele_ois_thread = tele_thread_status;

}


bool IsOISReady(struct cam_ois_ctrl_t *o_ctrl)
{
	uint32_t temp, retry_cnt;
	retry_cnt = 3;
	if (o_ctrl) {
		if (strstr(o_ctrl->ois_name, "sem1215") != NULL) {
			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			CAM_DBG(CAM_OIS, "g_is_enable_tele_ois_thread:%d,ois_poll_thread_exit:%d",g_is_enable_tele_ois_thread,o_ctrl->ois_poll_thread_exit);
			if (o_ctrl->ois_poll_thread_exit
				|| false == g_is_enable_tele_ois_thread) {
				CAM_DBG(CAM_OIS, "tele camera Sem1215sOISPollThread work is already created, no need to create again.");
			} else {
				o_ctrl->ois_poll_thread_exit = true;
				schedule_delayed_work(&o_ctrl->ois_delaywork, 10); //start work
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			#if 0
			if (!o_ctrl->ois_poll_thread) {
				o_ctrl->ois_poll_thread_exit = true;
				CAM_ERR(CAM_OIS, "create ois poll thread failed");
				return false;
			}
			#endif
			return true;
		}
		if (strstr(o_ctrl->ois_name, ic_name_a) == NULL
			&& strstr(o_ctrl->ois_name, ic_name_b) == NULL) {
			return true;
		}
		if (CAM_OIS_READY == ois_state[o_ctrl->ois_type]) {
			CAM_INFO(CAM_OIS, "%s OIS %d is ready ",o_ctrl->ois_name,o_ctrl->ois_type);

			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			if (o_ctrl->ois_poll_thread
					|| false == g_is_enable_main_ois_thread ) {
				CAM_ERR(CAM_OIS, "main camera ois_poll_thread is already created or don't need to create..");
			} else {
				o_ctrl->ois_poll_thread_exit = false;
				if (strstr(o_ctrl->ois_name, "128")) {
					o_ctrl->ois_poll_thread = kthread_run(OISPollThread128, o_ctrl, o_ctrl->ois_name);
				} else if (strstr(o_ctrl->ois_name, "124")) {
					o_ctrl->ois_poll_thread = kthread_run(OISPollThread124, o_ctrl, o_ctrl->ois_name);
				}
				if (!o_ctrl->ois_poll_thread) {
					o_ctrl->ois_poll_thread_exit = true;
					CAM_ERR(CAM_OIS, "create ois poll thread failed");
				}
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			return true;
		} else {
			do {
				//RamRead32A_oneplus(o_ctrl,0xF100, &temp);
				CAM_ERR(CAM_OIS, "OIS %d 0xF100 = 0x%x", o_ctrl->ois_type, temp);
				if (temp == 0) {
					ois_state[o_ctrl->ois_type] = CAM_OIS_READY;
					return true;
				}
				retry_cnt--;
				msleep(10);
			} while(retry_cnt);
			return false;
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
		return false;
	}
}

void InitOIS(struct cam_ois_ctrl_t *o_ctrl)
{
	if (o_ctrl) {
		if (strstr(o_ctrl->ois_name, ic_name_a) == NULL
			&& strstr(o_ctrl->ois_name, ic_name_b) == NULL) {
			return;
		}
		if (o_ctrl->ois_type == CAM_OIS_MASTER) {
			ois_state[CAM_OIS_MASTER] = CAM_OIS_INVALID;
		} else if (o_ctrl->ois_type == CAM_OIS_SLAVE) {
			ois_state[CAM_OIS_SLAVE] = CAM_OIS_INVALID;
			if (ois_ctrls[CAM_OIS_MASTER]) {
				if (camera_io_init(&(ois_ctrls[CAM_OIS_MASTER]->io_master_info))) {
					CAM_ERR(CAM_OIS, "cci_init failed");
				}
			}
		} else {
			CAM_ERR(CAM_OIS, "ois_type 0x%x is wrong", o_ctrl->ois_type);
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}
}

void DeinitOIS(struct cam_ois_ctrl_t *o_ctrl)
{
	set_ois_thread_status(0,0);
	if (o_ctrl) {
		#if 0
		if (strstr(o_ctrl->ois_name, "sem1215") != NULL)
		{
			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			if (o_ctrl->ois_poll_thread) {
				o_ctrl->ois_poll_thread_exit = true;
				o_ctrl->ois_poll_thread = NULL;
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
		}
		#endif
		if (strstr(o_ctrl->ois_name, ic_name_a) == NULL
			&& strstr(o_ctrl->ois_name, ic_name_b) == NULL) {
			return;
		}

		mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
		if (o_ctrl->ois_poll_thread) {
			o_ctrl->ois_poll_thread_exit = true;
			o_ctrl->ois_poll_thread = NULL;
		}
		mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));

		if (o_ctrl->ois_type == CAM_OIS_MASTER) {
			if (dump_ois_registers&&ois_ctrls[CAM_OIS_MASTER]) {
				ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 0);
			}
			ois_state[CAM_OIS_MASTER] = CAM_OIS_INVALID;
		} else if (o_ctrl->ois_type == CAM_OIS_SLAVE) {
			if (ois_ctrls[CAM_OIS_MASTER]) {
				if(dump_ois_registers) {
					ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 0);
				}
				if (camera_io_release(&(ois_ctrls[CAM_OIS_MASTER]->io_master_info))) {
					CAM_ERR(CAM_OIS, "cci_deinit failed");
				}
			}

			if (ois_ctrls[CAM_OIS_SLAVE]) {
				if(dump_ois_registers) {
					ois_start_read_thread(ois_ctrls[CAM_OIS_SLAVE], 0);
				}
			}
			ois_state[CAM_OIS_SLAVE] = CAM_OIS_INVALID;

		} else {
			CAM_ERR(CAM_OIS, "ois_type 0x%x is wrong", o_ctrl->ois_type);
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}
}

void InitOISResource(struct cam_ois_ctrl_t *o_ctrl)
{
	struct proc_dir_entry *face_common_dir = NULL;
	struct proc_dir_entry *proc_file_entry = NULL;
	if (o_ctrl) {
		INIT_DELAYED_WORK(&o_ctrl->ois_delaywork, Sem1215sOISPollThreadwork);
		if (strstr(o_ctrl->ois_name, ic_name_a) == NULL
			&& strstr(o_ctrl->ois_name, ic_name_b) == NULL) {
			return;
		}
		mutex_init(&ois_mutex);
		if (o_ctrl->ois_type == CAM_OIS_MASTER) {
			ois_ctrls[CAM_OIS_MASTER] = o_ctrl;
			//Hardcode the parameters of main OIS, and those parameters will be overrided when open main camera
			o_ctrl->io_master_info.cci_client->sid = 0x24;
			o_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;
			o_ctrl->io_master_info.cci_client->retries = 3;
			o_ctrl->io_master_info.cci_client->id_map = 0;
			CAM_INFO(CAM_OIS, "ois_ctrls[%d] = %p", CAM_OIS_MASTER, ois_ctrls[CAM_OIS_MASTER]);
		} else if (o_ctrl->ois_type == CAM_OIS_SLAVE) {
			ois_ctrls[CAM_OIS_SLAVE] = o_ctrl;
			CAM_INFO(CAM_OIS, "ois_ctrls[%d] = %p", CAM_OIS_SLAVE, ois_ctrls[CAM_OIS_SLAVE]);
		} else {
			CAM_ERR(CAM_OIS, "ois_type 0x%x is wrong", o_ctrl->ois_type);
		}

		if (!ois_dentry) {
			ois_dentry = debugfs_create_dir("camera_ois", NULL);
			if (ois_dentry) {
				debugfs_create_bool("dump_registers", 0644, ois_dentry, &dump_ois_registers);
			} else {
				CAM_ERR(CAM_OIS, "failed to create dump_registers node");
			}
		} else {
			CAM_ERR(CAM_OIS, "dump_registers node exist");
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}
	//Create OIS control node
	face_common_dir =  proc_mkdir("OIS", NULL);
	if(!face_common_dir) {
		CAM_ERR(CAM_OIS, "create dir fail CAM_ERROR API");
		//return FACE_ERROR_GENERAL;
	}

	proc_file_entry = proc_create("OISControl", 0777, face_common_dir, &proc_file_fops);
	if(proc_file_entry == NULL) {
		CAM_ERR(CAM_OIS, "Create fail");
	} else {
		CAM_INFO(CAM_OIS, "Create successs");
	}
}

void CheckOISdata(void) {
	uint32_t temp = 0x0;
	RamRead32A(0XF010, &temp);
	CAM_INFO(CAM_OIS, "0XF010 = 0x%0x", temp);
	RamRead32A(0XF011, &temp);
	CAM_INFO(CAM_OIS, "0XF011 = 0x%0x", temp);
	RamRead32A(0XF012, &temp);
	CAM_INFO(CAM_OIS, "0XF012 = 0x%0x", temp);
	RamRead32A(0XF013, &temp);
	CAM_INFO(CAM_OIS, "0XF013 = 0x%0x", temp);
	RamRead32A(0XF015, &temp);
	CAM_INFO(CAM_OIS, "0XF015 = 0x%0x", temp);
	RamRead32A(0XF017, &temp);
	CAM_INFO(CAM_OIS, "0XF017 = 0x%0x", temp);
	RamRead32A(0X0178, &temp);
	CAM_INFO(CAM_OIS, "0X0178 = 0x%0x", temp);
	RamRead32A(0X017C, &temp);
	CAM_INFO(CAM_OIS, "0X017C = 0x%0x", temp);
	RamRead32A(0X00D8, &temp);
	CAM_INFO(CAM_OIS, "0X00D8 = 0x%0x", temp);
	RamRead32A(0X0128, &temp);
	CAM_INFO(CAM_OIS, "0X0128 = 0x%0x", temp);
	return;
}

void CheckOISfwVersion(void) {
	uint32_t temp = 0x0;
	RamRead32A(0x8000, &temp);
	CAM_INFO(CAM_OIS, "OIS Version 0X8000 = 0x%0x", temp);
	RamRead32A(0x8004, &temp);
	CAM_INFO(CAM_OIS, "OIS Version 0X8004 = 0x%0x", temp);
	return;
}

