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
	function putrange($rs){
		global $config;
		$ranges = array();
		foreach($config[$rs] as $r) if($r[2] & 1){
			$ranges[] = u2i($r[0]).($r[0] != $r[1] ? "_".u2i($r[1]) : '');
		}
		echo implode('|', $ranges);
	}
	function sendranges(){
		echo "\n*b"; putrange('sbans'); // bans
		echo "\n*a"; putrange('sallows'); // allows
	}
?>