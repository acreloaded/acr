<?
	// server bans
	$config['sbans'] = array(
		// array(start, end, type, reason), // use longs, don't overlap; type = blocked actions {1: play; 2: register}
	);
	$config['sallows'] = array( // same thing, but acts as a whitelist
	);
	include "banpack/banpack.php";
	// tools
	function u2i($n){ return $n > 0x7FFFFFFF ? $n - 0x100000000 : $n; }
	function putrange($rs){
		global $config;
		$ranges = array();
		foreach($config[$rs] as $r) if($r[2] & 1){
			$ranges [] = array($r[0], $r[1]);
		}
		// remove this in 2.2.3+
		usort($ranges, create_function('$a,$b', 'return - ($a[0] < $b[0]) + ($a[0] > $b[1]);'));
		// end
		foreach($ranges as &$r){
			$r = u2i($r[0]).($r[0] != $r[1] ? "_".u2i($r[1]) : '');
		}
		echo implode('|', $ranges);
	}
	function sendranges($pad){
		if($pad & 1) echo "\n";
		echo "*b"; putrange('sbans')."\n*a"; putrange('sallows'); // bans / allows
		if($pad & 2) echo "\n";
	}
?>