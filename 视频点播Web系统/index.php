<?php
header("Content-type: text/html; charset=utf-8");
?>
<!DOCTYPE html>
<html>
	<head>
		<meta charset="utf-8">
		<meta http-equiv="X-UA-Compatible" content="IE=edge">
		<meta name="viewport" content="width=device-width, initial-scale=1.0">
		<title>视频点播</title>
		<link href="plugins/bootstrap/css/bootstrap.css" rel="stylesheet" type="text/css"/>
		<link href="plugins/font-awesome/css/font-awesome.css" rel="stylesheet" type="text/css"/>
		<link href="plugins/datatables.net-bs/css/dataTables.bootstrap.min.css" rel="stylesheet" type="text/css"/>
		<link href="plugins/AdminLTE/css/AdminLTE.min.css" rel="stylesheet" type="text/css"/>
	</head>

	<body>
	<div class="well" style="padding: 10px;">
        <h1><i class='fa fa-video-camera'></i>  历史视频点播</h1>
    </div>	
	<div class="container">
		<div class="row">
		<table id="video_table" class="table table-bordered table-hover">
		<thead>
			<tr>
			  <th>设备ID</th>
			  <th>起始时间</th>
			</tr>
		</thead>
		</table>
		</div>
	</div>
	
	<div class="modal fade" id="dialog">
		<div class="modal-dialog  modal-lg" style="background: #303030;">
			<div class="modal-content">
				<div class="modal-header myModal-header">
					<button type="button" class="close myModal-header-close" data-dismiss="modal" onclick="document.getElementById('main_video').pause();">&times;</button>
				</div>
				<div class="modal-body myModal-body" id="video_area">
					<video id="main_video" controls="controls" autoplay="autoplay" style="width: 100%;height: 100%;">
						<source id="video_path"  type="video/mp4" />
					</video>
				</div>
			</div>
		</div>
		<script>
			function closeDialog()
			{
				$("#dialog").modal("hide");
			}
		</script>
	</div>	
	</body>
	<script src="plugins/jquery/jquery.min.js"></script>
	<script src="plugins/bootstrap/js/bootstrap.js"></script>	
	<script src="plugins/datatables.net/js/jquery.dataTables.min.js"></script>
	<script src="plugins/datatables.net-bs/js/dataTables.bootstrap.min.js"></script>
	<script src="plugins/AdminLTE/js/adminlte.min.js"></script>
	
	<script type="text/javascript">
		function startplay(url)
		{
			var video=document.getElementById("main_video");
			if(video.play) video.pause();
			video.src=url;
			video.load();//重新加载资源
			video.play();//如果用play方法，对于已加载过的视频，播放按钮会有一点bug
			$("#dialog").modal({'show':true,'backdrop':'static'});
		}	
		//配置datatable
		var options={
			"processing": true,
	        	"serverSide": true,
	        	"ajax": "data.php",	
	        	"columns" : [{"data" : 0},{"data" : 1}],
			"aaSorting": [1, 'desc'],
			"language": {
				"decimal":        "",
				"emptyTable":     "无数据",
				"info":           "当前显示第 _START_ 到 _END_ 项（共 _TOTAL_ 项）",
				"infoEmpty":      "",
				"infoFiltered":   "(从 _MAX_ 项中检索)",
				"infoPostFix":    "",
				"thousands":      ",",
				"lengthMenu":     "每页显示 _MENU_ 项",
				"loadingRecords": "正在加载...",
				"processing":     "",
				"search":         "快速检索:",
				"zeroRecords":    "未找到包含关键字的项",
				"paginate": {
					"first":      "首页",
					"last":       "尾页",
					"next":       "下一页",
					"previous":   "上一页"
				}
			},
			"ordering": true, 
			"searching": true,
			"lengthChange": true,//每页项目数的设置框
			"lengthMenu": [10,20,50,100],//控制分页选项框内是否有下拉菜单，如果没有就只能手动输入
			"paging":true,//是否分页
			"info":true,
			"autoWidth": true
		}
	    var tab=$('#video_table').DataTable(options);
	    $('#video_table tbody').on('click','tr',function (){
	        var data = tab.row(this).data();
	        startplay("/Video/"+data[2]);
    	});
	</script>
	
	<style>
	.myModal-header{
		background-color:#404040;
		border:0px;
		height:30px;
	}
 
	.myModal-body{
		background-color:#404040;
	}

	.myModal-header-font{
		font-size:12px;
		color:#FFFFFF;
	}

	.myModal-header-close{
		color:#FFFFFF;
	}

	.myModal-footer{
		background-color:#404040;
		border:1px solid #404040;
		border-top:none;
		padding:0px 15px 30px 15px;
	}
	</style>
</html>

