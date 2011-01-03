<?
	require "config.php"; // get our configuration
	require_once "inc.ip.php"; // function to detect IP
	require_once "cron.php"; // take care of the tasks
	require_once "bans.php"; // banning sysrem
	require_once "auth.php"; // auths
	docron();
	function getServers(){ // {string, int}[] (void)
		global $config;
		$buffer = array();
		foreach($config['servers']['force'] as $srv){ // forced servers
			preg_match("/([^:]+):([0-9]{1,5})/", $srv, $s);
			$buffer[] = array($s[1], $s[2]);
		}
		$q = mysql_query("SELECT `ip`, `port` FROM `{$config['db']['pref']}servers`");
		while ($r = mysql_fetch_row($q)) $buffer[] = array(long2ip($r[0]), $r[1]); // {$r[ip], $r[port]}
		return $buffer;
	}
	if(isset($_GET['cube'])){ // cubescript
		header('Content-type: text/plain');
		$motdlines = explode("\n", $config['motd']);
		foreach($config['sbans'] as $b) if($b[0] <= $ip && $b[1] <= $banRangeStart && $b[2] & 1) exit("You are not authorized to fetch the server list. {$config['contact']}");
		foreach($motdlines as $l) // MOTD
			echo 'echo "'.str_replace('"', "''", trim($l))."\"\n"; // looks like we can't use double quotes!
		$srvs = getServers();
		foreach($srvs as $s) echo "addserver {$s[0]} {$s[1]}\r\n";
	}
	elseif(isset($_GET['xml'])){ // XML
		header('Content-type: text/xml');
		echo '<?xml version="1.0" encoding="UTF-8"?><MOTD><![CDATA['.str_replace("\f", '\f', $config['motd']).']]></MOTD><Servers>';
		$srvs = getServers();
		foreach($srvs as $s) echo '<Server port="'.$s[1].'">'.$s[0].'</Server>';
		echo '</Servers>';
	}
	elseif(isset($_GET['register'])){ // register
		if($config['servers']['autoapprove'] === false) exit("Automatic registration is closed. {$config['contact']}");
		$port = intval($_GET['port']);
		if($port < $config['servers']['minport'] || $port > $config['servers']['maxport'])
			exit("You may only register a server with ports between {$config['servers']['minport']} and {$config['servers']['maxport']}");
		$ip = getiplong();
		// find server
		// connect_db(); // we took care of this in cron
		mysql_query("DELETE FROM `{$config['db']['pref']}auth` WHERE `ip`={$ip}"); // clear auth from this server
		if($q = mysql_num_rows(mysql_query("SELECT `port` FROM `{$config['db']['pref']}servers` WHERE `ip`={$ip} AND `port`={$port}"))){ // renew
			mysql_query("UPDATE `{$config['db']['pref']}servers` SET `time`=".time()." WHERE `ip`={$ip} AND `port`={$port}");
			echo 'Your server has been renewed. Just a reminder to forward ports UDP '.$port.' and '.($port + 1).' if you have not already';
		}else{ // register
			foreach($config['sbans'] as $b) if($b[0] <= $ip && $b[1] <= $banRangeStart && $b[2] & 2) exit("You are not authorized to register a server. {$config['contact']}");
			if($config['servers']['minprotocol'] > $_GET['proto']) exit("!!!UPDATE NOW!!!! You must run a server at least protocol {$config['servers']['minprotocol']}. {$config['contact']}");
			/*
				No sockets with free hosting :(
			*/
			mysql_query("INSERT INTO `{$config['db']['pref']}servers` (`ip`, `port`, `time`) VALUES ({$ip}, {$port}, ".time().")");
			echo 'Your server has been registered. Make sure you forward ports UDP '.$port.' and '.($port + 1).' if you have not already';
		}
	}
	elseif(isset($_GET['authreq'])){ // request auth
		$ip = getiplong();
		$q = mysql_num_rows(mysql_query("SELECT `ip` FROM `{$config['db']['pref']}servers` WHERE `ip`={$ip}"));
		if(!$q) exit("*f{$id}");
		$id = intval($_GET['id']);
		$q = mysql_num_rows(mysql_query("SELECT `id` FROM `{$config['db']['pref']}auth` WHERE `ip`={$ip} AND `id`={$id}"));
		if($q) exit("*f{$id}");
		$nonce = mt_rand(0, 2147483647); // 32-bit signed -> 31-bit unsigned
		mysql_query("INSERT INTO `{$config['db']['pref']}auth` (`ip`, `time`, `id`, `nonce`) VALUES ({$ip}, ".time().", {$id}, {$nonce})");
		echo "*c{$id}|".$nonce;
	}
	elseif(isset($_GET['authchal'])){ // answer auth
		$ip = getiplong();
		$q = mysql_num_rows(mysql_query("SELECT `ip` FROM `{$config['db']['pref']}servers` WHERE `ip`={$ip}"));
		if(!$q) exit("*f{$id}");
		$id = intval($_GET['id']);
		$q = mysql_num_rows(mysql_query("SELECT `id` FROM `{$config['db']['pref']}auth` WHERE `ip`={$ip} AND `id`={$id}"));
		if(!$q) exit("*f{$id}");
		$q = mysql_result(mysql_query("SELECT `nonce` FROM `{$config['db']['pref']}auth` WHERE `ip`={$ip} AND `id`={$id}"), 0, 0);
		mysql_query("DELETE FROM `{$config['db']['pref']}auth` WHERE `ip`={$ip} AND `id`={$id}"); // used auth
		$ans = &$_GET['ans'];
		foreach($config['auth'] as $authkey) if(strtolower($ans) == sha1($q.$authkey[0])) exit("*s{$id}|".$authkey[2].$authkey[1]);
		echo "*d{$id}"; // no match
	}
	else{
	echo <<<INFO
AssaultCube Special Edition Master Server
====================
Use /cube for CubeScript Server List
Use /xml for XML Server List (Custom format)
Your server may register any port between {$config['servers']['minport']} and {$config['servers']['maxport']} if:
your server is running protocol {$config['servers']['minprotocol']} or later.
INFO
;}?>