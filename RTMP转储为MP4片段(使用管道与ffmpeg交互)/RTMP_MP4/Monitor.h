//武汉理工大学  刘俊杰  2019年5月
#pragma once
#include <stdio.h>
#include <queue>
#include <windows.h>
#include "nanodbc.h"

//参数配置
#define DETECT_DURATION 3 //测试监控设备在线状态的时间间隔(秒)
#define FILE_DURATION 120 //每个视频文件的时长(秒)
#define MIN_FILE_SIZE 200*1024  //最小视频文件尺寸(字节)，小于此尺寸的视频将被当作出错视频删掉
#define UPDATE_DATA_DURATION 1 //更新数据库信息的间隔(分钟)
#define CHECK_HISTORY_PERIOD 200  //检查并删除过期数据的周期(以UPDATE_DATA_DURATION为单位)
#define VALIDITY_PERIOD 90  //数据有效期(天)，过期数据将被清理
#define VIDEO_STORAGE_DIR "D:/Website/Video/"  //视频存储目录，注意最后的斜杠不能省略

#define CONNECT_STR "Driver={sql server};server=localhost;database=YourDB;uid=sa;pwd=YourPSW;" //数据库连接字
#define SELECT_ID_STR  "select SN from Cameras where VideoSave=1;" //查询需要进行转储的录像设备的序列号
#define SELECT_URL_STR "select RTMP from Cameras where SN=?;" //查询指定设备的RTMP地址
#define SELECT_ONLINE_STATE_STR  "select OnlineState from Cameras where SN=?;" //查询指定设备的在线状态
#define INSERT_VIDEOINFO_STR "insert into Videos values(?,?,?);" //向视频表插入一条记录
#define SELECT_FILENAME_STR "select FileName from Videos where VideoTime<?;" //查询指定时间以前的视频记录
#define DELETE_VIDEOINFO_STR  "delete from Videos where VideoTime<?;" //删除指定时间以前的视频记录

//工具宏
#define TIME_FORMAT_STR(x) x->tm_year + 1900,x->tm_mon+1,x->tm_mday,x->tm_hour,x->tm_min,x->tm_sec
#define WAIT_WHILE(x) while(x)Sleep(50);

//错误状态码
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

//提交给存储队列的视频信息
struct VideoInfo
{
	char* dev_id;
	time_t time_stamp;
};

class Monitor
{
public:
	char m_dev_id[32] = {0};//对应的远程设备ID，仅用于数据库存储
	char m_input_url[256] = { 0 };//输入文件的URL
	unsigned char m_exit_flag;//监控线程退出信号，0是运行状态，1是通知退出信号，2是已退出信号
	//char m_last_online_state;//上一次检查时的设备在线状态
	void* m_monitor_thread;//监控线程句柄

	static std::queue<VideoInfo> video_info_list;//存储队列，由多线程共享
	static nanodbc::connection* db_connection;//数据库线程操作的odbc连接对象
	//static HANDLE db_mutex;//数据库对象的互斥锁，目前使用临界区来替代
	static CRITICAL_SECTION db_cs;//数据库资源临界区
	static CRITICAL_SECTION queue_cs;//队列资源临界区
	static int storage_period;//数据库线程更新循环的计数，CHECK_HISTORY_PERIOD次循环后检查一次过期数据
	static HANDLE h_database_thread;//数据库线程句柄
	static bool exit_flag;//全局退出信号

public:
	Monitor(const char* id);
	~Monitor();

	static void init();//初始化
	static void exit();//收尾时调用

	void set_input_url(char* input_url);
	static void update_database();//由存储队列数据更新数据库并检查过期数据
	bool get_online_state();//从数据库获取设备的在线信息
	void get_input_url();//从数据库更新rtmp地址

	void run();//启动监控线程
};
