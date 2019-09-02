//�人����ѧ  ������  2019��5��
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
		printf("ID[%s]�Ĵ����߳����˳�.\n", m_dev_id);
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
	//��ʼ�����ݿ�����
	db_connection = new nanodbc::connection(NANODBC_TEXT(CONNECT_STR));
	//�������ݿ�洢�߳�
	h_database_thread = CreateThread(NULL, 0, database_thread, NULL, 0, NULL);
	//���������ݿ������ص���Դ�ٽ���
	InitializeCriticalSection(&db_cs);
	InitializeCriticalSection(&queue_cs);
}

void Monitor::exit()
{
	exit_flag = true;
	printf("ˢ�����ݿ�...\n");
	update_database();//ǿ��ˢ�����ݿ⣬�����˳�ǰ����Ƶ��Ϣ

	if (h_database_thread)
		TerminateThread(h_database_thread, 0);//�˺����з��գ�������ͨ��һϵ�л��ƿ��Ա�֤��ȫ�������ݿ��߳�
	
	CloseHandle(h_database_thread);
	DeleteCriticalSection(&db_cs);
	DeleteCriticalSection(&queue_cs);
	printf("���ݿ��߳����˳�.\n");
	delete db_connection;
}

//����URL
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

//��ȡ�豸����״̬
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

//RTMPת��MP4�߳�
DWORD WINAPI monitor_thread(LPVOID p)
{
	Monitor* m = (Monitor*)p;//����Ĳ�����Monitor����ָ��
	//����ѭ��
	while (1)
	{
	detect:
		if (m->m_exit_flag)//�����ж��˳��ź�
			goto end;

		if (m->get_online_state()==false)//����豸�����ߣ�ÿ�����볢��һ������
		{
			Sleep(DETECT_DURATION * 1000);
			goto detect;
		}
		//m->get_input_url();//���µ�ַ�����ȷ�������ڼ�rtmp����ı䣬������ʵʱ����
		//��ȡ��ǰ��������ʱ�̣��Դ�������Ƶ�ļ�
		time_t time_stamp;
		time(&time_stamp);
		tm* t = localtime(&time_stamp);
		VideoInfo info = { (char*)(m->m_dev_id),time_stamp };//���ݸ��洢���е���Ƶ��Ϣ
		char output_url[512] = { 0 };
		sprintf(output_url, "%s%s-%d%02d%02d-%02d%02d%02d.mp4", VIDEO_STORAGE_DIR, m->m_dev_id, TIME_FORMAT_STR(t));
		char para[512] = { 0 };//���ݸ�ffmpeg�������в���
		sprintf(para, " -i %s -c copy -f mp4 %s", m->m_input_url, output_url);
		
		//�����ܵ�
		HANDLE hInputReadPipe, hInputWritePipe;
		HANDLE hOutputReadPipe, hOutputWritePipe;
		SECURITY_ATTRIBUTES saPipe;
		saPipe.nLength = sizeof(SECURITY_ATTRIBUTES);
		saPipe.lpSecurityDescriptor = NULL;
		saPipe.bInheritHandle = TRUE;
		
		if (!CreatePipe(&hInputReadPipe, &hInputWritePipe, &saPipe, 0))//�½��̵�����ܵ�
		{
			printf("�����ܵ�ʧ��...\n");
			goto detect;
		}
		if (!CreatePipe(&hOutputReadPipe, &hOutputWritePipe, &saPipe, 0))//�½��̵�����ܵ�
		{
			printf("�����ܵ�ʧ��...\n");
			goto detect;
		}

		//��������
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
			TRUE,//ָʾ�½����Ƿ�ӵ��ý��̼̳о�����������Ϊ�棬���ý����е�ÿһ���ɼ̳еĴ򿪾���������ӽ��̼̳У����̳еľ����ԭ����ӵ����ȫ��ͬ��ֵ�ͷ���Ȩ��
			0, //��ʹ���µĿ���̨����
			NULL, NULL, &si, &pi))
		{
			printf("δ�ܴ���ffmpeg����\n");
			return 0;
		}
		//�����ӽ��̺����ѱ��̳У�Ϊ�˰�ȫӦ�ùر�����
		CloseHandle(hInputReadPipe);
		CloseHandle(hOutputWritePipe);

		char cmd[] = "q\n";//��ffmpeg����������
		int counter = 0;
		while (1)
		{
			counter++;
			if (m->m_exit_flag)
				goto force_ffmpeg_close;
			DWORD ret = WaitForSingleObject(pi.hProcess, 1000);
			if (ret == WAIT_TIMEOUT) {
				if (counter == FILE_DURATION)//�����趨��¼��ʱ����ǿ�ƽ���ffmpeg
					goto force_ffmpeg_close;
				continue;
			}
			//ffmpeg��������������������ֹ���������
			goto clear;
		}
force_ffmpeg_close:
		write_pipe(hInputWritePipe, cmd);//��ffmpeg���������ִ���֪ͨ�����¼�Ƴ���
		WaitForSingleObject(pi.hProcess, INFINITE);
clear:
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		CloseHandle(hInputWritePipe);
		CloseHandle(hOutputReadPipe);

		//������Ƶ�ļ���С�ж��Ǳ��滹�Ƕ���
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
			//��洢����push��Ƶ��Ϣ
			EnterCriticalSection(&Monitor::queue_cs);
			Monitor::video_info_list.push(info);
			LeaveCriticalSection(&Monitor::queue_cs);
		}
	}
end:
	m->m_exit_flag = 2;
	return 0;
}

//�������ݿ���Ϣ
void Monitor::update_database()
{
	time_t time_stamp;
	tm* t = NULL;
	VideoInfo info;

	EnterCriticalSection(&db_cs);
	EnterCriticalSection(&queue_cs);
	nanodbc::statement statement;
	int n_wait = Monitor::video_info_list.size();
	//�������е���Ƶ��Ϣ�������ݿ�
	for (int i = 0; i < n_wait; i++)
	{
		//printf("�������ݿ�...\n");
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

	if (exit_flag)//�յ��˳��ź�ʱ����������ִ������Ĺ��̣���Ϊ��ʱ�̼߳�����������������ݲ������ܲ�����
	{
		return;
	}
	//������ݿ����Ƿ��й��ڵ����ݣ�������ɾ����Ӧ��¼���ļ�
	if (storage_period == CHECK_HISTORY_PERIOD)
	{
		time(&time_stamp);
		time_stamp -= VALIDITY_PERIOD * 24 * 60 * 60;
		t = localtime(&time_stamp);
		char datetime[64] = { 0 };
		sprintf(datetime, "%d-%02d-%02d %02d:%02d:%02d", TIME_FORMAT_STR(t));

		//��ѯ���ݿ��й�����Ƶ���ļ���
		statement.open(*db_connection);
		statement.prepare(NANODBC_TEXT(SELECT_FILENAME_STR));
		statement.bind(0, datetime);
		nanodbc::result results = statement.execute();
		//ɾ�����ڵ��ļ�
		while (results.next())
		{
			nanodbc::string name = results.get<nanodbc::string>(0);
			char filename[512] = VIDEO_STORAGE_DIR;
			strcat(filename, name.c_str());
			remove(filename);
		}
		statement.close();
		statement.open(*Monitor::db_connection);
		//ɾ�����ݿ��й��ڵ�����
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

//���ݿ�����߳�
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
