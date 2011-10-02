<?
	// server bans
	$config['sbans'] = array(
		// array(start, end, type, reason), // use longs, don't overlap; type = blocked actions {1: play; 2: register}
	);
	$config['sallows'] = array( // same as above, but acts as a whitelist
	);
	$config['snickw'] = array( // nickname whitelist
		// WARNING: YOU MUST ADD PASSWORDS TO THE AUTH LIST
		// array(fullname[, case-INsensitive][, 'password' => true][, 'ip' => array(array(start, end)...)]),
	);
	$config['snickb'] = array( // nickname blacklist
		// frag,
		// array(frag, 'nocase' => true),
		// array(frag1, frag2...[, 'nocase' => true]),
	);
	// ban parser
	include "bans/parser.php";
?>