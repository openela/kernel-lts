#ifndef _OPPO_PROJECT_H_
#define _OPPO_PROJECT_H_

#define MAX_OCP 6
#define MAX_LEN 8

#define ALIGN4(s) ((sizeof(s) + 3)&(~0x3))

#define FEATURE1_FOREIGN_MASK	(1 << 0)

typedef struct
{
	uint32_t	nProject;
	uint32_t	nDtsi;
	uint32_t	nAudio;
	uint8_t		nRF;
	uint32_t	nFeature1;
	uint32_t	nFeature2;
	uint32_t	nFeature3;
	uint8_t		nOppoBootMode;
	char		nPCB[MAX_LEN];
	uint8_t		nPmicOcp[MAX_OCP];
	uint8_t		reserved[16];
} ProjectInfoCDTType;

enum OPPO_ENG_VERSION {
    RELEASE                 = 0x00,
    AGING                   = 0x01,
    CTA                     = 0x02,
    PERFORMANCE             = 0x03,
    PREVERSION              = 0x04,
    ALL_NET_CMCC_TEST       = 0x05,
    ALL_NET_CMCC_FIELD      = 0x06,
    ALL_NET_CU_TEST         = 0x07,
    ALL_NET_CU_FIELD        = 0x08,
    ALL_NET_CT_TEST         = 0x09,
    ALL_NET_CT_FIELD        = 0x0A,
};

typedef struct
{
  uint32_t   version;
  uint32_t   is_confidential;
} EngInfoType;

unsigned int get_project(void);
unsigned const char * get_PCB_Version(void);
int cmp_pcb(const char *pcb);
int get_eng_version(void);
bool is_confidential(void);
uint32_t get_oppo_feature1(void);
uint32_t get_oppo_feature2(void);
uint32_t get_oppo_feature3(void);
unsigned int get_audio_project(void);

#endif
