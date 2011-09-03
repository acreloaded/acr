<?
	// server bans
	$config['sbans'] = array(
		// array(start, end, type), // use longs, don't overlap; type = blocked actions {1: play; 2: register}
	);
	$config['sallows'] = array( // same thing, but acts as a whitelist
	);
	include "banpack.php";
	// tools
	function u2i($n){ return $n > 2147483647 ? $n - 4294967296 : $n; }
	function putrange($rs, $mask){
		$ret = "";
		foreach($rs as $r) if($r[2] & $mask) $ret .= u2i($r[0])."_".u2i($r[1])."|";
		return $ret;
	}
?>