-- AssaultCube Special Edition Masterserver Tables

CREATE TABLE IF NOT EXISTS `cubems_auth` (
  `ip` int(10) unsigned NOT NULL DEFAULT '0',
  `time` bigint(20) unsigned DEFAULT NULL,
  `id` int(10) unsigned NOT NULL DEFAULT '0',
  `nonce` int(11) DEFAULT NULL,
  PRIMARY KEY (`ip`,`id`)
) ENGINE=MEMORY DEFAULT CHARSET=latin1;


CREATE TABLE IF NOT EXISTS `cubems_authtimes` (
  `ip` mediumint(9) NOT NULL,
  `time` bigint(20) NOT NULL,
  PRIMARY KEY (`ip`)
) ENGINE=MEMORY DEFAULT CHARSET=latin1;


CREATE TABLE IF NOT EXISTS `cubems_servers` (
  `ip` int(10) unsigned NOT NULL DEFAULT '0',
  `port` smallint(5) unsigned NOT NULL DEFAULT '0',
  `time` bigint(20) unsigned NOT NULL,
  `add` tinyint(1) NOT NULL DEFAULT '1',
  PRIMARY KEY (`ip`,`port`)
) ENGINE=MEMORY DEFAULT CHARSET=latin1;
