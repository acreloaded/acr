<?
	// server ban-pack
	require "banpack_read.php";
	foreach($banpack as $bs => $be) $config['sbans'] []= array($bs, $be, 1);
?>