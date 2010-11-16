proc fact {n} {
	set i 2
	set result 1
	while {$i <= $n} {
		set result [expr {$result * $i}]
		set i [expr {$i + 1}]
	}
	return $result
}

if {$argc != 1} {
    puts "Usage: $argv0 n"
} else {
    set n [lindex $argv 0]

    for {set j 0} {$j < $n} {incr j} {
	    for {set i 1} {$i <= 12} {incr i} {
		    fact $i
	    }
    }
}
