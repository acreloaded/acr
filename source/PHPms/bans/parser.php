<?
	// server ban tools
	function atoip($s, &$ip){
		$d = array(0, 0, 0, 0);
		$n = 0;
		if(sscanf($s, "%u.%u.%u.%u%n", $d[0], $d[1], $d[2], $d[3], $n) != 5) return false;
		$ip = 0;
		foreach($d as $p) if($p > 255) return false; else $ip = ($ip << 8) + $p;
		return $n;
	}
	function atoipr($a, &$ipr){
		if(($skip = atoip($a, $ipr[0])) === false) return false;
		$ipr[1] = $ipr[0];
		$skip += strspn($a, " \t\r\n", $skip);
		$a = substr($a, $skip);
		if($a[0] == '-'){
			if(false === ($skip2 = atoip(substr($a, 1), $ipr[1])) || $ipr[0] > $ipr[1]) return false;
			$skip += 1 + $skip2;
		}
		else if($a[0] == '/'){
			$m = $n = 0;
			if(sscanf(substr($a, 1), "%d%n", $m, $n) != 2 || $m < 0 || $m > 32) return false;
			$m = (1 << (32 - $m)) - 1;
			$ipr[0] &= ~$m;
			$ipr[1] |= $m;
			$skip += 1 + $n;
		}
		return $skip;
	}
	function readcfg($f){
		if(($buf = fgets($f, 512)) === false) return false;
		if(($buf2 = strstr($buf, "//", true)) !== false) $buf = $buf2;
		return $buf[0] == '#' ? '' : $buf;
	}
	// IP bans
	if($f = @fopen(dirname(__FILE__)."/ipbans.txt", "r")) while (($buf = readcfg($f)) !== false ){
		if(atoipr($buf, $ipr) !== false) $config['sbans'] []= array($ipr[0], $ipr[1], 3, "ip is in a proxy range"); // 1: play, 2: register, 3: all
	}
	// Nickname Bans
	if($f = @fopen(dirname(__FILE__)."/nickbans.txt", "r")) while (($buf = readcfg($f)) !== false ){
		// I think it's racist that AC's server checks for the whitelist first
		// It's more efficient to check the blacklist first as it usually has more entries
		$t = strtok($buf, " \t\r\n");
		if($t[0] != "a" && $t[0] != "b") continue; // let's speed it up!
		if($t){
			$s = strtok(" \t\r\n");
			if($s){
				$ic = 0;
				if($t == "block" || $t == "b" || $ic++ || $t == "blocki" || $t == "bi"){
					$vals = array();
					while($s !== false){
						$vals[] = $s;
						$s = strtok(" \t\r\n");
					}
					$vals['nocase'] = (bool) $ic;
					$config['snickb'][] = $vals;
				}
				elseif(($ic = 0) || $t == "accept" || $t == "a" || $ic++ || $t == "accepti" || $t == "ai"){
					$val[0] = $s;
					$val[1] = (bool)$ic;
					while(($s = strtok(" \t\r\n")) !== false) if(atoipr($s, $v) !== false) $val['ip'][] = $v;
					$config['snickw'][] = $val;
				}
			}
		}
	}
?>