<?php
/*
本文件返回的json格式为：
[
	{"id":2,"url": rtmp(设备在线)或url(不在线),"online":0(不在线)或1(在线)},
	{...},
	...
]
*/
header("Content-type:application/json;charset=utf8");
header("Access-Control-Allow-Origin: *");

function gbk2utf8($string){
	//return mb_convert_encoding($string, "UTF-8", "GBK");
	return iconv("gbk","utf-8",$string);
}

function utf82gbk($string){
	return iconv("utf-8","gbk",$string);
}

$server_addr = "127.0.0.1";
$connect_opt = array(
    "Database" => "YourDB",
    "Uid" => "sa",
    "PWD" => "YourPWD"
);
//连接数据库
$connection = sqlsrv_connect($server_addr,$connect_opt);

if ($connection==null){
	die(print_r(sqlsrv_errors(),true));
}

//查询设备信息表
$data=array();
$sql = "select CameraSN,RTMP,OnlineState from Cameras where VideoSave=1;";
$results = sqlsrv_query($connection,$sql);
if($results){
	while ($row = sqlsrv_fetch_array($results,SQLSRV_FETCH_ASSOC)){
		array_push($data, array('id'=>$row['SN'],'url'=>$row['RTMP'],'online'=>$row['OnlineState']));
	}
	sqlsrv_free_stmt($results);
}
else{
	die(print_r(sqlsrv_errors(), true));
}


//对于不在线的设备，将url换成最近的一个视频文件的地址
$addr="http://".gethostbyname(gethostname());
$n=count($data);
for($i=0;$i<$n;$i++)
{
	if($data[$i]['online']==0)
	{
		$sql="
		SELECT TOP 1 FileName 
		FROM Videos 
		WHERE CamaraSN='" . $data[$i]['id'] . "' ORDER BY VideoTime DESC";
		$results = sqlsrv_query($connection,$sql);
		$data[$i]['url']='default.mp4';//如果查询失败或没有历史视频，则返回默认的视频文件
		if($results)
		{
			$row = sqlsrv_fetch_array($results,SQLSRV_FETCH_ASSOC);
			if($row['FileName']!='')
			{
				$data[$i]['url']='Video/'.$row['FileName'];
				sqlsrv_free_stmt($results);
			}
		}
	}
}

//关闭连接
sqlsrv_close($connection);

echo json_encode($data);

?>
