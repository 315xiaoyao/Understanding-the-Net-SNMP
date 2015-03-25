/************************************************************
 * Copyright (C)	GPL
 * FileName:		snmpipc.c
 * Author:		�Ŵ�ǿ
 * Date:			2014-08
 * gcc -g -Wall -fPIC -shared snmpipc.c -o libsnmpipc.so
 ***********************************************************/
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include "snmpipc.h"

static int update_shm_data( int dType,T_ShmCellVal *pValue, int diretion );

// union semun {
// int val; /* value for SETVAL */
// struct semid_ds *buf; /* buffer for IPC_STAT, IPC_SET */
// unsigned short *array; /* array for GETALL, SETALL */
// struct seminfo *__buf; /* buffer for IPC_INFO */
// };

// �ź���ID
static int s_SemId;
static int s_ShmId;


// �����ֹ�ά��,�ṹ���ӳ���
static T_MapTable s_tParaMapTable[] =
{
	{ .iNo = PARA_A, .iOffset = offsetof( T_ParaData, a ),                    .iLen = sizeof( int ) },
	{ .iNo = PARA_B, .iOffset = offsetof( T_ParaData, b ),                    .iLen = MAX_CHAR_LEN  },
	{ .iNo = PARA_C1, .iOffset = offsetof( T_ParaData, c ),                   .iLen = sizeof( int ) },
	{ .iNo = PARA_C2, .iOffset = offsetof( T_ParaData, c ) + sizeof( int ),   .iLen = sizeof( int ) },
	{ .iNo = PARA_C3, .iOffset = offsetof( T_ParaData, c ) + 2 * sizeof( int ), .iLen = sizeof( int ) },
};
#define PARA_NUM ( sizeof( s_tParaMapTable ) / sizeof( T_MapTable ) )

static T_MapTable s_tRealDtMapTable[] =
{
	{ .iNo = XY0X, .iOffset = offsetof( T_RealData, xy[0].x ),      .iLen = sizeof( int ) },
	{ .iNo = XY0Y, .iOffset = offsetof( T_RealData, xy[0].y ),       .iLen = sizeof( int ) },
	{ .iNo = XY1X, .iOffset = offsetof( T_RealData, xy[1].x ), .iLen = sizeof( int ) },
	{ .iNo = XY1Y, .iOffset = offsetof( T_RealData, xy[1].y ), .iLen = sizeof( int ) },
	{ .iNo = REALZ, .iOffset = offsetof( T_RealData, z ), .iLen = sizeof( int ) },
};
#define REALDATA_NUM ( sizeof( s_tRealDtMapTable ) / sizeof( T_MapTable ) )

static T_MapTable s_tAlarmDtMapTable[] =
{
	{ .iNo = ALARM1, .iOffset = offsetof( T_AlarmData, alarm1 ),    .iLen = sizeof( int ) },
	{ .iNo = ALARM2, .iOffset = offsetof( T_AlarmData, alarm2 ),     .iLen =  MAX_CHAR_LEN },
	{ .iNo = ALARM_COUNTER, .iOffset = offsetof( T_AlarmData, alarmCounter ), .iLen = sizeof( int ) },
};
#define ALARMDATA_NUM ( sizeof( s_tAlarmDtMapTable ) / sizeof( T_MapTable ) )


// ��������С����
static int s_acMaxObjNum[SHM_TYPE_NUM] =
{
	PARA_NUM,
	REALDATA_NUM,
	ALARMDATA_NUM,
	0,
};

// �����ڴ��С���ַ
static T_ShareMem s_atShareMem[] =
{
	{ .iSize = PARA_NUM * sizeof( T_ShmCellVal ), .pShmAddr = NULL },
	{ .iSize = REALDATA_NUM * sizeof( int ),          .pShmAddr = NULL },
	{ .iSize = ALARMDATA_NUM * sizeof( int ),         .pShmAddr = NULL },
};

#define SHM_ARRAY_SIZE  sizeof( s_atShareMem ) / sizeof( T_ShareMem )
#define SHM_CELL_SIZE   sizeof( T_ShmCellVal )

static void check_file_exist( char *pName );
static int get_maxobj_num(int dType);
static void update_para_data(T_ParaData* pData, int direction);
static void update_realtime_data(T_RealData* pData, int direction);
/***********************************************************
* Description:    ����dType���������ж��������
***********************************************************/
static int get_maxobj_num(int dType)
{
	if( ( dType >= SHM_PARADATA)
	    && (dType < SHM_TYPE_NUM )
	    )
		return s_acMaxObjNum[dType];

	return FAILURE;
}

/***********************************************************
* Description:    ��ȡdType��������MAP�ڴ����ʼ��ַ
***********************************************************/
T_MapTable* get_maptable(int dType)
{
	T_MapTable * pMapData =  NULL;
	if( dType == SHM_PARADATA )
	{
		pMapData = s_tParaMapTable;

	}else if( dType == SHM_REALDATA )
	{
		pMapData = s_tRealDtMapTable;
	}else if( dType == SHM_ALARM )
	{
		pMapData = s_tAlarmDtMapTable;
	}
	return pMapData;
}

/***********************************************************
* Description:    ���ؿ����ĳ���
***********************************************************/
static int app_memcpy( void* pValInner, void* pShmVal, int iLen, int diretion )
{
	if( NULL == pValInner || NULL == pShmVal || iLen <= 0 )
		return 0;

	if( TO_SHM == diretion )            // ���õ������ڴ�
	{
		memcpy( pShmVal, pValInner, iLen );
	} else if( FROM_SHM == diretion )   // �ӹ����ڴ�ȡ
	{
		memcpy( pValInner, pShmVal, iLen );
	} else
	{
		return 0;
	}
	return iLen;
}

/***********************************************************
* Description:    ����ļ��Ƿ���ڣ��������򴴽�
***********************************************************/
static void check_file_exist( char *pName )
{
	FILE *pF;
	if( access( pName, F_OK ) < 0 )
	{
		pF = fopen( pName, "w" );
		if( pF )
		{
			///����д��·�������ļ���
			fwrite( pName, strlen( pName ), 1, pF );
			fclose( pF );
		}else
		{
			printf( "check_file_exist: %s  failed!!", pName );
		}
	}
	return;
}


static int get_shm_size()
{
	int i =0,j=0;
	// ���㹲���ڴ��ܴ�С
	for( i = 0; i < SHM_ARRAY_SIZE; i++ )
		j += j + s_atShareMem[i].iSize;
	return j;
}

static int get_shm_key()
{
	char acFile[256] = { 0 };
	int iShmKey;
	// ���ļ����ڻ�ȡ�����ڴ��key
	sprintf( acFile, "%s%s", APP_DIR, SHM_CONF );
	check_file_exist( acFile );
	// ���keyֵ
	iShmKey = ftok( acFile, SHM_KEY_ID );
	if( iShmKey == (key_t)-1 )
	{
		printf( "get_share_memory:ftok() for shm failed!!\n" );
		return FAILURE;
	}
	printf( "ftok return key = 0x%x\n", iShmKey );
	return iShmKey;
}

static int shmget_create(int iShmKey,int iShmSize)
{

	if( iShmKey == FAILURE|| iShmSize <=0)
		return FAILURE;

	// ���������û��ɶ�д�Ĺ����ڴ�
	s_ShmId = shmget( iShmKey, iShmSize, 0666 | IPC_CREAT );
	if( -1 == s_ShmId )
	{
		printf( "get_share_memory:shmget() failed!!\n" );
		return FAILURE;
	}
	return SUCCESS;
}

static int shmget_get(int iShmKey,int iShmSize)
{

	if( iShmKey == FAILURE|| iShmSize <=0)
		return FAILURE;

	// ��ȡ�����ڴ�
	s_ShmId = shmget( iShmKey, iShmSize, 0666 );
	if( -1 == s_ShmId )
	{
		printf( "get_share_memory:shmget() failed!!\n" );
		return FAILURE;
	}
	return SUCCESS;
}

static void allocate_shm( void*pShmAddr )
{
	int i =0;
	if(NULL == pShmAddr)
	{
		printf("NULL ADDRESS!!\n");
		return;
	}
	// Ϊ�������ݷ��乲���ڴ���ʼ��ַ
	for( i = 0; i < SHM_ARRAY_SIZE; i++ )
	{
		s_atShareMem[i].pShmAddr = pShmAddr;
		printf( "shm adress:0x%x\n", (unsigned int )s_atShareMem[i].pShmAddr );
		pShmAddr = (void*)( (int)pShmAddr + s_atShareMem[i].iSize );
	}
	return;
}

/***********************************************************
* Description:   ��ȡ�����ڴ��ID
***********************************************************/
static int get_shmid(  )
{
	return s_ShmId;
}

/***********************************************************
* Description:   ��ȡ�ź�����ID
***********************************************************/
static int get_semid(  )
{
	return s_SemId;
}

void    *shm_attach( )
{
	void    *pShmAddr       = NULL;
	// ���һ������Ϊ0 ��ʾ�ɶ�д
	pShmAddr = shmat( get_shmid(), NULL, 0 );
	if( NULL == pShmAddr )
	{
		del_shm();
		printf( "main:shmat() failed!!\n" );
		return NULL;
	}
	return pShmAddr;
}
/***********************************************************
* Description:
   bIsMasterΪ�洴�������ڴ棬�����ȡ�����ڴ��ID��
   ��Ϊ�����⣬�ú���ֻҪ��ҵ����̵���һ�ξ�OK�ˣ�
   �������bIsMasterΪ1��SNMP����ɲ��õ��øýӿڣ�
***********************************************************/
static int init_share_memory( BOOLEAN bIsMaster )
{
	void    *pShmAddr       = NULL;

	if( bIsMaster )
	{
		// ���������û��ɶ�д�Ĺ����ڴ�
		if(FAILURE == shmget_create(get_shm_key(),get_shm_size()) )
			return FAILURE;
	}else
	{
		// ��ȡ�����ڴ�
		if(FAILURE == shmget_get(get_shm_key(),get_shm_size()) )
			return FAILURE;
	}
	printf( "shm id = %d \n", get_shmid() );

	pShmAddr = shm_attach();
	if(NULL == pShmAddr) return FAILURE;

	if( bIsMaster ) //�����ڴ���ʾ��ʼ��,������!
		memset( pShmAddr, 0x00, get_shm_size() );

	// Ϊ�������ݷ��乲���ڴ���ʼ��ַ
	allocate_shm(pShmAddr);

	return get_shmid();
}

/***********************************************************
* Description:    ��ȡ�����ڴ��ַ
***********************************************************/
static void * get_shm_addr( int dtype )
{
	if( 0 <= dtype && dtype < SHM_TYPE_NUM )
		return	   s_atShareMem[dtype].pShmAddr;

	return NULL;
}


static int get_sem_key()
{
	char acFile[256] = { 0 };
	int iSemKey =-1;
	// ���ļ����ڻ�ȡ�����ڴ��key
	sprintf( acFile, "%s%s", APP_DIR, SEM_CONF );
	check_file_exist( acFile );
	iSemKey = ftok( acFile, SEM_KEY_ID );

	if( iSemKey == (key_t)-1 )
	{
		printf( "get_sem_key:ftok() for sem failed!!\n" );
		return FAILURE;
	}
	printf( "ftok return key = 0x%x\n", iSemKey );
	return iSemKey;
}

static int semget_create( int iSemKey )
{
	int i =0;
	if( iSemKey == FAILURE )
		return FAILURE;

	// ���������û��ɶ�д���ź���
	s_SemId = semget( iSemKey, SEM_NUM, 0666 | IPC_CREAT );
	if( -1 == s_SemId )
	{
		printf( "semget_create:semget() failed!!\n" );
		return FAILURE;
	}
	// ��ʼ���ź�����
	//ʹ��forѭ����SETVAL��������ÿ���ź���ֵΪ1�����ɻ��
	for( i = 0; i < SEM_NUM; i++ )
	{
		if( semctl( s_SemId, i, SETVAL, 1 ) < 0 )
		{
			return FAILURE;
		}
	}
	return s_SemId;
}

static int semget_get( int iSemKey )
{

	if( iSemKey == FAILURE )
		return FAILURE;

	s_SemId = semget( iSemKey, SEM_NUM, 0666 );
	if( -1 == s_SemId )
	{
		printf( "semget_get:semget() failed!!\n" );
		return FAILURE;
	}
	return s_SemId;
}


/***********************************************************
* Description: �����ź���
   bIsMaster Ϊ�洴���������ȡ
***********************************************************/
static int create_semaphore( BOOLEAN bIsMaster )
{
	if( bIsMaster )
		// ���������û��ɶ�д���ź���
		semget_create( get_sem_key() );
	else
		semget_get( get_sem_key());

	printf( "s_SemId=%d \n", get_semid() );
	return get_semid();
}

/***********************************************************
* Description:    ɾ�������ڴ�
***********************************************************/
int  del_shm( )
{
	int rc;
	rc = shmctl( s_ShmId, IPC_RMID, NULL );
	if( FAILURE == rc )
	{
		printf( "del_shm: shmctl() failed!!\n" );
		return FAILURE;
	}
	return SUCCESS;
}

/***********************************************************
* Description:   �Ե����ź���V�������ͷ���Դ;
***********************************************************/
static int unlock_sem( int slNo )
{
	struct sembuf tSem;
	tSem.sem_num    = slNo;
	tSem.sem_op             = 1;
	tSem.sem_flg    = SEM_UNDO;
	return
	        semop( s_SemId, &tSem, 1 );
}

/***********************************************************
* Description:
   �Ե����ź���P������ȡ����Դ
   ʹ��SEM_UNDOѡ���ֹ�ý����쳣�˳�ʱ���ܵ�������
***********************************************************/
static int lock_sem( int slNo )
{
	struct sembuf tSem;
	tSem.sem_num    = slNo;
	tSem.sem_op             = -1;
	tSem.sem_flg    = SEM_UNDO;
	return
	        semop( s_SemId, &tSem, 1 );
}

/***********************************************************
* Description:   ��ȡ�ź�����ֵ
***********************************************************/
static int get_sem( int slNo )
{
	return
	        semctl( s_SemId, 0, GETVAL );
}

/***********************************************************
* Description:   ɾ���ź���
***********************************************************/
int del_sem( void )
{
	int rc;
	rc = semctl( s_SemId, 0, IPC_RMID );
	if( rc == -1 )
	{
		perror( "del_sem:semctl() remove id failed!!\n" );
		return FAILURE;
	}
	return SUCCESS;
}

static void set_shmcellval(T_ShmCellVal*        shmVal,int no,int ll)
{
	if(NULL != shmVal)
	{
		bzero( (void*)shmVal, (size_t)sizeof( T_ShmCellVal ) );
		shmVal->iNo     = no;
		shmVal->iLen = ll;
	}
	return;
}

/***********************************************************
* Description:
    SNMP����ʹ�õ����ݸ��½ӿڣ�
    �������ݣ����ظ��µ��ֽ�����
    ʵ���ϣ���Щ��ʼ���Ĺ��������Բ��ô�����ʵ�֣�
    ��������ʵ����Ŀ�У����������ᶨ��������Ҫ�ļ���
    ����������޷Ǿ��Ǹ�������������ԣ�
    ���������ͣ��ֽڳ��ȣ���дȨ�ޣ�����������ȱʡֵ�ȵȡ�
    ��ݶ����ļ����ǿ��Գ�֮Ϊϵͳ��ض���������ֵ䡣
    ����ֻҪ�����������ֵ�����һ��������нṹ��һ�µĶ����Ƶ��ļ���
    ÿ�γ�ʼ��ʱֻҪֱ�Ӷ�ȡ���ļ������ݵ������ڴ�Σ��������ϵͳ�ĳ�ʼ���Ĺ�����
***********************************************************/
static int  updata_cellvalue( int dType, int no, int ll, void* pV, int diretion )
{
	T_ShmCellVal shmVal;
	int ret = 0;
	if( no < 0
	    || ll < 0
	    || NULL == pV
	    || ( dType < SHM_PARADATA || dType >= SHM_TYPE_NUM )
	    )
	{
		printf( "updata_cellvalue failed!!\n" );
		return 0;
	}

	set_shmcellval(&shmVal,no,ll);

	// ���û��ȡ
	// ����ʱ: pV -> shmVal.uValue ,��ȡʱ�������
	if( TO_SHM == diretion )
		app_memcpy( pV, &shmVal.uValue, ll, diretion );

	ret = update_shm_data( dType, &shmVal, diretion );

	// ��ȡʱ: shmVal.uValue -> pv,����ʱ�������
	if( (FROM_SHM == diretion) && (0 < ret) )
		ret = app_memcpy( pV, &shmVal.uValue, ret, diretion );

	return ret;
}


static void* get_update_addr(int dType,const T_ShmCellVal *pValue)
{

	if( NULL == pValue )		return NULL;

	char    *pShmAddr       = (char*)get_shm_addr( dType );
	T_MapTable   *pMapTable= get_maptable( dType );
	
	if(NULL != pMapTable)
	{     // ȡָ��λ�õĵ�Ԫ��ַ
		pShmAddr += pMapTable[pValue->iNo].iOffset;
		return (void*)pShmAddr;
	}else
		return NULL;
}

static int get_update_len(int dType,const T_ShmCellVal *pValue)
{
	if( NULL == pValue ) return 0;

	T_MapTable   *pMapTable= get_maptable( dType );

	if(NULL != pMapTable)
		return MIN( pMapTable[pValue->iNo].iLen, pValue->iLen );
	else
		return 0;
}

/***********************************************************
* Description:
        SNMP����ʹ�õ����ݸ��½ӿڣ�
        pValue �������T_ShmCellVal ָ��,��Ϊ���������
        ���ؿ����ĳ���;
***********************************************************/
static int update_shm_data( int dType, T_ShmCellVal *pValue, int diretion )
{

	void * pShmAddr = get_update_addr(dType,pValue);
	int	  iLen         = get_update_len(dType,pValue);
	
	if( NULL == pShmAddr ||iLen <=0 ) return 0;

	lock_sem( dType );
		app_memcpy( &pValue->uValue, pShmAddr, iLen, diretion );
	unlock_sem( dType );

	return iLen;
}


/*
   �����������ݣ�
   ����Ӳ������ֻ�м������ݵĲ����ṹ��
   ʵ���У�һ��ϵͳ�Ĳ���ԶԶ��ֻ��Щ�������ʹ�����ַ�ʽ���϶�����һ���õĽ��������
   һ������£����ڶ�����ݵĴ���ʽ����ı�����ʽ��ʹ��ѭ����
   ��ʹ��ѭ����ȻҪ������е��������͵Ĵ���ʽ��һ����
   ��������˼·������ı��뷽ʽ�Ƕ���ͨ�õĽṹ������Ӷ���Ŀɹ�ѭ��ͳһ�������Ϣ����Ψһ��ʶ�ķ�����
   �������ݵĴ����Ϊ����ı��뼼���ǽ������Ϊ����ֵ�����ԡ���ʶ��
   �������ֲַ��˼�룬���е����ݶ�����Ϊͳһ�Ĵ������ˣ�
   ����Ͳ������ˣ����ϻ����ƺ������ˣ�
 */

/***********************************************************
* Description:   ����ͨ�ýṹ���빲���ڴ������
***********************************************************/
static void _update_data(void* pData, int dType,int direction)
{
	char *pAddr = NULL;
	int i = 0;
	T_MapTable *pMap = get_maptable( dType );
	if( NULL == pMap || NULL == pData ) return;

	for(i = 0; i < get_maxobj_num(dType); i++ )
	{
		pAddr = (char *)pData;
		updata_cellvalue( dType,i,pMap[i].iLen,
		                  pAddr+pMap[i].iOffset, direction);
	}

	return;
}

/***********************************************************
* Description:   �����ṹ������빲���ڴ����ݸ���
***********************************************************/
static void update_para_data(T_ParaData* pData, int direction)
{
	return
	        _update_data(pData,SHM_PARADATA,direction);

}

/***********************************************************
* Description:   ʵʱ���ݽṹ������빲���ڴ����ݸ���
***********************************************************/
static void update_realtime_data(T_RealData* pData, int direction)
{

	void * pShmVal = get_shm_addr( SHM_REALDATA );
	if( NULL == pShmVal || NULL == pData ) return;

	lock_sem( SHM_REALDATA );
	app_memcpy( pData, pShmVal, sizeof(T_RealData),direction);
	unlock_sem( SHM_REALDATA );

	return;
}

/***********************************************************
* Description:  �澯���ݽṹ������빲���ڴ����ݸ���
***********************************************************/
static void update_alarm_data(T_AlarmData* pData, int direction)
{
	return
	        _update_data(pData,SHM_ALARM,direction);
}


/***********************************************************
* Description:   ҵ�����ʹ��: �ṹ������빲���ڴ����ݸ��£�
   pStrVal,Ϊҵ������нṹ�������
   dir,ȡֵΪFROM_SHM��TO_SHM,�ֱ��ʾ��д�����ڴ棻
***********************************************************/
static int update_data(void* pStrVal, int dType, int dir)
{
	if( NULL == pStrVal ) return FAILURE;

	if( SHM_PARADATA == dType)
		update_para_data( pStrVal, dir );
	else if( SHM_REALDATA == dType )
		update_realtime_data(pStrVal, dir);
	else if( SHM_ALARM== dType )
		update_alarm_data(pStrVal, dir);
	else
		return FAILURE;

	return SUCCESS;
}

/***********************************************************
* Description:   ҵ�����ʹ��:
   �ӹ����ڴ��ж����ݸ��µ��ṹ��
   pStrVal,Ϊҵ������нṹ�����ָ��;
   dType:��������
***********************************************************/
int app_get_data(void* pStrVal, int dType)
{
	return
	        update_data(pStrVal, dType,FROM_SHM);
}

/***********************************************************
* Description:   ҵ�����ʹ��:
   �ӽṹ��д�������ڴ���
   pStrVal,Ϊҵ������нṹ�����ָ��;
   dType:��������
***********************************************************/
int app_set_data(void* pStrVal, int dType)
{
	return
	        update_data(pStrVal, dType,TO_SHM);
}


/***********************************************************
* Description:   snmp����ʹ��:
   �ӹ����ڴ��ж����ݵ�pV
   dType:��������
   no:���
   ll:�ֽڳ���
   pV:������ַ
***********************************************************/
int snmp_get_data( int dType, int no, int ll, void* pV )
{
	return
	        updata_cellvalue(dType,no,ll,pV,FROM_SHM);
}

/***********************************************************
* Description:   snmp����ʹ��:
   ��pV����д�뵽�����ڴ���
   dType:��������
   no:���
   ll:�ֽڳ���
   pV:������ַ
***********************************************************/
int snmp_set_data( int dType, int no, int ll, void* pV )
{
	return
	        updata_cellvalue(dType,no,ll,pV,TO_SHM);
}


/***********************************************************
* Description:�����ڴ���ź�����ʼ��
   isMasterָʾ�Ƿ�Ϊ��ҵ����̣��ý��̸��𴴽��ͳ�ʼ��
***********************************************************/
static void init_shm_sem(BOOLEAN isMaster)
{
	if( FAILURE == init_share_memory( isMaster ) )
		exit (-1);

	if( FAILURE == create_semaphore( isMaster ) )
		exit (-1);
}


/***********************************************************
* Description:������ ���ó�ʼ�������ڴ���ź���
***********************************************************/

void init_shm_sem_master()
{
	init_shm_sem(1);
}

/***********************************************************
* Description:�������� ���ó�ʼ�������ڴ���ź���
***********************************************************/
void init_shm_sem_slave()
{
	init_shm_sem(0);
}

