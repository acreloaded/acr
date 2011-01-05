-- AssaultCube Special Edition Masterserver Tables

CREATE TABLE IF NOT EXISTS `cubems_auth` (
  `ip` int(10) unsigned NOT NULL default '0',
  `time` bigint(20) unsigned default NULL,
  `id` int(10) unsigned NOT NULL default '0',
  `nonce` int(11) default NULL,
  PRIMARY KEY  (`ip`,`id`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;


CREATE TABLE IF NOT EXISTS `cubems_servers` (
  `ip` int(10) unsigned NOT NULL default '0',
  `port` smallint(5) unsigned NOT NULL default '0',
  `time` bigint(20) unsigned default NULL,
  PRIMARY KEY  (`ip`,`port`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;