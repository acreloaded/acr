<?
	require_once "config.php"; // of course I need it if I don't have it!
	if(function_exists("docron")) return false; // double include safety
	function docron(){
		global $config;
		// connect to database
		connect_db();
		// delete old servers
		mysql_query("DELETE FROM `{$config['db']['pref']}servers` WHERE `time` < ".(time() - 1440)); // give 24 minutes
		mysql_query("DELETE FROM `{$config['db']['pref']}auth` WHERE `time` < ".(time() - 60)); // authentication requests expire in a minute
	}
?>