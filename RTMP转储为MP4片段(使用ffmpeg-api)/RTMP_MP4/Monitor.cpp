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

	m_ifmt_ctx = NULL;
	m_ofmt_ctx = NULL;
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
	//��ʼ�����еķ�װ�ͽ��װ���
	av_register_all();
	//��ʼ�������
	avformat_network_init();
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

int Monitor::open_input()
{
	//�������ļ������װ�ļ�ͷ
	if (avformat_open_input(&m_ifmt_ctx, m_input_url, 0, 0) < 0) {
		printf("δ�ܴ������ļ�\n");
		return ERROR_NO_INPUT;
	}
	//��ȡ�����ļ���Ƶ��Ƶ������Ϣ�������޷���ȷд�ļ�ͷ
	if (avformat_find_stream_info(m_ifmt_ctx, 0) < 0) {
		printf("δ�ܽ��������ļ�ͷ��Ϣ\n");
		return ERROR_NO_HEARDER;
	}
	//����ͨ����������duration��Ϣ�����ж��յ�����Ƶ�Ƿ�Ϊ�豸�����ߵ���ʾ��Ƶ
	int64_t duration = m_ifmt_ctx->duration;
	if (duration != AV_NOPTS_VALUE) {
		/*
		//���Է���ֱ����Ƶ����duration=AV_NOPTS_VALUE��������ʱע�͵���ʱ���ж�
		double_t duration_seconds = (double_t)duration / AV_TIME_BASE;
		//printf("duration:%.2f s \n", duration_seconds);
		if (duration_seconds > 7 && duration_seconds < 8)//өʯ����ʾ��Ƶ����Ϊ7.5s
		*/
		return ERROR_NO_INPUT;
	}
	//av_dump_format(ifmt_ctx, 0, in_filename, 0);//�鿴�����ļ���ȫ����Ϣ
	return ERROR_NO_ERROR;
}

void Monitor::close_input()
{
	if (m_ifmt_ctx)
		avformat_close_input(&m_ifmt_ctx);//�˺������ͷ�ȫ���ռ�󣬻Ὣm_ifmt_ctx��ΪNULL
}

//������װ��ʽ��������������Ӧ��������Ͳ�����������ļ���д���ļ�ͷ
int Monitor::open_output(char* output_url)
{
	//����������ķ�װ��ʽ����
	avformat_alloc_output_context2(&m_ofmt_ctx, NULL, NULL, output_url);
	if (!m_ofmt_ctx) {
		printf("δ�ܴ����������AVFormatContext\n");
		return ERROR_OTHER;
	}

	//��������������Ƶ����Ӧ���������Ƶ�����������������������
	for (int i = 0; i < m_ifmt_ctx->nb_streams; i++) {
		AVStream *in_stream = m_ifmt_ctx->streams[i];
		AVStream *out_stream = avformat_new_stream(m_ofmt_ctx, in_stream->codec->codec);
		if (!out_stream) {
			printf("δ�ܴ����������Ƶ��\n");
			return ERROR_OTHER;
		}
		//�����������ı�����������ʹ��������뱣��һ�£��������±���
		//int ret = avcodec_copy_context(out_stream->codec, in_stream->codec);//�ϰ汾
		int ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
		//ret = avcodec_parameters_from_context(out_stream->codecpar, in_stream->codec);
		//ret = avcodec_parameters_to_context(out_stream->codec, in_stream->codecpar);
		if (ret < 0) {
			printf("���Ʊ�������������\n");
			return ERROR_OTHER;
		}
		out_stream->codecpar->codec_tag = 0;
		//out_stream->codec->codec_tag = 0;
		if (m_ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	//av_dump_format(ofmt_ctx, 0, out_filename, 1);//����ļ���ȫ����Ϣ

	//������ļ�
	if (!(m_ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&m_ofmt_ctx->pb, output_url, AVIO_FLAG_WRITE) < 0) {
			printf("δ�ܴ�������ļ� '%s'\n", output_url);
			return ERROR_OPEN_OUTPUT;
		}
	}
	//д��װ�ļ�ͷ
	if (avformat_write_header(m_ofmt_ctx, NULL) < 0) {
		printf("δ��д���ļ�ͷ\n");
		return ERROR_WRITE_HEADER;
	}
	return ERROR_NO_ERROR;
}

void Monitor::close_output()
{
	if (m_ofmt_ctx)
	{
		if (!(m_ofmt_ctx->oformat->flags & AVFMT_NOFILE))
		{
			write_trailer();
			avio_close(m_ofmt_ctx->pb);//�ر�����ļ�
		}
		avformat_free_context(m_ofmt_ctx);//�ͷŷ�װ����
		m_ofmt_ctx = NULL;//Ϊ����һ������open_output
	}
}

//дһ֡
int Monitor::write_frame()
{
	AVPacket pkt;
	AVStream *in_stream, *out_stream;
	av_init_packet(&pkt);
	pkt.size = 0;
	pkt.data = NULL;
	//����һ��AVPacket���൱��һ��NAL
	if (av_read_frame(m_ifmt_ctx, &pkt) < 0)
		return ERROR_NO_STREAM;
	in_stream = m_ifmt_ctx->streams[pkt.stream_index];
	out_stream = m_ofmt_ctx->streams[pkt.stream_index];
	if (m_ofmt_ctx->oformat->flags & AVFMT_NOTIMESTAMPS)
		return ERROR_WRITE_FRAME;
	//ת�������������PTS/DTS��������֡�ʲ�ͬʱ��ת����Ч�����Ǽӿ����������ٶ�
	pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);

	//д֡
	try 
	{
		if (av_interleaved_write_frame(m_ofmt_ctx, &pkt) < 0)
			return ERROR_WRITE_FRAME;
	}
	catch (exception& e)
	{
		av_free_packet(&pkt);
		return ERROR_WRITE_FRAME;
	}
	av_free_packet(&pkt);
	return ERROR_NO_ERROR;
}

//д�ļ�β
void Monitor::write_trailer()
{
	av_write_trailer(m_ofmt_ctx);
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
	int error;
	//����ѭ��
	while (1)
	{
	detect:
		if (m->m_exit_flag)//�����ж��˳��ź�
			goto end;

		if (m->get_online_state() == false)//����豸�����ߣ�ÿ�����볢��һ������
		{
			Sleep(DETECT_DURATION * 1000);
			goto detect;
		}
		if (m->open_input() != ERROR_NO_ERROR)
			goto detect;
		//m->get_input_url();//���µ�ַ����Ŀǰ������ȷ��rtmp����ı�
		//��ȡ��ǰ��������ʱ�̣��Դ�������Ƶ�ļ�
		//�洢ѭ��
		while (1)
		{
			if (m->m_exit_flag)
			{
				m->close_input();
				goto end;
			}	
			error = ERROR_NO_ERROR;
			//���Ȼ�ȡ��ǰ��������ʱ�̣��Դ�������Ƶ�ļ�
			time_t time_stamp;
			time(&time_stamp);
			tm* t = localtime(&time_stamp);
			char output_url[256] = { 0 };
			sprintf(output_url, "%s%s-%d%02d%02d-%02d%02d%02d.mp4", VIDEO_STORAGE_DIR, m->m_dev_id, TIME_FORMAT_STR(t));
			VideoInfo info = { m->m_dev_id,time_stamp };

			error = m->open_output(output_url);
			if (error != ERROR_NO_ERROR)
				goto close_and_end;

			int frame_index = 0;
			//д֡ѭ��
			while (frame_index < FILE_DURATION)
			{
				if (m->m_exit_flag)
					break;
				error = m->write_frame();
				if (error != ERROR_NO_ERROR)
					break;
				frame_index++;
			}
			//��д֡ѭ���˳�����һ��Ҫд�ļ�β���ر������
			m->close_output();
			
			//������Ƶ�ļ���С�ж��Ǳ��滹�Ƕ���
			WIN32_FIND_DATA file_info;
			HANDLE hFind;
			DWORD file_size = 0;
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
			
			//������豸���ߵ��µĴ洢��ֹ����ر����������������ѭ��
			if (error == ERROR_NO_STREAM)
			{
				m->close_input();
				goto detect;
			}
		}
	}
close_and_end:
	m->close_output();//�����close����������ʹ洢ѭ�����close��ͻ
	m->close_input();
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
