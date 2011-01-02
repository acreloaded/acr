-- AssaultCube Special Edition Masterserver Tables

CREATE TABLE `cubems_auth` (
   `ip` int(10) unsigned,
   `time` bigint(20) unsigned,
   `id` int(10) unsigned,
   `nonce` int(11),
   UNIQUE KEY (`ip`,`id`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;


CREATE TABLE `cubems_servers` (
   `ip` int(10) unsigned,
   `port` smallint(5) unsigned,
   `time` bigint(20) unsigned,
   UNIQUE KEY (`ip`,`port`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;