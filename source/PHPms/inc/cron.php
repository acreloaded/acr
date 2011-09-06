<?
	if(function_exists("docron")) return false; // double include safety
	function docron(){
		global $config;
		// connect to database
		connect_db();
		// delete old servers
		mysql_query("DELETE FROM `{$config['db']['pref']}servers` WHERE `time` < ".(time() - 960)); // give 16 minutes
		mysql_query("DELETE FROM `{$config['db']['pref']}auth` WHERE `time` < ".(time() - 30)); // authentication requests expire in half a minute
		mysql_query("DELETE FROM `{$config['db']['pref']}authtime` WHERE `time` < ".(time() - 10)); // forget servers that requested authentication after 10 seconds
	}
?>