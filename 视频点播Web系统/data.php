<?php
/*
测试请求：
http://localhost/data.php?draw=1&columns[0][data]=0&columns[0][name]=&columns[0][searchable]=true&columns[0][orderable]=true&columns[0][search][value]=&columns[0][search][regex]=false&columns[1][data]=1&columns[1][name]=&columns[1][searchable]=true&columns[1][orderable]=true&columns[1][search][value]=&columns[1][search][regex]=false&order[0][column]=1&order[0][dir]=desc&start=1&length=10&search[value]=&search[regex]=false&_=1418644693360
*/
header("Content-type:application/json;charset=utf8");
header("Access-Control-Allow-Origin: *");

function fatal($msg)
{
    echo json_encode(array(
        "error" => $msg
    ));
    exit(0);
}

$serverName = "127.0.0.1";
$connectionOption = array(
    "Database" => "YourDB",
    "Uid" => "sa",
    "PWD" => "YourPWD"
);
//连接数据库
$conn = sqlsrv_connect($serverName,$connectionOption);

if ($conn==null)
{
	fatal("数据库连接出错：" . sqlsrv_errors());
}

$draw = $_GET['draw'];//用于同步的序号，这个值会直接返回给前台

//排序
$order_column = $_GET['order']['0']['column'];//排序的列，从0开始
$order_dir = $_GET['order']['0']['dir'];//ase desc 升序或者降序

//拼接排序sql
$order_sql = "";
if(isset($order_column))
{
    $i = intval($order_column);
    switch($i)
    {
        case 0:
        	$order_sql = " ORDER BY CameraSN ".$order_dir;
        break;
        case 1:
        	$order_sql = " ORDER BY VideoTime ".$order_dir;
        break;
        default:
        	$order_sql = '';
    }
}

//搜索
$search = $_GET['search']['value'];//获取前台传过来的过滤条件

//分页
$limit_flag = isset($_GET['start']) && $_GET['length'] != -1;
$start = 0;
if(isset($_GET['start']))
	$start = $_GET['start'];
$length = $_GET['length'];

//总记录数
$sum_sql = "SELECT COUNT(CameraSN) AS sum FROM Videos";
$records_total = 0;
$results = sqlsrv_query($conn,$sum_sql);
if($results)
{
	while ($row = sqlsrv_fetch_array($results,SQLSRV_FETCH_ASSOC))
	{
		$records_total =  $row['sum'];
	}
	sqlsrv_free_stmt($results);
}
else
{
	//fatal("SQL查询失败：" . sqlsrv_errors());//发布版不要输出报错，这会破坏前端datatable的数据结构
}

//应用过滤条件
$records_filtered = 0;
$where_sql ="";
if(strlen($search)>0)
{
	$where_sql =" WHERE CONVERT(varchar(32),CamaraSN)+CONVERT(varchar(32),VideoTime,120) LIKE '%" .$search."%'";
    	$results = sqlsrv_query($conn,$sum_sql.$where_sql);
	if($results)
	{
		while ($row = sqlsrv_fetch_array($results,SQLSRV_FETCH_ASSOC))
		{
			$records_filtered = $row['sum'];
		}
		sqlsrv_free_stmt($results);
	}   
}
else
    $records_filtered = $records_total;

$total_sql="SELECT CamaraSN,VideoTime,FileName FROM Videos".$where_sql.$order_sql;
$data=array();
$results = sqlsrv_query($conn,$total_sql);
if($results)
{
	$i=0;
	$end=$start+$length;
	while ($row = sqlsrv_fetch_array($results,SQLSRV_FETCH_ASSOC))
	{
		if(!$limit_flag || ($i>=$start && $i<$end))
		{
			array_push($data, array($row['CamaraSN'],$row['VideoTime']->format('Y-m-d H:i:s'),$row['FileName']));
		}
		$i++;
	}
	sqlsrv_free_stmt($results);
}

echo json_encode(array(
    "draw" => intval($draw),
    "recordsTotal" => intval($records_total),
    "recordsFiltered" => intval($records_filtered),
    "data" => $data
),JSON_UNESCAPED_UNICODE);

//关闭连接
sqlsrv_close($conn);

?>
