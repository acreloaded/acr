<?
	// server ban-pack optimizer?
	require "banpack_read.php";
	ksort($banpack);
	foreach($banpack as $bs => $be) $banpack2[] = array($bs, $be);
	for($i = 1; $i < sizeof($banpack2); ++$i){
		if($banpack2[$i][1] <= $banpack2[$i - 1][1]){
			echo "already covered";
			unset($banpack2[$i--]);
			$banpack2 = array_values($banpack2);
		}
		elseif($banpack2[$i][0] <= $banpack2[$i - 1][1] + 1){
			echo "overlap ".long2ip($banpack2[$i][0])." - ".long2ip($banpack2[$i][1])." with ".long2ip($banpack2[$i - 1][0])." - ".long2ip($banpack2[$i - 1][1])." \n";
			$banpack2[$i - 1][1] = $banpack2[$i][1];
			unset($banpack2[$i--]);
			$banpack2 = array_values($banpack2);
		}
	}
	foreach($banpack2 as $bv) echo long2ip($bv[0]).",".long2ip($bv[1])."\n";
?>