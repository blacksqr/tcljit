proc sum2 {n} {
    set result 1
    for {set i 0} {$i <= $n} {incr i} {
	if {$i % 4 == 0} {
		set result [expr {$result + $i}]
	} elseif {$i % 3 == 0} {
		set result [expr {$result - $i}]
	}
    }
    return $result
}

if {$argc != 1} {
    puts "Usage: $argv0 n"
} else {
    set n [lindex $argv 0]

    for {set i 0} {$i < $n} {incr i} {
    	sum2 $i
    }
}
