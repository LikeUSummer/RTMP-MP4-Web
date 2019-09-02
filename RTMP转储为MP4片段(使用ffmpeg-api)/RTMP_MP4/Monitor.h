//�人����ѧ  ������  2019��5��
#pragma once
#include <stdio.h>
#include <queue>
#include <windows.h>
#include "nanodbc.h"

//#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
};
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"avcodec.lib")
#else
//Linux
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#ifdef __cplusplus
};
#endif
#endif

//��������
#define DETECT_DURATION 3 //���Լ���豸����״̬��ʱ����(��)
#define FILE_DURATION 120 //ÿ����Ƶ�ļ���ʱ��(��)
#define MIN_FILE_SIZE 200*1024  //��С��Ƶ�ļ��ߴ�(�ֽ�)��С�ڴ˳ߴ����Ƶ��������������Ƶɾ��
#define UPDATE_DATA_DURATION 1 //�������ݿ���Ϣ�ļ��(����)
#define CHECK_HISTORY_PERIOD 200  //��鲢ɾ���������ݵ�����(��UPDATE_DATA_DURATIONΪ��λ)
#define VALIDITY_PERIOD 90  //������Ч��(��)���������ݽ�������
#define VIDEO_STORAGE_DIR "D:/Website/Video/"  //��Ƶ�洢Ŀ¼��ע������б�ܲ���ʡ��

#define CONNECT_STR "Driver={sql server};server=localhost;database=YourDB;uid=sa;pwd=YourPSW;" //���ݿ�������
#define SELECT_ID_STR  "select SN from Cameras where VideoSave=1;" //��ѯ��Ҫ����ת����¼���豸�����к�
#define SELECT_URL_STR "select RTMP from Cameras where SN=?;" //��ѯָ���豸��RTMP��ַ
#define SELECT_ONLINE_STATE_STR  "select OnlineState from Cameras where SN=?;" //��ѯָ���豸������״̬
#define INSERT_VIDEOINFO_STR "insert into Videos values(?,?,?);" //����Ƶ�����һ����¼
#define SELECT_FILENAME_STR "select FileName from Videos where VideoTime<?;" //��ѯָ��ʱ����ǰ����Ƶ��¼
#define DELETE_VIDEOINFO_STR  "delete from Videos where VideoTime<?;" //ɾ��ָ��ʱ����ǰ����Ƶ��¼

//���ߺ�
#define TIME_FORMAT_STR(x) x->tm_year + 1900,x->tm_mon+1,x->tm_mday,x->tm_hour,x->tm_min,x->tm_sec
#define WAIT_WHILE(x) while(x)Sleep(50);

//����״̬��
enum Errors
{
	ERROR_NO_ERROR,
	ERROR_NO_STREAM,
	ERROR_NO_INPUT,
	ERROR_NO_HEARDER,
	ERROR_WRITE_HEADER,
	ERROR_WRITE_FRAME,
	ERROR_OPEN_OUTPUT,
	ERROR_OTHER
};

//�ύ���洢���е���Ƶ��Ϣ
struct VideoInfo
{
	char* dev_id;
	time_t time_stamp;
};

class Monitor
{
private:
	AVFormatContext *m_ifmt_ctx, *m_ofmt_ctx;

public:
	char m_dev_id[32] = { 0 };//��Ӧ��Զ���豸ID�����������ݿ�洢
	char m_input_url[256] = { 0 };//�����ļ���URL
	unsigned char m_exit_flag;//����߳��˳��źţ�0������״̬��1��֪ͨ�˳��źţ�2�����˳��ź�
	//char m_last_online_state;//��һ�μ��ʱ���豸����״̬
	void* m_monitor_thread;//����߳̾��

	static std::queue<VideoInfo> video_info_list;//����洢����
	static nanodbc::connection* db_connection;//���ݿ��̲߳�����odbc���Ӷ���
	//static HANDLE db_mutex;//���ݿ����Ļ�������Ŀǰʹ���ٽ��������
	static CRITICAL_SECTION db_cs;//���ݿ���Դ�ٽ���
	static CRITICAL_SECTION queue_cs;//������Դ�ٽ���
	static int storage_period;//���ݿ��̸߳���ѭ���ļ�����CHECK_HISTORY_PERIOD��ѭ������һ�ι�������
	static HANDLE h_database_thread;//���ݿ��߳̾��
	static bool exit_flag;//ȫ���˳��ź�

public:
	Monitor(const char* id);
	~Monitor();
	void set_input_url(char* input_url);

	static void init();//��ʼ��
	static void exit();//��βʱ����
	static void update_database();//�ɴ洢�������ݸ������ݿⲢ����������

	bool get_online_state();//�����豸��������Ϣ
	void get_input_url();//�����ݿ����rtmp��ַ

	int open_input();//��������
	void close_input();//�ر�������
	int open_output(char* output_url);//�������������MP4�ļ�
	void close_output();//д�ļ�β���ر������
	int write_frame();//дһ֡����
	void write_trailer();//д�ļ�β

	void run();//��������߳�
};
