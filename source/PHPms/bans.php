<?
	// server bans
	$config['sbans'] = array(
		// array(start, end, type), // use longs, don't overlap; type = blocked actions {1: play; 2: register}
	);
	$config['sallows'] = array( // same thing, but acts as a whitelist
	);
	include "banpack.php";
?>