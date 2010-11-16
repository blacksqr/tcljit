proc sum {lower upper} {
	set result 0
	while {$lower <= $upper} {
		set result [expr {$result + $lower}]
		set lower [expr {$lower + 1}]
	}
	return $result
}

if {$argc != 1} {
    puts "Usage: $argv0 n"
} else {
    set n [lindex $argv 0]

    for {set i 1} {$i <= $n} {incr i} {
        sum 1 $i
    }
}
