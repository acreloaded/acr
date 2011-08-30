<?
	if(function_exists("getip")) return false; // double include safety
	function getip(){
		return $_SERVER['REMOTE_ADDR'];
		/*
		if (getenv('HTTP_CLIENT_IP')) {
			$ip = getenv('HTTP_CLIENT_IP');
		}
		elseif (getenv('HTTP_X_FORWARDED_FOR')) {
			$ip = getenv('HTTP_X_FORWARDED_FOR');
		}
		elseif (getenv('HTTP_X_FORWARDED')) {
			$ip = getenv('HTTP_X_FORWARDED');
		}
		elseif (getenv('HTTP_FORWARDED_FOR')) {
			$ip = getenv('HTTP_FORWARDED_FOR');
		}
		elseif (getenv('HTTP_FORWARDED')) {
			$ip = getenv('HTTP_FORWARDED');
		}
		else {
			$ip = $_SERVER['REMOTE_ADDR'];
		}
		return $ip;
		*/
	}
	
	function getipint(){ // int can be negative - but it still fits
		return ip2long(getip());
	}
	
	function getiplong(){ // as a uint in a string
		return sprintf("%u", getipint());
	}
?>