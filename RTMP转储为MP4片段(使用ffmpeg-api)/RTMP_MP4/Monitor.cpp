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
	//初始化所有的封装和解封装组件
	av_register_all();
	//初始化网络库
	avformat_network_init();
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

int Monitor::open_input()
{
	//打开输入文件，解封装文件头
	if (avformat_open_input(&m_ifmt_ctx, m_input_url, 0, 0) < 0) {
		printf("未能打开输入文件\n");
		return ERROR_NO_INPUT;
	}
	//获取输入文件音频视频流的信息，否则无法正确写文件头
	if (avformat_find_stream_info(m_ifmt_ctx, 0) < 0) {
		printf("未能解析输入文件头信息\n");
		return ERROR_NO_HEARDER;
	}
	//这里通过输入流的duration信息，来判断收到的视频是否为设备不在线的提示视频
	int64_t duration = m_ifmt_ctx->duration;
	if (duration != AV_NOPTS_VALUE) {
		/*
		//测试发现直播视频流的duration=AV_NOPTS_VALUE，所以暂时注释掉了时长判断
		double_t duration_seconds = (double_t)duration / AV_TIME_BASE;
		//printf("duration:%.2f s \n", duration_seconds);
		if (duration_seconds > 7 && duration_seconds < 8)//萤石的提示视频长度为7.5s
		*/
		return ERROR_NO_INPUT;
	}
	//av_dump_format(ifmt_ctx, 0, in_filename, 0);//查看输入文件的全部信息
	return ERROR_NO_ERROR;
}

void Monitor::close_input()
{
	if (m_ifmt_ctx)
		avformat_close_input(&m_ifmt_ctx);//此函数在释放全部空间后，会将m_ifmt_ctx置为NULL
}

//创建封装格式环境，并配置相应的输出流和参数，打开输出文件并写入文件头
int Monitor::open_output(char* output_url)
{
	//创建输出流的封装格式环境
	avformat_alloc_output_context2(&m_ofmt_ctx, NULL, NULL, output_url);
	if (!m_ofmt_ctx) {
		printf("未能创建输出流的AVFormatContext\n");
		return ERROR_OTHER;
	}

	//创建域输入音视频流相应的输出音视频流，并配置输出编码器参数
	for (int i = 0; i < m_ifmt_ctx->nb_streams; i++) {
		AVStream *in_stream = m_ifmt_ctx->streams[i];
		AVStream *out_stream = avformat_new_stream(m_ofmt_ctx, in_stream->codec->codec);
		if (!out_stream) {
			printf("未能创建输出音视频流\n");
			return ERROR_OTHER;
		}
		//复制输入流的编码器参数，使输出和输入保持一致，无需重新编码
		//int ret = avcodec_copy_context(out_stream->codec, in_stream->codec);//老版本
		int ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
		//ret = avcodec_parameters_from_context(out_stream->codecpar, in_stream->codec);
		//ret = avcodec_parameters_to_context(out_stream->codec, in_stream->codecpar);
		if (ret < 0) {
			printf("复制编码器环境出错\n");
			return ERROR_OTHER;
		}
		out_stream->codecpar->codec_tag = 0;
		//out_stream->codec->codec_tag = 0;
		if (m_ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	//av_dump_format(ofmt_ctx, 0, out_filename, 1);//输出文件的全部信息

	//打开输出文件
	if (!(m_ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&m_ofmt_ctx->pb, output_url, AVIO_FLAG_WRITE) < 0) {
			printf("未能创建输出文件 '%s'\n", output_url);
			return ERROR_OPEN_OUTPUT;
		}
	}
	//写封装文件头
	if (avformat_write_header(m_ofmt_ctx, NULL) < 0) {
		printf("未能写入文件头\n");
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
			avio_close(m_ofmt_ctx->pb);//关闭输出文件
		}
		avformat_free_context(m_ofmt_ctx);//释放封装环境
		m_ofmt_ctx = NULL;//为了下一次正常open_output
	}
}

//写一帧
int Monitor::write_frame()
{
	AVPacket pkt;
	AVStream *in_stream, *out_stream;
	av_init_packet(&pkt);
	pkt.size = 0;
	pkt.data = NULL;
	//读入一个AVPacket，相当于一个NAL
	if (av_read_frame(m_ifmt_ctx, &pkt) < 0)
		return ERROR_NO_STREAM;
	in_stream = m_ifmt_ctx->streams[pkt.stream_index];
	out_stream = m_ofmt_ctx->streams[pkt.stream_index];
	if (m_ofmt_ctx->oformat->flags & AVFMT_NOTIMESTAMPS)
		return ERROR_WRITE_FRAME;
	//转换输入输出流的PTS/DTS，当二者帧率不同时，转换的效果就是加快或减缓播放速度
	pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);

	//写帧
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

//写文件尾
void Monitor::write_trailer()
{
	av_write_trailer(m_ofmt_ctx);
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
	int error;
	//监听循环
	while (1)
	{
	detect:
		if (m->m_exit_flag)//优先判断退出信号
			goto end;

		if (m->get_online_state() == false)//如果设备不在线，每隔几秒尝试一次连接
		{
			Sleep(DETECT_DURATION * 1000);
			goto detect;
		}
		if (m->open_input() != ERROR_NO_ERROR)
			goto detect;
		//m->get_input_url();//更新地址，但目前几乎能确保rtmp不会改变
		//获取当前的日期与时刻，以此命名视频文件
		//存储循环
		while (1)
		{
			if (m->m_exit_flag)
			{
				m->close_input();
				goto end;
			}	
			error = ERROR_NO_ERROR;
			//首先获取当前的日期与时刻，以此命名视频文件
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
			//写帧循环
			while (frame_index < FILE_DURATION)
			{
				if (m->m_exit_flag)
					break;
				error = m->write_frame();
				if (error != ERROR_NO_ERROR)
					break;
				frame_index++;
			}
			//从写帧循环退出，则一定要写文件尾并关闭输出流
			m->close_output();
			
			//根据视频文件大小判断是保存还是丢弃
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
				//向存储队列push视频信息
				EnterCriticalSection(&Monitor::queue_cs);
				Monitor::video_info_list.push(info);
				LeaveCriticalSection(&Monitor::queue_cs);
			}
			
			//如果是设备下线导致的存储终止，则关闭输入流，进入监听循环
			if (error == ERROR_NO_STREAM)
			{
				m->close_input();
				goto detect;
			}
		}
	}
close_and_end:
	m->close_output();//这里的close操作，不会和存储循环里的close冲突
	m->close_input();
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
