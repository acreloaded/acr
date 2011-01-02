<?
	// general
	$config['contact'] = 'Please contact the masterserver admin.'; // include some form of contact
	// servers
	$config['servers']['minprotocol'] = 2103; // protocol requirements
	$config['servers']['autoapprove'] = true; // servers can be registered
	$config['servers']['force'] = array(
		// "server:port",
);
	$config['servers']['minport'] = 28760;
	$config['servers']['maxport'] = 28780;
	
	// motd
	$config['motd'] = <<<MOTD
\f0Welcome \f1to \f2the \f7Assault\f3Cube \f8Special \f9Edition \f3master\f5server
MOTD;

	// database
	$config['db']['host'] = 'localhost'; // database host (domain or IP)
	$config['db']['name'] = ''; // database name
	$config['db']['user'] = ''; // database user
	$config['db']['pass'] = ''; // database pass
	$config['db']['pref'] = 'cubems_'; // table prefix
	
	function connect_db(){ // global function to connect to the database
		global $config;
		$r = mysql_connect($config['db']['host'], $config['db']['user'], $config['db']['pass']);
		mysql_select_db($config['db']['name'], $r);
		return $r;
	}
?>