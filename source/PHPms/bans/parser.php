<?
	$banpack = array();
	if($f = @fopen("banpack/banpack.txt", "r")) while (($buf = fgets($f, 81)) !== false ){
		if($buf[0] == "#") continue; // speed it up!
		if(!preg_match("#([0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3})(?:/([0-9]{1,2}))?#", $buf, $match)) continue;
		$start = $end = ip2long($match[1]);
		if($match[2] > 0 && $match[2] < 32){
			$mask = (1 << (32 - $match[2])) - 1;
			$start &= ~$mask;
			$end |= $mask;
		}
		$banpack[$start] = $end;
	}
?>