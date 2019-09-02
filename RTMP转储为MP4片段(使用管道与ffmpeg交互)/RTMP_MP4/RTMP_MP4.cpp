#include "Monitor.h"
#include <vector>
#include <windows.h>

std::vector<Monitor*> g_monitors;
bool g_running = false;

void stop_monitor()
{
	g_running = false;
	printf("程序开始退出...\n正在清理资源和保存数据，请等待...\n");
	for (int i = 0; i < g_monitors.size(); i++)
	{
		delete g_monitors[i];
	}
	Monitor::exit();//清理静态资源

	printf("程序已正常结束.\n");
}

bool WINAPI ConsoleHandler(DWORD event)
{
	//在用户按下ctrl+c，或被强制关闭时，执行stop_monitor通知各监控线程收尾，并释放对象
	//但强烈建议不使用这种方式结束程序，而是按q或Q键以使用正常流程结束
	if (event == CTRL_C_EVENT || event == CTRL_CLOSE_EVENT || event == CTRL_SHUTDOWN_EVENT)
	{
		if (g_running)
			stop_monitor();
	}
	return true;
}

int main(int argc, char* argv[])
{
	//初始化
	Monitor::init();
	//从数据库查询设备列表，由设备ID创建对应的监控对象
	EnterCriticalSection(&Monitor::db_cs);
	nanodbc::result results = execute(*Monitor::db_connection, NANODBC_TEXT(SELECT_ID_STR));
	while (results.next())
	{
		nanodbc::string id = results.get<nanodbc::string>(0);
		printf("ID[%s]的监控线程启动\n",id.c_str());
		Monitor* p = new Monitor(id.c_str());
		g_monitors.push_back(p);
	}
	LeaveCriticalSection(&Monitor::db_cs);

	for (int i = 0; i < g_monitors.size(); i++)
	{
		g_monitors[i]->get_input_url();//从数据库获取对应设备的rtmp地址
		g_monitors[i]->run();
	}
	
	//注册控制台控制事件Handler
	if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler, TRUE) == FALSE)
		printf("注册控制台事件Handler失败\n");

	g_running = true;

	printf("RTMP转储MP4服务启动...\n");

	char ctrl;
	while (1)
	{
		printf("按q/Q建回车，可安全结束本程序...\n");
		ctrl = getchar();
		if (ctrl == 'q' || ctrl == 'Q')
		{
			stop_monitor();
			return 0;
		}
	}
	return 0;
}
