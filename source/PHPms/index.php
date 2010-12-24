<?
	require "config.php"; // get our configuration
	require_once "cron.php"; // take care of the tasks
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
		require_once "inc.ip.php"; // function to detect IP
		$ip = getiplong();
		// find server
		// connect_db(); // we took care of this in cron
		if($q = mysql_num_rows(mysql_query("SELECT `port` FROM `{$config['db']['pref']}servers` WHERE `ip`={$ip} AND `port`={$port}"))){ // renew
			mysql_query("UPDATE `{$config['db']['pref']}servers` SET `time`=".time()." WHERE `ip`={$ip} AND `port`={$port}");
			echo 'Your server has been renewed.';
		}else{ // register
			foreach($config['sbans'] as $b) if($b[0] <= $ip && $b[1] <= $banRangeStart && $b[2] & 2) exit("You are not authorized to register a server. {$config['contact']}");
			if($config['servers']['minprotocol'] > $_GET['proto']) exit("You must run a server at least protocol {$config['servers']['minprotocol']}. {$config['contact']}");
			/*
				No sockets with free hosting :(
			*/
			mysql_query("INSERT INTO `{$config['db']['pref']}servers` (`ip`, `port`, `time`) VALUES ({$ip}, {$port}, ".time().")");
			echo 'Your server has been registered. Make sure your server is accessible from the internet as it cannot be verified from this end.';
		}
	}
	else{
	echo <<<INFO
AssaultCube Special Edition Master Server
====================
Use /cube for CubeScript Server List
Use /xml for XML Server List (Custom format)
Your server may register any port between {$config['servers']['minport']} and {$config['servers']['maxport']} if:
your server is running version {$config['servers']['minversion']} (protocol {$config['servers']['minprotocol']}) or later.
INFO
;}?>