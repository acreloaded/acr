<?
	if(function_exists("getip")) return false; // double include safety
	function getip(){
		$ips = array($_SERVER['REMOTE_ADDR']);
		
		$bullshit = array( // bullshit provided to cope with backwards proxies
			'HTTP_X_FORWARDED_FOR',
			'HTTP_X_REAL_IP',
			'HTTP_CLIENT_IP',
			'HTTP_X_FORWARDED',
			'HTTP_FORWARDED_FOR',
			'HTTP_FORWARDED'
		);
		
		foreach($bullshit as $pieceofshit){
			preg_match("#[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}#s", getenv($pieceofshit), $m);
			$ips = array_merge($ips, (array)$m[0]);
		}

		foreach($ips as $ip) if(!preg_match("#^(1?0|172\.(1[6-9]|2[0-9]|3[0-1])|192\.168)\.#s", $ip)) return $ip;
		return "1.0.0.1";
	}
	
	function getipint(){ // int can be negative - but it still fits
		return ip2long(getip());
	}
	
	function getiplong(){ // as a uint in a string
		return sprintf("%u", getipint());
	}
?>