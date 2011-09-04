<?
	// server ban-pack
	require "banpack_read.php";
	foreach($banpack as $bs => $be) $config['sbans'] []= array($bs, $be, 3, "ip is in a proxy range"); // 1: play, 2: register, 3: all
?>