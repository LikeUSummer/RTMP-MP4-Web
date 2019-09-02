//武汉理工大学  刘俊杰  2019年5月
#include "Monitor.h"
#include <time.h>
#include <exception>

using namespace std;

int Monitor::storage_period = 0;
HANDLE Monitor::h_database_thread = NULL;
bool Monitor::exit_flag = false;
std::queue<VideoInfo> Monitor::video_info_list;
nanodbc::connection* Monitor::db_connection;
CRITICAL_SECTION Monitor::db_cs;
CRITICAL_SECTION Monitor::queue_cs;

Monitor::Monitor(const char* id)
{
	if (id)
		strcpy(m_dev_id, id);
	m_exit_flag = 0;
}

Monitor::~Monitor()
{
	m_exit_flag = 1;
	if (m_monitor_thread)
	{
		WaitForSingleObject(m_monitor_thread, INFINITE);
		CloseHandle(m_monitor_thread);
		printf("ID[%s]的处理线程已退出.\n", m_dev_id);
	}
}

void Monitor::set_input_url(char* input_url)
{
	if (input_url)
		strcpy(m_input_url, input_url);
}

DWORD _stdcall database_thread(LPVOID p);
void Monitor::init()
{
	storage_period = 0;
	exit_flag = false;
	//初始化数据库连接
	db_connection = new nanodbc::connection(NANODBC_TEXT(CONNECT_STR));
	//开启数据库存储线程
	h_database_thread = CreateThread(NULL, 0, database_thread, NULL, 0, NULL);
	//创建和数据库操作相关的资源临界区
	InitializeCriticalSection(&db_cs);
	InitializeCriticalSection(&queue_cs);
}

void Monitor::exit()
{
	exit_flag = true;
	printf("刷新数据库...\n");
	update_database();//强制刷新数据库，保存退出前的视频信息

	if (h_database_thread)
		TerminateThread(h_database_thread, 0);//此函数有风险，但我们通过一系列机制可以保证安全结束数据库线程
	
	CloseHandle(h_database_thread);
	DeleteCriticalSection(&db_cs);
	DeleteCriticalSection(&queue_cs);
	printf("数据库线程已退出.\n");
	delete db_connection;
}

//更新URL
void Monitor::get_input_url()
{
	EnterCriticalSection(&db_cs);
	nanodbc::statement statement(*db_connection);
	statement.prepare(NANODBC_TEXT(SELECT_URL_STR));
	statement.bind(0, (char*)&m_dev_id);
	nanodbc::result results = statement.execute();
	results.next();
	nanodbc::string url = results.get<nanodbc::string>(0);
	statement.close();
	strcpy(m_input_url, url.c_str());
	//printf(m_input_url);
	LeaveCriticalSection(&db_cs);
}

//获取设备在线状态
bool Monitor::get_online_state()
{
	EnterCriticalSection(&db_cs);
	nanodbc::statement statement(*db_connection);
	statement.prepare(NANODBC_TEXT(SELECT_ONLINE_STATE_STR));
	statement.bind(0, (char*)m_dev_id);
	nanodbc::result results = statement.execute();
	results.next();
	bool ret = results.get<int>(0);
	statement.close();
	LeaveCriticalSection(&db_cs);
	if (ret)
		return true;
	return false;
}

DWORD write_pipe(HANDLE h, char* data)
{
	DWORD n;
	WriteFile(
		h,
		data,
		strlen(data),
		&n,
		NULL
	);
	return n;
}

//RTMP转储MP4线程
DWORD WINAPI monitor_thread(LPVOID p)
{
	Monitor* m = (Monitor*)p;//传入的参数是Monitor对象指针
	//监听循环
	while (1)
	{
	detect:
		if (m->m_exit_flag)//优先判断退出信号
			goto end;

		if (m->get_online_state()==false)//如果设备不在线，每隔几秒尝试一次连接
		{
			Sleep(DETECT_DURATION * 1000);
			goto detect;
		}
		//m->get_input_url();//更新地址，如果确保运行期间rtmp不会改变，则无须实时更新
		//获取当前的日期与时刻，以此命名视频文件
		time_t time_stamp;
		time(&time_stamp);
		tm* t = localtime(&time_stamp);
		VideoInfo info = { (char*)(m->m_dev_id),time_stamp };//传递给存储队列的视频信息
		char output_url[512] = { 0 };
		sprintf(output_url, "%s%s-%d%02d%02d-%02d%02d%02d.mp4", VIDEO_STORAGE_DIR, m->m_dev_id, TIME_FORMAT_STR(t));
		char para[512] = { 0 };//传递给ffmpeg的命令行参数
		sprintf(para, " -i %s -c copy -f mp4 %s", m->m_input_url, output_url);
		
		//创建管道
		HANDLE hInputReadPipe, hInputWritePipe;
		HANDLE hOutputReadPipe, hOutputWritePipe;
		SECURITY_ATTRIBUTES saPipe;
		saPipe.nLength = sizeof(SECURITY_ATTRIBUTES);
		saPipe.lpSecurityDescriptor = NULL;
		saPipe.bInheritHandle = TRUE;
		
		if (!CreatePipe(&hInputReadPipe, &hInputWritePipe, &saPipe, 0))//新进程的输入管道
		{
			printf("创建管道失败...\n");
			goto detect;
		}
		if (!CreatePipe(&hOutputReadPipe, &hOutputWritePipe, &saPipe, 0))//新进程的输出管道
		{
			printf("创建管道失败...\n");
			goto detect;
		}

		//创建进程
		PROCESS_INFORMATION pi;
		STARTUPINFO si;
		memset(&si, 0, sizeof(si));
		si.hStdInput = hInputReadPipe;
		si.hStdOutput = hOutputWritePipe;
		si.dwFlags = STARTF_USESTDHANDLES;
		si.cb = sizeof(si);
		//si.wShowWindow = SW_SHOW;

		if (!CreateProcess("ffmpeg.exe",
			(LPSTR)para,
			NULL, NULL, 
			TRUE,//指示新进程是否从调用进程继承句柄，如果参数为真，调用进程中的每一个可继承的打开句柄都将被子进程继承，被继承的句柄与原进程拥有完全相同的值和访问权限
			0, //不使用新的控制台窗口
			NULL, NULL, &si, &pi))
		{
			printf("未能创建ffmpeg进程\n");
			return 0;
		}
		//创建子进程后句柄已被继承，为了安全应该关闭它们
		CloseHandle(hInputReadPipe);
		CloseHandle(hOutputWritePipe);

		char cmd[] = "q\n";//让ffmpeg结束的命令
		int counter = 0;
		while (1)
		{
			counter++;
			if (m->m_exit_flag)
				goto force_ffmpeg_close;
			DWORD ret = WaitForSingleObject(pi.hProcess, 1000);
			if (ret == WAIT_TIMEOUT) {
				if (counter == FILE_DURATION)//到达设定的录制时长，强制结束ffmpeg
					goto force_ffmpeg_close;
				continue;
			}
			//ffmpeg主动结束，可能是流终止或意外崩溃
			goto clear;
		}
force_ffmpeg_close:
		write_pipe(hInputWritePipe, cmd);//给ffmpeg发送命令字串，通知其结束录制程序
		WaitForSingleObject(pi.hProcess, INFINITE);
clear:
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		CloseHandle(hInputWritePipe);
		CloseHandle(hOutputReadPipe);

		//根据视频文件大小判断是保存还是丢弃
		WIN32_FIND_DATA file_info;
		HANDLE hFind;
		DWORD file_size=0;
		hFind = FindFirstFile(output_url, &file_info);
		if (hFind != INVALID_HANDLE_VALUE)
			file_size = file_info.nFileSizeLow;
		FindClose(hFind);
		if (file_size < MIN_FILE_SIZE)
			remove(output_url);
		else
		{
			//向存储队列push视频信息
			EnterCriticalSection(&Monitor::queue_cs);
			Monitor::video_info_list.push(info);
			LeaveCriticalSection(&Monitor::queue_cs);
		}
	}
end:
	m->m_exit_flag = 2;
	return 0;
}

//更新数据库信息
void Monitor::update_database()
{
	time_t time_stamp;
	tm* t = NULL;
	VideoInfo info;

	EnterCriticalSection(&db_cs);
	EnterCriticalSection(&queue_cs);
	nanodbc::statement statement;
	int n_wait = Monitor::video_info_list.size();
	//将队列中的视频信息存入数据库
	for (int i = 0; i < n_wait; i++)
	{
		//printf("更新数据库...\n");
		info = Monitor::video_info_list.front();
		t = localtime(&(info.time_stamp));

		char datetime[64] = { 0 };
		sprintf(datetime, "%d-%02d-%02d %02d:%02d:%02d", TIME_FORMAT_STR(t));
		char filename[64] = { 0 };
		sprintf(filename, "%s-%d%02d%02d-%02d%02d%02d.mp4", info.dev_id, TIME_FORMAT_STR(t));
		
		statement.open(*db_connection);
		statement.prepare(NANODBC_TEXT(INSERT_VIDEOINFO_STR));
		statement.bind(0, info.dev_id);
		statement.bind(1, datetime);
		statement.bind(2, filename);
		statement.execute();
		statement.close();
		
		Monitor::video_info_list.pop();
	}

	if (exit_flag)//收到退出信号时，避免碰巧执行下面的过程，因为此时线程即将结束，下面的数据操作可能不完整
	{
		return;
	}
	//检查数据库中是否有过期的数据，若有则删除对应记录和文件
	if (storage_period == CHECK_HISTORY_PERIOD)
	{
		time(&time_stamp);
		time_stamp -= VALIDITY_PERIOD * 24 * 60 * 60;
		t = localtime(&time_stamp);
		char datetime[64] = { 0 };
		sprintf(datetime, "%d-%02d-%02d %02d:%02d:%02d", TIME_FORMAT_STR(t));

		//查询数据库中过期视频的文件名
		statement.open(*db_connection);
		statement.prepare(NANODBC_TEXT(SELECT_FILENAME_STR));
		statement.bind(0, datetime);
		nanodbc::result results = statement.execute();
		//删除过期的文件
		while (results.next())
		{
			nanodbc::string name = results.get<nanodbc::string>(0);
			char filename[512] = VIDEO_STORAGE_DIR;
			strcat(filename, name.c_str());
			remove(filename);
		}
		statement.close();
		statement.open(*Monitor::db_connection);
		//删除数据库中过期的数据
		statement.prepare(NANODBC_TEXT(DELETE_VIDEOINFO_STR));
		statement.bind(0, datetime);
		statement.execute();
		statement.close();
		storage_period = 0;
	}
	storage_period++;
	LeaveCriticalSection(&queue_cs);
	LeaveCriticalSection(&db_cs);
}

//数据库操作线程
DWORD _stdcall database_thread(LPVOID p)
{
	while (1)
	{
		Monitor::update_database();
		Sleep(UPDATE_DATA_DURATION * 60 * 1000);
	}
	return 0;
}

void Monitor::run()
{
	m_exit_flag = 0;
	m_monitor_thread = CreateThread(NULL, 0, monitor_thread, this, 0, NULL);
}
